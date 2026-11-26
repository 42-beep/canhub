#include "canlog_reader.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace {

void setError(std::string *err, const std::string &msg)
{
    if (err != nullptr)
        *err = msg;
}

}  // namespace

bool CanLogFile::open(const std::string &path, std::string *err)
{
    close();

    canlog_reader_t *r = canlog_open_read(path.c_str());
    if (r == nullptr) {
        setError(err, "打不开或格式不对：" + std::string(std::strerror(errno)));
        return false;
    }

    /* 文件头里的 count 是 O(1) 读到的，先按它预留，避免反复扩容 */
    const uint64_t total = canlog_count(r);
    if (total == 0) {
        canlog_close_read(r);
        setError(err, "文件里没有记录");
        return false;
    }
    m_recs.reserve(static_cast<size_t>(total));

    canlog_rec_t tmp;
    int rc = 0;
    while ((rc = canlog_read(r, &tmp)) == 1)
        m_recs.push_back(tmp);      /* 不信任文件头的 count，按实际读到的算 */
    canlog_close_read(r);

    if (rc < 0) {
        m_recs.clear();
        setError(err, "读到第 " + std::to_string(m_recs.size()) + " 条时出错");
        return false;
    }
    if (m_recs.empty()) {
        setError(err, "文件里没有记录");
        return false;
    }

    m_startNs = m_recs.front().ts_ns;
    m_path    = path;
    m_open    = true;
    return true;
}

void CanLogFile::close()
{
    m_recs.clear();
    m_recs.shrink_to_fit();
    m_startNs = 0;
    m_path.clear();
    m_open = false;
}

double CanLogFile::durationMs() const
{
    if (m_recs.size() < 2)
        return 0.0;

    const uint64_t last = m_recs.back().ts_ns;
    if (last < m_startNs)
        return 0.0;                 /* 时间戳倒退，别算出负数 */
    return static_cast<double>(last - m_startNs) / 1e6;
}

std::vector<uint32_t> CanLogFile::ids() const
{
    std::vector<uint32_t> v;
    v.reserve(m_recs.size());

    for (const canlog_rec_t &rec : m_recs)
        v.push_back(rec.can_id);

    /* C 里要 qsort + 双指针压缩，C++ 三行搞定 */
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

std::vector<cv_sample_t> CanLogFile::samples(uint32_t canId) const
{
    std::vector<cv_sample_t> out;

    for (const canlog_rec_t &rec : m_recs) {
        if (rec.can_id != canId)
            continue;

        cv_sample_t s;
        const uint64_t d = (rec.ts_ns >= m_startNs) ? (rec.ts_ns - m_startNs) : 0;
        s.t_ms = static_cast<double>(d) / 1e6;
        s.dlc  = rec.dlc;
        std::memcpy(s.data, rec.data, sizeof(s.data));
        out.push_back(s);
    }

    return out;
}
