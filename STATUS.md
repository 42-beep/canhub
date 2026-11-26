# canhub · 状态卡

> 完整方案在 `../../项目重构-canhub方案.md`。这里只记状态和下一个动作。

## 状态

**M1 已完成（2026.11.23）：ringbuf 11 个函数全实现，`make test` 44 checks / 0 failures。**

**M2.0 已完成（2026.11.25）：`src/can_receiver.c` 用阻塞 read 从 vcan0 收帧并打印，3 秒 30 帧。**

**M4 地基已完成（2026.11.25）：`src/canlog.c` 二进制日志（32B 头 + 24B 定长记录），
`src/canreplay.c` 回放工具。端到端实测：收帧写日志 96 帧 / 2336 字节，回放时间间隔与 cangen 的 -g 20 完全吻合。**

**M5 / canview V1 进行中（2026.11.26）：`src/canview/` 全 C++（QCustomPlot + Qt5 Widgets），
读离线 `.canlog` 画单个 ID 某个字节的曲线。不接实时源。**

M2 拆成两段：
- **M2.0 阻塞 read 收帧** —— 已跑通，不需要 epoll
- **M2.1 epoll 多路复用** —— 仍等 2-A 的 epoll 练习，epoll 没学通，写了也是抄

下一步：**canview 编译通过 + 画出第一条曲线**，然后把 socketcan 实时源接上（1 天）。
理由：文件数据固定，同样的输入跑一百遍都一样，好定位问题；实时数据每秒在变，出问题复现不了。

## 项目线（2026.11.25 定稿，不再改）

| | 是什么 | 状态 |
|---|---|---|
| **项目一** | canhub：ringbuf + socketcan + DBC + 日志回放 + Qt 界面 | M1/M2.0 完成，界面或回放二选一推进 |
| **项目二** | **字符设备驱动 + I2C 温度传感器**（方案 §8 候选 B） —— 不是 ISO-TP/UDS | 等 6ULL 到板（2027.1） |

> UDS 方向 2026.11.25 已否。理由：第二个项目要的是"我碰过内核"的证据，不是再叠一层协议。
> 驱动必须上板跑，WSL 里只能写和编译，不能加载 —— 所以真实开工时间锁在 6ULL 到手之后。


## 踩坑清单（简历原料，每踩一个记一行）

> 格式：**现象 → 排查过程 → 根因 → 修法**。
> 这四段是面试时"你遇到过什么问题"的标准答案，临时编是编不出来的。
> 到 2027.4 投简历时，从这里挑 2-3 条写进 `简历\简历.md` 的 V2。

| # | 日期 | 现象 | 根因 | 修法 |
|---|---|---|---|---|
| 1 | 2026.11.23 | 代码改在 D 盘，`make test` 仍是 34 failures | 编译的是 WSL 里另一份副本 | 软链接 `~/work/ringbuf → D:\career_plan\项目\ringbuf` |
| 2 | 2026.11.25 | `-std=c11` 下报 `storage size of 'ifr' isn't known` / `IFNAMSIZ undeclared` | glibc 严格 C 模式隐藏 BSD/POSIX 扩展，`<net/if.h>` 不暴露 `struct ifreq` | 文件首行加 `#define _DEFAULT_SOURCE`，必须在所有 include 之前 |
| 3 | 2026.11.25 | 线格式包头字段加起来 10 字节，`_Static_assert(sizeof == 10)` 却失败 | 结构体按 4 字节对齐，末尾补了 2 字节，sizeof 变 12 | 线格式结构体加 `__attribute__((packed))`；内存格式反而要显式 `_pad` 钉死布局 |
| 4 | 2026.11.25 | 同一个对齐坑踩了第二次：`canlog_header_t` 字段和是 32，sizeof 却是 40 | `start_ns`（8 字节）前面只有 12 字节，编译器补 4 字节到 16 | 调整字段顺序：`magic(8)+version(2)+flags(2)+reserved(4)+start_ns(8)+count(8)`，每个字段落在自然对齐位，无需 packed |
| 5 | 2026.11.25 | `timeout 2 ./can_receiver -o run.log` 跑完得到一个 **0 字节**文件 | SIGTERM 直接终止进程，`canlog_close_write` 没执行；32 字节文件头还停在 FILE* 用户态缓冲区里 | ① 写头后立刻 `fflush`；② 注册 SIGINT/SIGTERM handler 设 `g_stop`，read 返回 `EINTR` 时 `continue` 回循环顶部优雅退出；③ 读侧容错：count=0 时按文件大小反推条数 |
| 6 | 2026.11.25 | `pkill -f cangen` 之后整个 shell 也死了，后面的命令一行没执行 | `-f` 匹配**完整命令行**，而执行这条命令的 shell 自己的命令行里就含 "cangen" | 用 `pkill -x cangen`（精确匹配进程名）。**教训：用 `-f` 杀进程之前，先想清楚自己会不会被匹配到** |
| 7 | 2026.11.26 | C++ 直接 `#include "canlog.h"` 后链接报 `undefined reference to canlog_open_read` | C++ 对函数名做名字修饰（`_Z17canlog_open_readPKc`），而 canlog.c 编译出的是裸符号 —— **C/C++ 混编的经典坑** | C++ 侧用 `extern "C" { #include "canlog.h" }` 包住；只包 C 的部分，自己的代码照常用 vector/string |
| 8 | 2026.11.26 | `make` 报 `warning: Clock skew detected. Your build may be incomplete.` | 源码在 `/mnt/d`（Windows 挂载），文件时间戳精度和 WSL 内部 ext4 不一致，make 误判新旧 | 遇到就 `make clean` 重来，**不要怀疑代码**。彻底解法是把源码放 WSL 内部（~/work 直接放文件，不走软链接） |
| 9 | 2026.11.26 | `canreplay` 打印出 `can0`，但日志明明是从 `vcan0` 收的 | 日志格式里**没有存接口名**（32B 头只存了 magic/version/start_ns/count），回放工具只能写死一个名字 | 待修：头里 `reserved` 那 4 字节改成 ifindex，或者下次改格式版本时加一个接口名字段。**格式设计的教训：调试信息要一起存进去** |
| 10 | 2026.11.26 | canview 画 `data[0]` 时，某些帧后面几个字节是上一帧的残留，曲线出现尖刺 | `-D i` 模式下 cangen 的 DLC 会变（3/2/8），`data[dlc..7]` 是**未定义内容** | 画图前判 `byteIdx < dlc`，超出就按 0 处理。这不是画图的问题，是**任何读 CAN 数据的地方都必须判 dlc** |
| 11 | 2026.11.26 | C++ 编译 `canlog.h` 报 `expected constructor, destructor, or type conversion before '(' token` | `_Static_assert` 是 **C11 关键字**，C++ 里没有这个词（只有 `static_assert`）。这是混编的第二个坑，紧跟在名字修饰后面 | 在 C++ 侧的 include 前临时 `#define _Static_assert static_assert`，包完 `#undef`，不改 `canlog.h`（它还要给 C 程序用） |
| 12 | 2026.11.26 | 曲线画出来了，但 X/Y 轴标签全是 **□□ 方块** | WSL 的 Ubuntu 没装中文字体，Qt 找不到字形就用豆腐块代替 | `sudo apt install fonts-wqy-microhei`，一行解决。**Linux 上跑任何带中文的 GUI，先装字体** |

