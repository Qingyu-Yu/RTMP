#pragma once

#include <unistd.h>
#include <sys/syscall.h>

namespace CurrentThread
{
    extern __thread int t_cachedTid;

    // 将当前线程 id 缓存到线程局部存储中。
    void cachedTid();

    // 返回当前线程 id。
    inline int tid()
    {
        if (__builtin_expect(t_cachedTid == 0, 0))
        {
            cachedTid();
        }
        return t_cachedTid;
    }
}