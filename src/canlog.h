/*
 * canlog.h · 二进制日志格式（M4 的地基）
 *
 * 为什么先做这个，而不是先做界面？
 *   因为 canview 和 canreplay 都要读它。而且调试界面时，
 *   实时数据每秒钟都在变，出了问题没法复现；
 *   文件数据是固定的，同样的输入跑一百遍都一样，好定位。
 *
 * 为什么用二进制而不是文本（JSON Lines / CSV）？
 *   一帧 JSON 大约 120 字节，二进制 24 字节，差 5 倍。
 *   3800 帧/秒的压测下，文本格式光序列化就吃满一个核。
 *   而且定长记录的读取是 O(1) 的指针偏移，不需要解析。
 *
 * 文件布局：
 *   [文件头 32 字节] [记录 24 字节] × N
 *   定长记录意味着 canview 可以直接 mmap 整个文件，
 *   把第 i 条当成数组元素访问，不用顺序读。
 */

#ifndef CANHUB_CANLOG_H
#define CANHUB_CANLOG_H

#include <stdint.h>
#include <stddef.h>

#define CANLOG_MAGIC     "CANHUBLG"   /* 8 字节，读到它就确认是日志文件 */
#define CANLOG_VERSION   1

/* 记录 flags 的位定义 */
#define CANLOG_F_EXT     0x01   /* 扩展帧（29 位 ID） */
#define CANLOG_F_RTR     0x02   /* 远程帧 */
#define CANLOG_F_ERR     0x04   /* 错误帧 */
#define CANLOG_F_SYNTH   0x08   /* 合成帧（测试注入） */

/*
 * 文件头，固定 32 字节。
 * 记录条数写在头里，canview 打开文件就知道总量，能立刻算时间范围。
 *
 * 字段顺序是算过的，不是随便排的：
 *   8 + 2 + 2 + 4 + 8 + 8 = 32，而且每个字段都落在自然对齐位置上，
 *   编译器不需要插任何填充字节，也不用加 packed。
 *
 * 如果按直觉排成 magic(8) | version(2) | flags(2) | start_ns(8) | count(8) | reserved(4)，
 * start_ns 前面只剩 12 字节、不满足 8 字节对齐，编译器会补 4 字节，
 * sizeof 变成 40 —— 这个坑我在 frame_header_t 上已经踩过一次。
 */
typedef struct {
    char     magic[8];      /* "CANHUBLG" */
    uint16_t version;       /* CANLOG_VERSION */
    uint16_t flags;         /* 保留，填 0 */
    uint32_t reserved;      /* 凑满 32 字节，同时把下面两个 8 字节字段顶到对齐位置 */
    uint64_t start_ns;      /* 第一条记录的时间戳，回放时作为零点 */
    uint64_t count;         /* 记录总条数，关闭时回填 */
} canlog_header_t;

/*
 * 一条记录，固定 24 字节。
 * 字段顺序是刻意的：时间戳在前，因为按时间查找是最常见的操作。
 *
 * 注意 can_id 只存 29 位原始值，扩展帧标志放在 flags 里，
 * 不要把 CAN_EFF_FLAG 混进 ID —— 那是 Linux 的定义，不是协议本身的定义。
 */
typedef struct {
    uint64_t ts_ns;         /* CLOCK_MONOTONIC 纳秒 */
    uint32_t can_id;        /* 11 位或 29 位，不含标志位 */
    uint8_t  dlc;           /* 数据长度 0-8 */
    uint8_t  flags;         /* CANLOG_F_* */
    uint8_t  reserved[2];
    uint8_t  data[8];
} canlog_rec_t;

_Static_assert(sizeof(canlog_header_t) == 32, "日志文件头必须是 32 字节");
_Static_assert(sizeof(canlog_rec_t) == 24, "日志记录必须是 24 字节");

/* 不透明句柄，调用方不需要知道内部是 FILE* 还是 mmap */
typedef struct canlog_writer canlog_writer_t;
typedef struct canlog_reader canlog_reader_t;

/*
 * 写侧
 *
 * canlog_open_write: 成功返回句柄，失败返回 NULL（errno 带原因）。
 *                    会立刻写出 32 字节文件头（count 先填 0）。
 * canlog_write:      成功 0，失败 -1。一次调用写一条，内部 fwrite 24 字节。
 * canlog_close:      回填 count 到文件头，然后关闭。会 flush + fsync，
 *                    因为"写完就断电"是车载场景的常态。
 */
canlog_writer_t *canlog_open_write(const char *path);
int              canlog_write(canlog_writer_t *w, uint64_t ts_ns, uint32_t can_id,
                              uint8_t dlc, uint8_t flags, const uint8_t *data);
int              canlog_close_write(canlog_writer_t *w);

/*
 * 读侧
 *
 * canlog_open_read: 校验 magic 和 version，失败返回 NULL。
 * canlog_read:      1 读到一条；0 到文件尾；-1 出错。
 * canlog_count:     总条数（从文件头读的，O(1)）。
 * canlog_seek:      跳到第 index 条，供 canview 拖动时间轴用。0 成功，-1 失败。
 * canlog_close_read: 关闭，传 NULL 安全。
 */
canlog_reader_t *canlog_open_read(const char *path);
int              canlog_read(canlog_reader_t *r, canlog_rec_t *out);
uint64_t         canlog_count(const canlog_reader_t *r);
int              canlog_seek(canlog_reader_t *r, uint64_t index);
void             canlog_close_read(canlog_reader_t *r);

#endif /* CANHUB_CANLOG_H */
