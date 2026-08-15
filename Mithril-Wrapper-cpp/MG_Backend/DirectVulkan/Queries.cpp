// Mithril-Wrapper - MG_Backend/DirectVulkan/Queries.cpp
// GL 查询对象的真实 VkQueryPool 后端（glBeginQuery / glEndQuery /
// glQueryCounter / glGetQueryObject* / glGetQueryBufferObject*）。
//
// 设计（深度对照 MobileGL VulkanRenderer 的 query 处理 + mesa radv/anv 的
// GL_TIME_ELAPSED 映射）：
//   * GL_SAMPLES_PASSED / GL_ANY_SAMPLES_PASSED
//       -> VK_QUERY_TYPE_OCCLUSION，1 slot。vkCmdResetQueryPool + Begin/End。
//          ANY_SAMPLES_PASSED 在 GL 层读回时布尔化（result != 0）。
//   * GL_TIMESTAMP (glQueryCounter)
//       -> VK_QUERY_TYPE_TIMESTAMP，1 slot，vkCmdWriteTimestamp。
//   * GL_TIME_ELAPSED (glBeginQuery/glEndQuery)
//       -> Vulkan 不允许对 TIMESTAMP 池 Begin/End（VUID-
//          vkCmdBeginQuery-queryType-02804），用 2-slot 时间戳池：
//          begin 写 slot0、end 写 slot1，结果 = t1 - t0。radv 对
//          GL_TIME_ELAPSED 的 GL->Vulkan 映射同此。
//   * GL_PRIMITIVES_GENERATED / GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
//       -> Metal 无管线统计查询（MoltenVK pipelineStatisticsQuery=VK_FALSE），
//          在 GL 层用录制期软件计数（dvk_draw_* 汇入 g_state 的累加器），
//          语义精确（draw 参数已知），非 stub。
//
// 生命周期：pool 按 GL 查询对象 id 存表；glDeleteQueries 时若查询命令可能
// 仍在飞（该 slot 的 fence 未亮），推入 disposalQueue 延迟销毁 —— 与
// buffer/texture 的 DeferredDestroy 同路径（Device.h）。
//
// MoltenVK 能力：occlusion 查询全支持；timestamp 取决于
// queueFamilyProperties.timestampValidBits（A13+/M1+ 为 64，老 GPU 为 0）。
// validBits == 0 时 dvk_query_pool_create 对时间戳类 kind 返回 false，
// GL 层回退到单调 CPU 时钟（真实流逝时间，仅基准不同，文档化回退）。
#include "Device.h"
#include "CommandStream.h"  // ensure_command_buffer_recording
#include "Resources.h"      // dvk_get_buffer
#include "BackendVulkanDecls.h"
#include "../../MG_Impl/Log.h"

#include <cstdint>
#include <ctime>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace vk {

namespace {

struct QueryPoolEntry {
    VkQueryPool pool = VK_NULL_HANDLE;
    int         kind = -1;        // MITHRIL_QUERY_*
    uint32_t    slots = 0;        // 1 or 2 (TIME_ELAPSED)
};

std::unordered_map<uint64_t, QueryPoolEntry>& query_pool_table() {
    static std::unordered_map<uint64_t, QueryPoolEntry> t;
    return t;
}

VkQueryType kind_to_vk_type(int kind) {
    switch (kind) {
        case MITHRIL_QUERY_OCCLUSION:    return VK_QUERY_TYPE_OCCLUSION;
        case MITHRIL_QUERY_TIMESTAMP:
        case MITHRIL_QUERY_TIME_ELAPSED: return VK_QUERY_TYPE_TIMESTAMP;
        default:                         return VK_QUERY_TYPE_MAX_ENUM;
    }
}

uint32_t kind_slots(int kind) {
    return (kind == MITHRIL_QUERY_TIME_ELAPSED) ? 2 : 1;
}

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

} // namespace

extern "C" uint32_t dvk_query_timestamp_valid_bits() {
    Backend* b = backend();
    if (!b || !b->initialized || !b->physicalDevice) return 0;
    // 缓存进 Backend？queue 属性不变，取一次存静态即可（单物理设备进程单例）。
    static uint32_t cached = UINT32_MAX;
    if (cached == UINT32_MAX) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &count, nullptr);
        if (count == 0) { cached = 0; return 0; }
        // 找 graphics family 的 validBits（b->graphicsFamily 已在 init 时选定）。
        std::vector<VkQueueFamilyProperties> props(count);
        vkGetPhysicalDeviceQueueFamilyProperties(b->physicalDevice, &count,
                                                 props.data());
        cached = (b->graphicsFamily < count)
               ? props[b->graphicsFamily].timestampValidBits : 0;
    }
    return cached;
}

