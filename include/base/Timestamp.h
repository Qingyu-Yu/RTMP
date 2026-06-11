#pragma once

#include <iostream>
#include <string>


// Timestamp 封装了时间戳，精度为微秒。
class Timestamp
{
public:
    // 构造一个默认时间戳，表示时间点 0。
    Timestamp();

    // 使用微秒级时间戳构造对象。
    explicit Timestamp(int64_t microSecondsSinceEpoch);

    // 返回当前系统时间的 Timestamp。
    static Timestamp now();

    // 将时间戳格式化为文本 "YYYY-MM-DD HH:MM:SS.UUUUUU"。
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_; // 自 Epoch 起的微秒数。
};