#pragma once

#include "rtmp/protocol/message.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtmp::stream {

// 网络适配层生成的进程内连接标识；不拥有连接对象。
using ClientId = std::uintptr_t;

/**
 * 线程安全的发布/订阅流注册表。
 *
 * StreamRegistry 只维护领域关系，不依赖 TcpConnection。网络层拿到订阅者
 * ClientId 后自行查找连接并发送数据，因此锁内不会执行 socket I/O。
 */
class StreamRegistry {
public:
    /**
     * 注册发布者。
     *
     * 同一 stream key 同时只允许一个发布者；重复注册同一 client 是幂等的。
     */
    [[nodiscard]] bool publish(ClientId client, const std::string& stream_key);

    // 仅当目标流已有发布者时允许订阅。
    [[nodiscard]] bool subscribe(ClientId client,
                                 const std::string& stream_key);

    // 从发布或订阅关系中移除连接；可安全重复调用。
    void remove(ClientId client);

    /**
     * 校验发布者、更新初始化消息缓存并返回订阅者快照。
     *
     * 返回副本是为了让上层在释放 mutex 后执行跨线程网络发送。
     */
    [[nodiscard]] std::vector<ClientId> route(
        ClientId publisher, const std::string& stream_key,
        const protocol::Message& message);

    // 返回新播放器开始解码所需的 metadata/AAC/AVC 初始化消息副本。
    [[nodiscard]] std::vector<protocol::Message> bootstrap(
        const std::string& stream_key) const;

private:
    // 一个逻辑 stream key 的全部路由状态。
    struct Stream {
        // 0 表示当前没有发布者。
        ClientId publisher{0};

        // 使用集合避免同一连接重复订阅。
        std::unordered_set<ClientId> subscribers;

        // 每种媒体类型保留最近一条初始化消息。
        std::vector<protocol::Message> bootstrap_messages;
    };

    // 判断消息是否应在新订阅者加入时立即重放。
    static bool isBootstrapMessage(const protocol::Message& message);

    // TcpServer 可在多个 I/O 线程调用注册表，因此所有映射共享同一把锁。
    mutable std::mutex mutex_;

    // stream key -> 发布/订阅状态。
    std::unordered_map<std::string, Stream> streams_;

    // client -> stream key 的反向索引，用于 O(1) 连接清理。
    std::unordered_map<ClientId, std::string> client_streams_;
};

}  // namespace rtmp::stream
