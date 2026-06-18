#pragma once

#include <cstdlib>
#include <string>

#include "noncopyable.h"
#include "Timestamp.h"

#define LOG_INFO(...) \
    do { Logger::instance().log(INFO, __VA_ARGS__); } while (false)
#define LOG_ERROR(...) \
    do { Logger::instance().log(ERROR, __VA_ARGS__); } while (false)
#define LOG_FATAL(...) \
    do { Logger::instance().log(FATAL, __VA_ARGS__); std::exit(EXIT_FAILURE); } while (false)
#define LOG_DEBUG(...) \
    do { Logger::instance().log(DEBUG, __VA_ARGS__); } while (false)

// 日志级别定义。
enum LogLevel
{
    INFO,
    ERROR,
    FATAL,
    DEBUG,
};

// 简单日志系统，线程不安全但足以用于调试和演示。
class Logger : noncopyable
{
public:
    // 获取单例 Logger 实例。
    static Logger& instance();

    // 格式化并输出日志消息。
    void log(LogLevel level, const char* format, ...);

private:
    Logger() = default;
};
