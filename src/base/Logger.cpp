#include <cstdarg>
#include <cstdio>
#include <iostream>

#include "base/Logger.h"


Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, const char* format, ...)
{
    // 格式化日志消息，支持可变参数。
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    const std::string& timeStr = Timestamp::now().toString();
    switch (level)
    {
        case INFO:
            std::cout << "[INFO] " << timeStr << " " << buf << std::endl;
            break;
        case ERROR:
            std::cout << "[ERROR] " << timeStr << " " << buf << std::endl;
            break;
        case FATAL:
            std::cout << "[FATAL] " << timeStr << " " << buf << std::endl;
            break;
        case DEBUG:
            std::cout << "[DEBUG] " << timeStr << " " << buf << std::endl;
            break;
        default:
            std::cout << "[UNKNOWN] " << timeStr << " " << buf << std::endl;
            break;
    }
}
