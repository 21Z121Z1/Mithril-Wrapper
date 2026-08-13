// Mithril-Wrapper - MG_Backend/DirectVulkan/LogRing.h
// 线程安全的环形资源操作日志。
//
// 用途：真机上 GPU page fault（kIOGPUCommandBufferCallbackErrorPageFault）
// 发生前，Mithril 的诊断日志往往是空的——fault 是 GPU 执行期的静默崩溃，
// 第一次可见的错误是后续的 vkQueueSubmit/VK_ERROR_DEVICE_LOST。本环形日志
// 记录最近 N 次资源操作（纹理/缓冲创建与销毁、mipmap 生成、FBO 切换、
// 描述符绑定等），在 submit 失败 / device lost 时整体 dump，让 fault 前
// 最后一次资源操作现形，从而定位「采样了已释放的 view / 越界 staging」等
// 根因。
//
// 纯头文件实现（无 .cpp，不改 CMake），所有打点调用都低于 WARN 日志的
// 开销量级（一次 vsnprintf）。
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <chrono>

namespace mithril {
namespace vk {

struct LogRing {
    struct Entry {
        uint64_t tick;   // 进程单调毫秒（相对 0，用于 fault 前后时序）
        char msg[192];
    };
    // 256 条在 draw 密集时只覆盖 ~47ms（fault 帧信息被覆盖，导致多轮无法
    // 定位）。扩容到 4096：每帧约 200-400 条资源记录，可回溯 fault 前
    // 10-20 帧，覆盖"主菜单首帧 → fault"的完整 draw 序列。
    static constexpr int kCap = 4096;

    Entry  buf[kCap];
    int    head = 0;
    bool   filled = false;
    std::mutex mu;

    void push(const char* fmt, ...) {
        Entry e;
        e.tick = now_ms();
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e.msg, sizeof(e.msg), fmt, ap);
        va_end(ap);
        std::lock_guard<std::mutex> lk(mu);
        buf[head] = e;
        head = (head + 1) % kCap;
        if (head == 0) filled = true;
    }

    // 从最旧到最新打印全部条目。fault 根因通常是「dump 中最后几条」。
    void dump(const char* reason) {
        std::lock_guard<std::mutex> lk(mu);
        const int n = filled ? kCap : head;
        const int start = filled ? head : 0;
        fprintf(stderr, "=== LogRing dump (%s): %d entries ===\n", reason, n);
        for (int i = 0; i < n; ++i) {
            const Entry& e = buf[(start + i) % kCap];
            fprintf(stderr, "  [t+%llums] %s\n",
                    (unsigned long long)e.tick, e.msg);
        }
        fflush(stderr);
    }

    static uint64_t now_ms() {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count();
    }
};

inline LogRing& log_ring() {
    static LogRing r;
    return r;
}

} // namespace vk
} // namespace mithril

// 打点宏：LOG_RESOURCE("tex created name=%u ...", name, ...)
#define LOG_RESOURCE(...) ::mithril::vk::log_ring().push(__VA_ARGS__)
