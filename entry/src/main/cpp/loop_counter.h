/*
 * LoopCounter：循环次数统计与上报（对应 T08 / T09）。
 *
 * 并发约定：
 * - 递增只发生在解码线程（一次 LoopEnd → LoopStart 跳转调一次）；
 * - 读取与重置可由其他线程调用，内部用原子变量，无需加锁；
 * - 回调在触发递增的线程上同步执行。真实设备上它就是解码线程，
 *   因此回调内不得做耗时操作；投递到 JS 线程由桥接层负责（任务 5.3）。
 *
 * 重置时机（T08）：seek 与循环点变更后必须调用 Reset()。
 */

#ifndef SEAMLESS_LOOP_LOOP_COUNTER_H
#define SEAMLESS_LOOP_LOOP_COUNTER_H

#include <atomic>
#include <cstdint>

namespace slh {

class LoopCounter {
public:
    // 参数为累计循环次数（从 1 开始）。
    using Callback = void (*)(void* userData, int64_t loopCount);

    void SetCallback(Callback callback, void* userData)
    {
        // 先存 userData 再存回调，避免回调先可见、数据后可见的窗口。
        userData_.store(userData, std::memory_order_release);
        callback_.store(callback, std::memory_order_release);
    }

    int64_t Count() const
    {
        return count_.load(std::memory_order_acquire);
    }

    void Reset()
    {
        count_.store(0, std::memory_order_release);
    }

    // 完成一次循环跳转：计数加一，并把累计值回调给业务层。
    int64_t IncrementAndNotify()
    {
        const int64_t next = count_.fetch_add(1, std::memory_order_acq_rel) + 1;
        const Callback callback = callback_.load(std::memory_order_acquire);
        if (callback != nullptr) {
            callback(userData_.load(std::memory_order_acquire), next);
        }
        return next;
    }

private:
    std::atomic<int64_t> count_{0};
    std::atomic<Callback> callback_{nullptr};
    std::atomic<void*> userData_{nullptr};
};

} // namespace slh

#endif // SEAMLESS_LOOP_LOOP_COUNTER_H
