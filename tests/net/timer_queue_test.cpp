#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "net/EventLoop.h"

int main()
{
    EventLoop loop;
    std::atomic<int> repeated{0};
    bool oneShotCalled = false;
    bool canceledCalled = false;

    loop.runAfter(0.02, [&oneShotCalled] {
        oneShotCalled = true;
    });

    const TimerId canceled = loop.runAfter(0.04, [&canceledCalled] {
        canceledCalled = true;
    });
    loop.cancel(canceled);

    TimerId repeating;
    repeating = loop.runEvery(0.01, [&] {
        if (++repeated == 3)
        {
            loop.cancel(repeating);
            loop.quit();
        }
    });

    std::thread crossThread([&loop] {
        loop.runAfter(0.5, [&loop] {
            loop.quit();
        });
    });
    crossThread.join();

    loop.loop();

    if (!oneShotCalled || canceledCalled || repeated != 3)
    {
        std::cerr << "timer queue behavior mismatch: oneShot="
                  << oneShotCalled
                  << " canceled="
                  << canceledCalled
                  << " repeated="
                  << repeated
                  << '\n';
        return 1;
    }
    return 0;
}
