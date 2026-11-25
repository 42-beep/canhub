/*
 * canhub M2.0 · 最简接收器
 *
 * 目标：从 vcan0 收帧，把 ID / DLC / 原始字节打印出来。
 *
 * 这一版故意不用 epoll，也不用 ringbuf，就一条阻塞 read 死循环。
 * 目的只有一个：先亲眼看见"帧是怎么从内核出来的"。
 * 后面 M2.1 才加 epoll，M2.2 才把帧塞进 ringbuf。
 *
 * 用法：
 *   ./can_receiver            # 默认 vcan0
 *   ./can_receiver can1       # 指定网卡
 *
 * 造数据：
 *   cangen vcan0 -g 100       # 每 100ms 发一帧随机数据
 *
 * 这段代码里有四步，每一步对应一个系统调用：
 *   1. socket(PF_CAN, SOCK_RAW, CAN_RAW)  —— 开一个 CAN 套接字
 *   2. ioctl(SIOCGIFINDEX)                —— 用网卡名换 ifindex（套接字不认名字，只认编号）
 *   3. bind()                             —— 把套接字绑到这张网卡上
 *   4. read()                             —— 等帧，收到一个 struct can_frame
 *
 * 每一步都可能失败，全都要判。这就是 M2 和玩具代码的区别。
 */

/*
 * 这个 define 必须放在所有 include 之前，否则 -std=c11 下
 * <net/if.h> 不会暴露 struct ifreq 和 IFNAMSIZ：
 *   error: storage size of 'ifr' isn't known
 * 原因：严格 C 模式下 glibc 隐藏了 BSD/POSIX 之外的扩展接口，
 *      ifreq 属于后者，要显式声明"我要用这些扩展"。
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "canlog.h"

/*
 * 收到 Ctrl-C / SIGTERM 时不要立刻死，先把日志正常关掉。
 *
 * 为什么必须做这件事：
 *   第一次实测时用 `timeout 2 ./can_receiver -o run.log` 跑，
 *   得到一个 0 字节的文件 —— 因为 SIGTERM 直接终止了进程，
 *   canlog_close_write 根本没执行，文件头里的 count 也没回填。
 *   现场调试时你会无数次 kill 采集进程，不做这个处理，日志基本全丢。
 *
 * 关键细节：这里不能设 SA_RESTART，否则 read() 会被自动重启，
 * 主循环没机会检查 g_stop，进程只能等到下一帧才退出。
 */
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int main(int argc, char **argv)
{
    const char *ifname  = "vcan0";
    const char *logpath = NULL;

    /* 参数：./can_receiver [网卡] [-o 日志文件] */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            logpath = argv[++i];
        } else {
            ifname = argv[i];
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* --- 1. 开套接字 --- */
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    /* --- 2. 网卡名 → ifindex --- */
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(s);
        return 1;
    }

    /* --- 3. 绑定到这张网卡 --- */
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }

    /* --- 4. 日志（可选） --- */
    canlog_writer_t *log = NULL;
    if (logpath) {
        log = canlog_open_write(logpath);
        if (!log) {
            perror(logpath);
            close(s);
            return 1;
        }
    }

    printf("listening on %s (ifindex=%d)%s, Ctrl-C 退出\n",
           ifname, ifr.ifr_ifindex, log ? "，日志写入中" : "");
    fflush(stdout);

    /* --- 5. 收帧死循环 --- */
    struct can_frame frame;
    unsigned long count = 0;

    for (;;) {
        if (g_stop)
            break;

        ssize_t n = read(s, &frame, sizeof(frame));

        if (n < 0) {
            /* 信号打断不是错误，回到循环顶部检查 g_stop */
            if (errno == EINTR)
                continue;
            perror("read");
            break;
        }
        if (n != (ssize_t)sizeof(frame)) {
            fprintf(stderr, "短读: 期望 %zu 字节，实际 %zd\n", sizeof(frame), n);
            continue;
        }

        count++;

        /* 时间戳在这里取，越靠近 read 返回越准 */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ts_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;

        int is_ext = (frame.can_id & CAN_EFF_FLAG) != 0;

        /* 标准帧 11 位 ID，扩展帧 29 位，分开打印才看得清 */
        if (is_ext)
            printf("[%6lu] ID=%08X (扩展) DLC=%d [", count,
                   frame.can_id & CAN_EFF_MASK, frame.can_dlc);
        else
            printf("[%6lu] ID=%03X (标准) DLC=%d [", count,
                   frame.can_id & CAN_SFF_MASK, frame.can_dlc);

        for (int i = 0; i < frame.can_dlc && i < 8; i++)
            printf(" %02X", frame.data[i]);
        printf(" ]\n");
        fflush(stdout);

        if (log) {
            /* ID 只存纯 ID，扩帧标志单独放到 flags —— 别把 Linux 的位定义写进日志 */
            uint8_t lflags = is_ext ? CANLOG_F_EXT : 0;
            uint32_t raw_id = is_ext ? (frame.can_id & CAN_EFF_MASK)
                                     : (frame.can_id & CAN_SFF_MASK);
            canlog_write(log, ts_ns, raw_id, frame.can_dlc, lflags, frame.data);
        }
    }

    if (log)
        canlog_close_write(log);
    close(s);
    return 0;
}
