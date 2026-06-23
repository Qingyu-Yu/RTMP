#include "net/Buffer.h"
#include <sys/uio.h>
#include <unistd.h>
#include <algorithm>
#include <errno.h>

Buffer::Buffer(size_t initialSize)
    : buffer_(kCheapPrepend + initialSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend)
{
    // 初始化时在开头预留 kCheapPrepend 字节，方便以后在头部插入数据。
}

size_t Buffer::readableBytes() const
{
    return writerIndex_ - readerIndex_;
}

size_t Buffer::writableBytes() const
{
    return buffer_.size() - writerIndex_;
}

size_t Buffer::prependableBytes() const
{
    return readerIndex_;
}

const char* Buffer::peek() const
{
    return begin() + readerIndex_;
}

char* Buffer::beginWrite()
{
    return begin() + writerIndex_;
}

const char* Buffer::beginWrite() const
{
    return begin() + writerIndex_;
}

void Buffer::retrieve(size_t len)
{
    if (len < readableBytes())
    {
        readerIndex_ += len;
    }
    else
    {
        retrieveAll();
    }
}

void Buffer::retrieveAll()
{
    // 读取完毕后重置索引，避免缓冲区无限增长。
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
}

std::string Buffer::retrieveAllAsString()
{
    return retrieveAsString(readableBytes());
}

std::string Buffer::retrieveAsString(size_t len)
{
    len = std::min(len, readableBytes());
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

void Buffer::hasWritten(size_t len)
{
    writerIndex_ += len;
}

void Buffer::ensureWritableBytes(size_t len)
{
    if (writableBytes() < len)
    {
        makeSpace(len);
    }
}

void Buffer::append(const char* data, size_t len)
{
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    writerIndex_ += len;
}

void Buffer::append(const std::string& str)
{
    append(str.data(), str.size());
}

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    // 如果 Buffer 当前尾部空间足够，数据直接写入 Buffer；
    // 如果一次读到的数据更多，溢出部分进入栈上的 extrabuf，随后再 append。
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 使用 readv 一次性读取到缓冲区和额外数组中。
    // 这样可以避免频繁扩容，提高非阻塞读取性能。
    // readv 将两个不连续区域视为一块连续接收空间，只进行一次系统调用。
    const ssize_t n = ::readv(fd, vec, 2);
    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        // 数据全部落在 Buffer 原有的可写区域。
        writerIndex_ += static_cast<size_t>(n);
    }
    else
    {
        // Buffer 原有区域已填满，把 extrabuf 中的剩余数据追加进去。
        writerIndex_ = buffer_.size();
        append(extrabuf, static_cast<size_t>(n) - writable);
    }
    return n;
}

char* Buffer::begin()
{
    return &*buffer_.begin();
}

const char* Buffer::begin() const
{
    return &*buffer_.begin();
}

void Buffer::makeSpace(size_t len)
{
    // 如果已有空间不足，则判断是否可以通过移动已读数据腾出空间。
    // 若可行则移动数据，否则直接扩容。
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
        // 全部空闲空间加起来仍不够，只能扩容。
        buffer_.resize(writerIndex_ + len);
    }
    else
    {
        // 总空间足够，仅仅是未读数据位于中间；把它移动到预留区之后。
        size_t readable = readableBytes();
        std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + readable;
    }
}
