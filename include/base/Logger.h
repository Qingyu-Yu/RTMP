#pragma once

#include <iostream>
#include <string>

#include "noncopyable.h"
#include "Timestamp.h"

#define LOG_INFO(msg, ...) Logger::instance().setLogLevel(INFO); Logger::instance().log(msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) Logger::instance().setLogLevel(ERROR); Logger::instance().log(msg, ##__VA_ARGS__)
#define LOG_FATAL(msg, ...) Logger::instance().setLogLevel(FATAL); Logger::instance().log(msg, ##__VA_ARGS__); exit(-1)
#define LOG_DEBUG(msg, ...) Logger::instance().setLogLevel(DEBUG); Logger::instance().log(msg, ##__VA_ARGS__)

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

    // 设置日志级别。
    void setLogLevel(LogLevel level);

    // 格式化并输出日志消息。
    void log(std::string msg, ...);

private:
    LogLevel logLevel_; // 当前日志级别。
    Logger() : logLevel_(INFO) {}
};