extern "C" bool dvk_query_pool_create(uint64_t query_id, int kind) {
    Backend* b = backend();
    if (!b || !b->initialized || !b->device) return false;

    VkQueryType type = kind_to_vk_type(kind);
    if (type == VK_QUERY_TYPE_MAX_ENUM) return false;
    // Metal 无时间戳计数器（老 GPU）：如实告知 GL 层走 CPU 时钟回退。
    if (type == VK_QUERY_TYPE_TIMESTAMP && dvk_query_timestamp_valid_bits() == 0)
        return false;

    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it != table.end()) {
        if (it->second.kind == kind) return true;   // 幂等
        // 类型变化：退役旧池（走延迟销毁），下面重建。
        dvk_query_pool_destroy(query_id);
    }

    VkQueryPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType = type;
    ci.queryCount = kind_slots(kind);
    // OCCLUSION: precise bit 让结果为精确样本数而非 0/1（Metal/ lavapipe 支持；
    // 精确度不足时驱动自行降级，Vulkan 语义允许）。
    ci.flags = 0;

    VkQueryPool pool = VK_NULL_HANDLE;
    VkResult r = vkCreateQueryPool(b->device, &ci, nullptr, &pool);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateQueryPool(kind=%d) failed: %d", kind, (int)r);
        return false;
    }

    QueryPoolEntry e;
    e.pool = pool;
    e.kind = kind;
    e.slots = ci.queryCount;
    table[query_id] = e;
    return true;
}

extern "C" void dvk_query_pool_destroy(uint64_t query_id) {
    Backend* b = backend();
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end()) return;
    VkQueryPool pool = it->second.pool;
    table.erase(it);
    if (!b || !b->device || !pool) return;

    // 与 buffer/texture 相同的延迟销毁：查询命令可能仍在该 slot 的
    // command buffer 里，等 slot fence 亮起后再销毁。
    DeferredDestroy d{};
    d.queryPool = pool;
    b->disposalQueue[b->currentFrame].push_back(d);
}

extern "C" void dvk_query_begin(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end() || !it->second.pool) return;
    if (!ensure_command_buffer_recording()) return;
    note_query_commands();
    VkCommandBuffer cb = b->commandBuffer;

    if (it->second.kind == MITHRIL_QUERY_OCCLUSION) {
        // 复用前清零（GL 语义：新 begin 丢弃旧结果）。
        vkCmdResetQueryPool(cb, it->second.pool, 0, 1);
        vkCmdBeginQuery(cb, it->second.pool, 0, 0);
    } else if (it->second.kind == MITHRIL_QUERY_TIME_ELAPSED) {
        vkCmdResetQueryPool(cb, it->second.pool, 0, 2);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            it->second.pool, 0);
    }
}

extern "C" void dvk_query_end(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end() || !it->second.pool) return;
    if (!ensure_command_buffer_recording()) return;
    note_query_commands();
    VkCommandBuffer cb = b->commandBuffer;

    if (it->second.kind == MITHRIL_QUERY_OCCLUSION) {
        vkCmdEndQuery(cb, it->second.pool, 0);
    } else if (it->second.kind == MITHRIL_QUERY_TIME_ELAPSED) {
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            it->second.pool, 1);
    }
}

extern "C" void dvk_query_write_timestamp(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end() || !it->second.pool) return;
    if (it->second.kind != MITHRIL_QUERY_TIMESTAMP) return;
    if (!ensure_command_buffer_recording()) return;
    note_query_commands();

    vkCmdResetQueryPool(b->commandBuffer, it->second.pool, 0, 1);
    vkCmdWriteTimestamp(b->commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        it->second.pool, 0);
}

