/*
 * canlog.c · 二进制日志读写
 *
 * 这个文件只有 150 行，但它决定了后面所有事情的上限：
 * canreplay 靠它，canview 靠它，5 万帧压测也靠它。
 *
 * 三个刻意的选择：
 *   1. 用 fwrite/fread 而不是 mmap —— 先跑通，性能优化留到压测时再说。
 *      （mmap 版本能让 canview 打开 100 MB 文件瞬间到位，这是 M4 的优化点）
 *   2. 关闭时 fsync —— 车载场景拔电是常态，缓冲区里的数据丢了就永远没了。
 *   3. 每条独立 fwrite —— 不做用户态缓冲，因为一次写 24 字节已经是原子操作，
 *      而且崩溃时最多丢最后一条，不会丢一整块。
 */

/*
 * _POSIX_C_SOURCE 必须在所有 include 之前。
 * 不加的话，-std=c11 严格模式下 fileno() 不会声明：
 *   warning: implicit declaration of function 'fileno'
 * 隐式声明在 64 位下会返回 int，而 fd 是 int 侥幸能跑，
 * 但这属于"恰好能跑"，换个平台就是未定义行为。
 */
#define _POSIX_C_SOURCE 200809L

#include "canlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

struct canlog_writer {
    FILE    *fp;
    uint64_t count;
    uint64_t start_ns;
};

struct canlog_reader {
    FILE    *fp;
    uint64_t count;
    uint64_t start_ns;
};

/* ---------------------------------------------------------------- 写侧 */

canlog_writer_t *canlog_open_write(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    FILE *fp = fopen(path, "wb");
    if (!fp)
        return NULL;

    canlog_writer_t *w = calloc(1, sizeof(*w));
    if (!w) {
        fclose(fp);
        return NULL;
    }
    w->fp = fp;

    /* 先写一个占位的头，count 等关闭时回填 */
    canlog_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, CANLOG_MAGIC, 8);
    hdr.version = CANLOG_VERSION;

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        int e = errno;
        fclose(fp);
        free(w);
        errno = e;
        return NULL;
    }

    /*
     * 立刻 flush。
     * 不加这一行的话，32 字节的文件头会停在用户态缓冲区里 ——
     * 进程被 kill 时整份日志变成 0 字节，连"这是个日志文件"都看不出来。
     * 这是实测踩出来的：timeout 2 ./can_receiver -o run.log 直接得到一个空文件。
     */
    fflush(fp);

    return w;
}

int canlog_write(canlog_writer_t *w, uint64_t ts_ns, uint32_t can_id,
                 uint8_t dlc, uint8_t flags, const uint8_t *data)
{
    if (!w || !w->fp) {
        errno = EINVAL;
        return -1;
    }
    if (dlc > 8) {
        errno = EINVAL;
        return -1;
    }

    canlog_rec_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.ts_ns  = ts_ns;
    rec.can_id = can_id;
    rec.dlc    = dlc;
    rec.flags  = flags;
    if (data && dlc > 0)
        memcpy(rec.data, data, dlc);

    /* 第一条记录的时间戳决定回放的零点 */
    if (w->count == 0)
        w->start_ns = ts_ns;

    if (fwrite(&rec, sizeof(rec), 1, w->fp) != 1)
        return -1;

    w->count++;
    return 0;
}

int canlog_close_write(canlog_writer_t *w)
{
    if (!w)
        return 0;

    int ret = 0;
    if (w->fp) {
        /* 回填 count 和 start_ns：跳到文件头重写 */
        canlog_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        memcpy(hdr.magic, CANLOG_MAGIC, 8);
        hdr.version  = CANLOG_VERSION;
        hdr.start_ns = w->start_ns;
        hdr.count    = w->count;

        if (fseek(w->fp, 0, SEEK_SET) == 0 &&
            fwrite(&hdr, sizeof(hdr), 1, w->fp) == 1) {
            fflush(w->fp);
            /* 拔电场景：不 fsync 的话，数据可能还在页缓存里 */
            fsync(fileno(w->fp));
        } else {
            ret = -1;
        }

        if (fclose(w->fp) != 0)
            ret = -1;
    }
    free(w);
    return ret;
}

/* ---------------------------------------------------------------- 读侧 */

canlog_reader_t *canlog_open_read(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    canlog_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        errno = EIO;
        goto fail;
    }

    if (memcmp(hdr.magic, CANLOG_MAGIC, 8) != 0) {
        errno = EINVAL;          /* 不是 canhub 的日志文件 */
        goto fail;
    }
    if (hdr.version != CANLOG_VERSION) {
        errno = EPROTONOSUPPORT; /* 版本不匹配，明确报出来，不要瞎猜 */
        goto fail;
    }

    /*
     * count 为 0 但文件明显有内容 —— 说明上次进程被强制杀掉，
     * canlog_close_write 没机会回填。这时候不能傻傻地报"0 帧"，
     * 按文件大小反推一条不少。
     *
     * 定长记录的价值在这里体现：任何时刻都能算出文件里有几条，
     * 不需要扫描、不需要索引。
     */
    if (hdr.count == 0) {
        if (fseek(fp, 0, SEEK_END) == 0) {
            long sz = ftell(fp);
            if (sz >= (long)sizeof(canlog_header_t)) {
                uint64_t calc = (uint64_t)(sz - sizeof(canlog_header_t)) / sizeof(canlog_rec_t);
                if (calc > 0) {
                    fprintf(stderr,
                            "注意: 文件头 count=0，按大小推算 %llu 条（上次未正常关闭）\n",
                            (unsigned long long)calc);
                    hdr.count = calc;
                }
            }
            if (fseek(fp, sizeof(canlog_header_t), SEEK_SET) != 0)
                goto fail;
        }
    }

    canlog_reader_t *r = calloc(1, sizeof(*r));
    if (!r) {
        errno = ENOMEM;
        goto fail;
    }
    r->fp       = fp;
    r->count    = hdr.count;
    r->start_ns = hdr.start_ns;
    return r;

fail:
    {
        int e = errno;
        fclose(fp);
        errno = e;
        return NULL;
    }
}

int canlog_read(canlog_reader_t *r, canlog_rec_t *out)
{
    if (!r || !r->fp || !out) {
        errno = EINVAL;
        return -1;
    }

    size_t n = fread(out, sizeof(*out), 1, r->fp);
    if (n == 1)
        return 1;

    if (feof(r->fp))
        return 0;

    return -1;
}

uint64_t canlog_count(const canlog_reader_t *r)
{
    return r ? r->count : 0;
}

int canlog_seek(canlog_reader_t *r, uint64_t index)
{
    if (!r || !r->fp) {
        errno = EINVAL;
        return -1;
    }
    if (index > r->count) {
        errno = ERANGE;
        return -1;
    }

    /* 定长记录的好处：第 index 条的偏移能直接算出来，不用从头读 */
    long off = (long)sizeof(canlog_header_t) + (long)(index * sizeof(canlog_rec_t));
    if (fseek(r->fp, off, SEEK_SET) != 0)
        return -1;

    return 0;
}

void canlog_close_read(canlog_reader_t *r)
{
    if (!r)
        return;
    if (r->fp)
        fclose(r->fp);
    free(r);
}
