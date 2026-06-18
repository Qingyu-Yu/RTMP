#include <cstdarg>
#include "base/Logger.h"


Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(LogLevel level)
{
    logLevel_ = level;
}

void Logger::log(std::string msg, ...)
{
    // 格式化日志消息，支持可变参数。
    char buf[1024];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, sizeof(buf), msg.c_str(), args);
    va_end(args);

    const std::string& timeStr = Timestamp::now().toString();
    switch (logLevel_)
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
