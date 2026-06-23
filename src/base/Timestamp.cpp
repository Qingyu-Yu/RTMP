#include "base/Timestamp.h"

#include <cstdio>
#include <sys/time.h>


Timestamp Timestamp::now()
{
    struct timeval tv;
    ::gettimeofday(&tv, nullptr);
    const int64_t us =
        static_cast<int64_t>(tv.tv_sec) * kMicroSecondsPerSecond + tv.tv_usec;
    return Timestamp(us);
}

std::string Timestamp::toString() const
{
    char buf[32] = {0};
    const int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
    const int64_t microseconds =
        microSecondsSinceEpoch_ % kMicroSecondsPerSecond;
    std::snprintf(buf, sizeof buf, "%lld.%06lld",
                  static_cast<long long>(seconds),
                  static_cast<long long>(microseconds));
    return buf;
}

std::string Timestamp::toFormattedString(bool showMicroseconds) const
{
    char buf[64] = {0};
    const std::time_t seconds = secondsSinceEpoch();
    struct tm utcTime;
    ::gmtime_r(&seconds, &utcTime);

    if (showMicroseconds)
    {
        const int microseconds = static_cast<int>(
            microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
        std::snprintf(buf, sizeof buf,
                      "%4d%02d%02d %02d:%02d:%02d.%06d",
                      utcTime.tm_year + 1900,
                      utcTime.tm_mon + 1,
                      utcTime.tm_mday,
                      utcTime.tm_hour,
                      utcTime.tm_min,
                      utcTime.tm_sec,
                      microseconds);
    }
    else
    {
        std::snprintf(buf, sizeof buf,
                      "%4d%02d%02d %02d:%02d:%02d",
                      utcTime.tm_year + 1900,
                      utcTime.tm_mon + 1,
                      utcTime.tm_mday,
                      utcTime.tm_hour,
                      utcTime.tm_min,
                      utcTime.tm_sec);
    }
    return buf;
}

Timestamp Timestamp::fromUnixTime(std::time_t seconds, int microseconds)
{
    return Timestamp(
        static_cast<int64_t>(seconds) * kMicroSecondsPerSecond + microseconds);
}

double timeDifference(Timestamp high, Timestamp low)
{
    const int64_t difference =
        high.microSecondsSinceEpoch() - low.microSecondsSinceEpoch();
    return static_cast<double>(difference) /
           Timestamp::kMicroSecondsPerSecond;
}

Timestamp addTime(Timestamp timestamp, double seconds)
{
    const int64_t delta = static_cast<int64_t>(
        seconds * Timestamp::kMicroSecondsPerSecond);
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}