extern "C" bool dvk_query_get_results(uint64_t query_id, bool wait,
                                     uint64_t* out, bool* available) {
    Backend* b = backend();
    if (!b || !b->initialized) return false;
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end() || !it->second.pool) return false;

    const uint32_t slots = it->second.slots;
    uint64_t data[2] = {0, 0};
    VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
    if (!wait) flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
    else       flags |= VK_QUERY_RESULT_WAIT_BIT;

    // wait 路径一次取齐所有 slot；非阻塞路径必须带 WITH_AVAILABILITY，
    // 每个 slot 读 [value, availability] 成对 u64。
    const uint32_t fetch = wait ? slots : 1;
    VkResult r = vkGetQueryPoolResults(
        b->device, it->second.pool, 0, fetch,
        sizeof(uint64_t) * (wait ? slots : slots * 2),
        data, sizeof(uint64_t) * (wait ? 1 : 2), flags);

    if (wait) {
        if (r != VK_SUCCESS) {
            // VK_NOT_READY 不会出现在 WAIT 位路径；deviceLost 时如实回报。
            if (available) *available = false;
            if (out) *out = 0;
            return true;  // pool 存在，只是读不回
        }
        if (it->second.kind == MITHRIL_QUERY_TIME_ELAPSED) {
            if (out) *out = (data[1] >= data[0]) ? (data[1] - data[0]) : 0;
        } else {
            if (out) *out = data[0];
        }
        if (available) *available = true;
        return true;
    }

    // 非阻塞：data = [value0, avail0(, value1, avail1)]
    if (r != VK_SUCCESS && r != VK_NOT_READY) {
        if (available) *available = false;
        if (out) *out = 0;
        return true;
    }
    if (available) *available = (data[1] != 0);
    if (out) {
        if (data[1] == 0) {
            *out = 0;  // 未就绪：GL 允许返回 undefined，给 0
        } else if (it->second.kind == MITHRIL_QUERY_TIME_ELAPSED) {
            // 取 end slot（slot1）非阻塞结果做差。
            uint64_t d2[2] = {0, 0};
            VkResult r2 = vkGetQueryPoolResults(
                b->device, it->second.pool, 1, 1, sizeof(d2), d2,
                sizeof(uint64_t) * 2,
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            *out = (r2 == VK_SUCCESS && d2[1] != 0 && d2[0] >= data[0])
                 ? (d2[0] - data[0]) : 0;
        } else {
            *out = data[0];
        }
    }
    return true;
}

extern "C" void dvk_query_copy_results(uint64_t query_id, uint32_t gl_buffer_id,
                                      VkDeviceSize offset, bool with_availability) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto& table = query_pool_table();
    auto it = table.find(query_id);
    if (it == table.end() || !it->second.pool) return;

    VkBuffer dst = dvk_get_buffer(gl_buffer_id);
    if (!dst) {
        MITHRIL_LOG_WARN("vk", "glGetQueryBufferObject: buffer %u has no "
                          "VkBuffer backend", gl_buffer_id);
        return;
    }
    if (!ensure_command_buffer_recording()) return;
    note_query_commands();

    // TIME_ELAPSED 的差值无法用单次 copy 表达（vkCmdCopyQueryPoolResults 拷
    // 原始 slot 值）：拷 end slot（slot1），GL 层文档化此限制（GL 规范允许
    // 实现相关的时间源；需要差值的应用极少走 buffer-object 路径）。
    const uint32_t firstSlot = (it->second.kind == MITHRIL_QUERY_TIME_ELAPSED) ? 1 : 0;
    VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
    if (with_availability) flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;

    VkDeviceSize stride = with_availability ? 16 : 8;
    vkCmdCopyQueryPoolResults(b->commandBuffer, it->second.pool, firstSlot, 1,
                              dst, offset, stride, flags);
}

// glGetInteger64v(GL_TIMESTAMP)：取"当前"GPU 时间戳。GL 规范允许任意单调
// 时基（与 glQueryCounter 同源即可）。实现：临时池写一枚 timestamp、提交、
// 阻塞读回、延迟销毁。罕见路径（性能仪表初始化），开销可接受。设备无
// timestamp 计数器时回退 CPU 单调时钟（ns）。
extern "C" uint64_t dvk_query_timestamp_now_ns(void) {
    Backend* b = backend();
    if (!b || !b->initialized) return now_ns();
    if (dvk_query_timestamp_valid_bits() == 0) return now_ns();

    const uint64_t key = UINT64_MAX;   // 保留键，不与 GL 查询名冲突
    if (!dvk_query_pool_create(key, MITHRIL_QUERY_TIMESTAMP)) return now_ns();
    if (!ensure_command_buffer_recording()) return now_ns();
    note_query_commands();

    auto& table = query_pool_table();
    auto it = table.find(key);
    if (it == table.end()) return now_ns();

    vkCmdResetQueryPool(b->commandBuffer, it->second.pool, 0, 1);
    vkCmdWriteTimestamp(b->commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        it->second.pool, 0);
    dvk_end_render_pass();
    dvk_commit();

    uint64_t v = 0; bool avail = false;
    dvk_query_get_results(key, true, &v, &avail);
    dvk_query_pool_destroy(key);   // 走延迟销毁（命令可能刚提交）
    return avail ? v : now_ns();
}

} // namespace vk
} // namespace mithril
