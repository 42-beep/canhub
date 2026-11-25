/*
 * test_canlog.c · 日志格式自测
 *
 * 不依赖 vcan，纯文件读写，任何机器上都能跑。
 * 测四件事：写进去的能读回来、条数对、内容对、随机跳转对。
 *
 * 编译：make test_canlog
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

#include "canlog.h"

#define N 1000
#define PATH "/tmp/test_canlog.bin"

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failures++; } \
} while (0)

int main(void)
{
    printf("=== 1. 写入 %d 条 ===\n", N);

    canlog_writer_t *w = canlog_open_write(PATH);
    if (!w) { perror("canlog_open_write"); return 1; }

    for (int i = 0; i < N; i++) {
        uint8_t data[8];
        for (int k = 0; k < 8; k++)
            data[k] = (uint8_t)(i + k);

        /* 时间戳每次前进 10ms，模拟 100Hz 的帧率 */
        uint64_t ts = 1000000000ull + (uint64_t)i * 10000000ull;
        uint32_t id = (uint32_t)(0x100 + (i % 16));
        uint8_t  flags = (i % 100 == 0) ? CANLOG_F_SYNTH : 0;

        if (canlog_write(w, ts, id, 8, flags, data) != 0) {
            perror("canlog_write");
            return 1;
        }
    }

    if (canlog_close_write(w) != 0) { perror("canlog_close_write"); return 1; }

    /* 文件大小必须精确：32 + 1000*24 = 24032 */
    struct stat st;
    if (stat(PATH, &st) != 0) { perror("stat"); return 1; }
    printf("  文件大小 = %ld 字节（期望 32 + %d*24 = %d）\n",
           (long)st.st_size, N, 32 + N * 24);
    CHECK(st.st_size == 32 + N * 24, "文件大小不对，说明记录不是定长 24 字节");

    printf("\n=== 2. 读回并校验 ===\n");

    canlog_reader_t *r = canlog_open_read(PATH);
    if (!r) { perror("canlog_open_read"); return 1; }

    printf("  count = %llu（期望 %d）\n", (unsigned long long)canlog_count(r), N);
    CHECK(canlog_count(r) == N, "文件头里的 count 不对");

    canlog_rec_t rec;
    int n = 0;
    int bad = 0;
    while (canlog_read(r, &rec) == 1) {
        uint64_t expect_ts = 1000000000ull + (uint64_t)n * 10000000ull;
        uint32_t expect_id = (uint32_t)(0x100 + (n % 16));

        if (rec.ts_ns != expect_ts || rec.can_id != expect_id)
            bad++;
        if (rec.dlc != 8)
            bad++;
        for (int k = 0; k < 8; k++)
            if (rec.data[k] != (uint8_t)(n + k))
                bad++;

        n++;
    }
    printf("  读回 %d 条，内容不符 %d 条\n", n, bad);
    CHECK(n == N,      "读回的条数不对");
    CHECK(bad == 0,    "有记录内容变了");

    printf("\n=== 3. 随机跳转 ===\n");
    CHECK(canlog_seek(r, 500) == 0, "seek 到 500 失败");
    if (canlog_read(r, &rec) == 1) {
        uint64_t expect = 1000000000ull + 500ull * 10000000ull;
        printf("  第 500 条 ts=%.3f s（期望 %.3f s）\n",
               (double)rec.ts_ns / 1e9, (double)expect / 1e9);
        CHECK(rec.ts_ns == expect, "seek 后读到的不是第 500 条");
    } else {
        CHECK(0, "seek 后读失败了");
    }

    canlog_close_read(r);

    printf("\n=== 4. 拒绝非日志文件 ===\n");
    FILE *fp = fopen("/tmp/not_a_log.txt", "wb");
    fputs("hello, not a canlog file at all\n", fp);
    fclose(fp);
    canlog_reader_t *bad_r = canlog_open_read("/tmp/not_a_log.txt");
    printf("  打开普通文本文件返回 %s\n", bad_r ? "句柄（错！）" : "NULL（对）");
    CHECK(bad_r == NULL, "没有拒绝非法文件");
    canlog_close_read(bad_r);

    printf("\n=== 结果 ===\n");
    if (failures == 0) {
        printf("全部通过\n");
        remove(PATH);
        remove("/tmp/not_a_log.txt");
        return 0;
    }
    printf("%d 项失败\n", failures);
    return 1;
}
