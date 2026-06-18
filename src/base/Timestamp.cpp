#include "base/Timestamp.h"
#include <sys/time.h>
#include <ctime>
#include <cstdio>

Timestamp::Timestamp()
: microSecondsSinceEpoch_(0)
{
}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
: microSecondsSinceEpoch_(microSecondsSinceEpoch)
{
}

Timestamp Timestamp::now()
{
    struct timeval tv;
    ::gettimeofday(&tv, nullptr);
    int64_t us = static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
    return Timestamp(us);
}

std::string Timestamp::toString() const
{
    // 将微秒时间戳转换为本地时间字符串格式。
    time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / 1000000);
    int microsec = static_cast<int>(microSecondsSinceEpoch_ % 1000000);
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);
    char buf[128];
    snprintf(buf, sizeof(buf), "%4d-%02d-%02d %02d:%02d:%02d.%06d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, microsec);
    return std::string(buf);
}
