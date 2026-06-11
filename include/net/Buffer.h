#pragma once

#include <vector>
#include <string>
#include <cstddef>

// Buffer 封装了一个可读可写的环形字节缓冲区，适合网络 IO 数据收发。
class Buffer
{
public:
    static const size_t kCheapPrepend = 8; // 预留的可写前置空间，方便在缓冲区头部插入数据。
    static const size_t kInitialSize = 1024; // 默认初始容量。

    explicit Buffer(size_t initialSize = kInitialSize);
    ~Buffer() = default;

    // 可读字节数。
    size_t readableBytes() const;

    // 可写字节数。
    size_t writableBytes() const;

    // 预留的可读前置字节数。
    size_t prependableBytes() const;

    // 返回当前可读数据的起始指针。
    const char* peek() const;

    // 读取 len 个字节，如果 len >= 可读字节数则清空缓冲区。
    void retrieve(size_t len);

    // 清空缓冲区中的所有可读数据。
    void retrieveAll();

    // 读取所有数据并以字符串形式返回。
    std::string retrieveAllAsString();

    // 读取 len 个字节并返回字符串。
    std::string retrieveAsString(size_t len);

    // 返回可写区域的起始指针。
    char* beginWrite();
    const char* beginWrite() const;

    // 通知缓冲区已经写入 len 字节数据。
    void hasWritten(size_t len);

    // 确保缓冲区至少有 len 字节的可写空间。
    void ensureWritableBytes(size_t len);

    // 将数据追加到缓冲区末尾。
    void append(const char* data, size_t len);
    void append(const std::string& str);

    // 直接从文件描述符读取数据，返回读取字节数。
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin();
    const char* begin() const;
    void makeSpace(size_t len);

    std::vector<char> buffer_; // 底层存储空间。
    size_t readerIndex_;       // 可读数据的起始位置。
    size_t writerIndex_;       // 可写数据的起始位置。
};
