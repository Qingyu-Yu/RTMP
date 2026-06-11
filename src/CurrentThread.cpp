#include "net/CurrentThread.h"

__thread int CurrentThread::t_cachedTid = 0;

void CurrentThread::cachedTid()
{
    if (t_cachedTid == 0)
    {
        t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
    }
}
