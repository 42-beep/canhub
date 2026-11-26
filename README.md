# canhub

Linux 下的 CAN 数据中枢：从 SocketCAN 收帧，写二进制日志，回放并分析。

目标是把「总线上的原始帧」变成「可重现、可回放、能定位问题的数据流」，
而不是只会跑一个 `candump`。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| M2.0 | SocketCAN 阻塞 read 收帧 | 已完成 |
| M2.1 | epoll 多路复用（多网卡） | 待做 |
| M2.2 | 接入 ringbuf（SPSC 无锁队列） | 待做 |
| M3 | DBC 解码（字节 → 物理量） | 待做 |
| M4 | 日志格式与回放 | 格式已完成 |
| M5 | canview（Qt 上位机，读离线日志画曲线） | V1 已完成 |

详见 [STATUS.md](STATUS.md)。

## 目录

```
Makefile
STATUS.md            里程碑与踩坑清单
src/canlog.h         .canlog 二进制日志格式（文件头 32B + 记录 24B）
src/canlog.c         日志读写实现
src/can_receiver.c   SocketCAN 收帧，可同时写日志
src/canreplay.c      回放日志，输出格式对齐 candump
src/test_canlog.c    日志格式自测（写 1000 条 → 读回 → 校验 → 随机跳转）
src/canview/         Qt 上位机（C++，QCustomPlot 画曲线）
```

## 依赖

- Linux（SocketCAN 内核模块）
- gcc、make
- 测试用 `can-utils`（`cangen` 造帧、`candump` 对照）

**本机开发用原生 gcc 编译，不是交叉编译。** 移植到 ARM 板时改 Makefile 里的
`CC := arm-linux-gnueabihf-gcc` 并补 `--sysroot`。

## 编译

```bash
make            # 生成 can_receiver / canreplay / test_canlog
make test       # 跑日志格式自测
make clean
```

## 准备虚拟 CAN 环境

没有硬件也能跑。虚拟网卡重启后会丢，需要重建：

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

## 运行

造帧 + 收帧 + 写日志：

```bash
cangen vcan0 -g 20 -I 123 -L 8 &      # 每 20ms 一帧，ID=0x123
./can_receiver vcan0 -o run.log
```

回放：

```bash
./canreplay run.log
```

收到 Ctrl-C / SIGTERM 时会优雅退出并回填记录条数，日志不会丢。

## canview（Qt 上位机）

依赖 Qt5 与 QCustomPlot：

```bash
sudo apt install qtbase5-dev qtbase5-dev-tools fonts-wqy-microhei
cd src/canview
qmake && make -j4
```

用法：

```bash
./canview run.log                  # 窗口：选 ID、选字节、拖拽缩放
./canview --png out.png run.log    # 无界面渲染一张图（QT_QPA_PLATFORM=offscreen 可用）
```

界面只有三件事：打开 `.canlog`、列出文件里出现过的 CAN ID、画出某个字节随时间的变化。
**不接实时源**——实时数据每秒在变，出问题复现不了；文件数据跑一百遍都一样。

混编说明：`canlog.c` 保持 C（`canreplay` / `test_canlog` 共用同一份实现），
C++ 侧用 `extern "C"` 桥接。两个必须处理的差异写在 `canlog_reader.h` 里：
名字修饰、以及 `_Static_assert`（C11）vs `static_assert`（C++）。

## 日志格式

定长二进制，为的是能 `mmap` 当数组直接索引。

```
[文件头 32B] [记录 24B] × N

文件头: magic(8) version(2) flags(2) reserved(4) start_ns(8) count(8)
记录:   ts_ns(8) can_id(4) dlc(1) flags(1) reserved(2) data(8)
```

`can_id` 只存 11/29 位原始值，扩展帧标志放在 `flags` 里，不混进 ID。

## 已知问题

- `ringbuf_write` / `ringbuf_read` 逐字节循环，未改 memcpy 分段（M2.2 接入时一起做）
- 日志文件 `count` 只在正常关闭时回填；异常掉电时靠读侧按文件大小反推
- epoll 版本未开始：前置是线程、非阻塞 IO、fd 本质，先补 Linux 系统编程
- 日志格式里**没有存接口名**，所以 `canreplay` 只能硬编码打印 `can0`（哪怕数据来自 `vcan0`）
- Qt 画图前要判 `dlc`：`data[dlc..7]` 是上一帧的残留，不判就会画出假尖刺
