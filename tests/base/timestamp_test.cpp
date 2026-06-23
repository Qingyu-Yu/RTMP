#include <cmath>
#include <iostream>
#include <string>

#include "base/Timestamp.h"

namespace
{

bool expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    const Timestamp epoch = Timestamp::fromUnixTime(1, 123456);
    const Timestamp later = addTime(epoch, 1.5);

    if (!expect(epoch.valid(), "timestamp should be valid") ||
        !expect(epoch.toString() == "1.123456", "unexpected toString") ||
        !expect(
            epoch.toFormattedString() == "19700101 00:00:01.123456",
            "unexpected formatted timestamp") ||
        !expect(epoch < later, "timestamp comparison failed") ||
        !expect(
            std::fabs(timeDifference(later, epoch) - 1.5) < 1e-9,
            "timeDifference failed") ||
        !expect(!Timestamp::invalid().valid(), "invalid timestamp is valid"))
    {
        return 1;
    }
    return 0;
}
