/*
 * canlog_reader.h · 日志读取（C++ / Qt 侧）
 *
 * canlog.h 是纯 C 头文件 —— canreplay、test_canlog 都是 C 程序，
 * 这份日志格式必须只有一份实现，所以那边不能改成 C++。
 *
 * C++ 引用它必须用 extern "C" 包住，否则：
 *   编译器按 C++ 规则对 canlog_open_read 做名字修饰（_Z17canlog_open_readPKc），
 *   而 canlog.c 编译出的是裸符号 canlog_open_read，链接时报 undefined reference。
 * 这是今天踩到的第 6 个坑，也是混编 C/C++ 最经典的一个。
 *
 * 上面的"桥"只包住了 C 接口。这一层自己的东西全部用 C++：
 *   std::vector 管记录、std::string 传错误、析构函数自动释放。
 */

#ifndef CANVIEW_CANLOG_READER_H
#define CANVIEW_CANLOG_READER_H

#include <cstdint>
#include <string>
#include <vector>

/*
 * 第二个混编坑：canlog.h 里用了 `_Static_assert`，那是 C11 关键字。
 * C++ 里没有这个词，只有 `static_assert`，所以直接 include 会报：
 *   error: expected constructor, destructor, or type conversion before '(' token
 *
 * 不改 canlog.h（它还要给 C 程序用），在这一侧临时做一次名字映射，
 * 用完立刻 #undef，不把宏泄漏给后面的代码。
 */
#ifndef _Static_assert
#define _Static_assert static_assert
#endif

extern "C" {
#include "canlog.h"
}

#undef _Static_assert

/*
 * 一个采样点：时间相对第一条记录（毫秒）+ 原始 8 字节。
 * 存原始字节而不是物理量 —— 解码是 M3 的事，界面只画字节。
 */
struct cv_sample_t {
    double  t_ms = 0.0;
    uint8_t dlc  = 0;
    uint8_t data[8] = {0};
};

/*
 * 一次性把日志读进内存，之后所有查询都是纯内存操作。
 * 100 万帧 = 24 MB，可以接受；真到几千万帧再换 mmap 版本。
 *
 * RAII：析构自动释放，不需要调用方记得 close。
 * 禁止拷贝：内部持有整份数据，拷贝一份就是几 MB 的意外开销。
 */
class CanLogFile
{
public:
    CanLogFile() = default;
    ~CanLogFile() = default;

    CanLogFile(const CanLogFile &) = delete;
    CanLogFile &operator=(const CanLogFile &) = delete;

    /* 成功返回 true；失败返回 false 并把原因写进 *err */
    bool open(const std::string &path, std::string *err);
    void close();

    bool     isOpen() const   { return m_open; }
    uint64_t count() const    { return m_recs.size(); }
    double   durationMs() const;
    std::string path() const  { return m_path; }

    /* 文件里出现过的 CAN ID，升序去重 */
    std::vector<uint32_t> ids() const;

    /* 某个 ID 的全部采样点；ID 不存在时返回空数组 */
    std::vector<cv_sample_t> samples(uint32_t canId) const;

private:
    std::vector<canlog_rec_t> m_recs;
    uint64_t    m_startNs = 0;
    std::string m_path;
    bool        m_open = false;
};

#endif /* CANVIEW_CANLOG_READER_H */
