#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

// Timestamp 封装了时间戳，精度为微秒。
class Timestamp
{
public:
    static const int kMicroSecondsPerSecond = 1000 * 1000;

    // 构造一个默认时间戳，表示时间点 0。
    Timestamp() = default;

    // 使用微秒级时间戳构造对象。
    explicit Timestamp(int64_t microSecondsSinceEpoch)
        : microSecondsSinceEpoch_(microSecondsSinceEpoch)
    {
    }

    // 返回当前系统时间的 Timestamp。
    static Timestamp now();

    // 返回无效时间戳。
    static Timestamp invalid() { return Timestamp(); }

    // 从 Unix 秒和微秒构造时间戳。
    static Timestamp fromUnixTime(std::time_t seconds, int microseconds = 0);

    // 返回 "seconds.microseconds"，适合日志和调试。
    std::string toString() const;

    // 返回 UTC 时间；showMicroseconds 为 true 时附带 6 位微秒。
    std::string toFormattedString(bool showMicroseconds = true) const;

    bool valid() const { return microSecondsSinceEpoch_ > 0; }
    int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }
    std::time_t secondsSinceEpoch() const
    {
        return static_cast<std::time_t>(
            microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
    }

    void swap(Timestamp& that)
    {
        std::swap(microSecondsSinceEpoch_, that.microSecondsSinceEpoch_);
    }

private:
    int64_t microSecondsSinceEpoch_{0}; // 自 Epoch 起的微秒数。
};

inline bool operator<(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() < rhs.microSecondsSinceEpoch();
}

inline bool operator==(Timestamp lhs, Timestamp rhs)
{
    return lhs.microSecondsSinceEpoch() == rhs.microSecondsSinceEpoch();
}

// 返回 high - low，单位为秒。
double timeDifference(Timestamp high, Timestamp low);

// 在 timestamp 上增加 seconds 秒。
Timestamp addTime(Timestamp timestamp, double seconds);
