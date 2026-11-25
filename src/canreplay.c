/*
 * canreplay.c · 日志回放
 *
 * 读 can_receiver 写下的二进制日志，按原时间间隔重新播出来。
 *
 * 这个工具看起来简单，但它是"故障复现"的唯一手段：
 *   现场总线断了 3 秒，事后没人能重现。有日志就能一帧不差地重播。
 *   这也是 §8.2 演示脚本第 6 步的东西。
 *
 * 用法：
 *   ./canreplay run.log             # 立即全速输出
 *   ./canreplay run.log -r          # 按原始时间间隔重放
 *   ./canreplay run.log -r -s 2     # 两倍速
 *
 * 输出格式故意做得和 candump 一样，因为调试时你的眼睛已经习惯了那个样子：
 *   (   0.010000) can0 123   [8] 80 15 1A 44 5D 0D 07 5E
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "canlog.h"

static void sleep_until_ns(uint64_t target_ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(target_ns / 1000000000ull);
    ts.tv_nsec = (long)(target_ns % 1000000000ull);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int realtime = 0;
    double speed = 1.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            realtime = 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            speed = atof(argv[++i]);
            if (speed <= 0.0) {
                fprintf(stderr, "-s 必须是正数\n");
                return 1;
            }
        } else {
            path = argv[i];
        }
    }

    if (!path) {
        fprintf(stderr,
                "用法: %s 日志文件 [-r 按原始时间] [-s 倍速]\n"
                "例:   %s run.log -r -s 2\n", argv[0], argv[0]);
        return 1;
    }

    canlog_reader_t *r = canlog_open_read(path);
    if (!r) {
        perror(path);
        return 1;
    }

    uint64_t total = canlog_count(r);
    printf("# %s: %llu 帧%s\n", path, (unsigned long long)total,
           realtime ? "（按原始时间重放）" : "（全速）");

    canlog_rec_t rec;
    uint64_t first_ts = 0;
    uint64_t t0 = 0;
    uint64_t n = 0;

    while (canlog_read(r, &rec) == 1) {
        if (n == 0) {
            first_ts = rec.ts_ns;
            t0 = now_ns();      /* 重放起点，和日志零点对齐 */
        }

        if (realtime) {
            double rel_s = (double)(rec.ts_ns - first_ts) / 1e9 / speed;
            sleep_until_ns(t0 + (uint64_t)(rel_s * 1e9));
        }

        double rel = (double)(rec.ts_ns - first_ts) / 1e9;

        printf("(%11.6f) can0 %-3X  [%d]",
               rel, rec.can_id, rec.dlc);
        for (int i = 0; i < rec.dlc && i < 8; i++)
            printf(" %02X", rec.data[i]);
        printf("\n");

        n++;
    }

    canlog_close_read(r);

    printf("# 回放结束，共 %llu 帧\n", (unsigned long long)n);
    return (n == total) ? 0 : 1;
}