### 流程坑（不是技术坑，但对进度影响更大）

> 这些不进简历，进 `/d/.workbuddy/memory/MEMORY.md`。记在这里是因为它们真实拖慢过进度。

| # | 日期 | 发生了什么 | 后果 |
|---|---|---|---|
| P1 | 2026.11.25 | 擅自把「第二个项目」定成 ISO-TP/UDS，还提前实现了 §9 三个留口子 | 用户要求全部删除，白干一轮 |
| P2 | 2026.11.25 | 未经确认就加了 `canlog.c` / `canreplay.c` / 改 `can_receiver.c` | 用户质问"你是在干嘛啊"，Makefile 反复改 |
| P3 | 2026.11.25 | Makefile 一晚上被整份覆盖 5 次（4 次是 Write 而非 Edit） | 每次都要靠重新编译来验证没漏东西，**是运气不是方法** |
| P4 | 2026.11.25-26 | 连续两天大量回答概念问题（接口/驱动/I/O/协议/线程池…），代码零进展 | 用户自己发现"用问概念回避写代码"这个模式 |

**规矩（已写进 MEMORY.md）**：动手前先给「改哪几个文件 / 加什么 / 不改什么 / 怎么验收」，用户回"做"才执行。只读操作不需要问。


**判定标准**：能写成"现象 → 根因 → 修法"三段的，进简历；
"我忘了保存""路径写错"这类，只算流程问题，不写。

## 开工那天做什么

```bash
mkdir -p ~/work/canhub/{src,inc,test}
cp ~/work/ringbuf/src/ringbuf.c ~/work/canhub/src/     # M1 直接搬，不重写
```

第一个文件不是 socketcan，是把已经写好的 ringbuf 搬过来跑通测试。
**复用，不重写**——这是 canhub 存在的意义。

## 五个模块（只有 M1 是现在的事）

| 模块 | 内容 | 周期 | 开始时间 |
|---|---|---|---|
| **M1** | ringbuf（无锁环形缓冲） | 2 天 | **就是现在，在 ringbuf 项目里做** |
| M2 | socketcan 收帧 + epoll | 3 周 | 2027.3 |
| M3 | DBC 解码 + 故障状态机 | 4 周 | 2027.4 |
| M4 | 二进制日志 + 回放 | 2 周 | 2027.5 |
| M5 | Qt 界面（或 DDS 网关） | 3-5 周 | 2027.5-6 |

## 硬件

**一块都不用买。** vcan + candump + cangen 全部免费，WSL 里已验证可用。

6ULL 板 2027 年 1 月才下单，用途是 3 月的交叉编译上板，不是写 canhub。

## 别做的事

- 不要现在建 `src/` 塞一堆空文件——空文件不会让你更接近开工
- 不要为了 canhub 提前学 socketcan——W4 才学，现在学了会忘
- 不要把 canhub 的方案再改一版——方案已经定稿，改方案是最省力的假装努力
