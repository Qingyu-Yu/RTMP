#include "rtmp/stream/stream_registry.h"

#include <algorithm>

namespace rtmp::stream {

bool StreamRegistry::publish(ClientId client, const std::string& stream_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // operator[] 在首次发布时创建空 Stream；失败的第二发布者不会覆盖原值。
    auto& stream = streams_[stream_key];
    if (stream.publisher != 0 && stream.publisher != client) {
        return false;
    }
    stream.publisher = client;
    client_streams_[client] = stream_key;
    return true;
}

bool StreamRegistry::subscribe(ClientId client,
                               const std::string& stream_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 当前策略不等待未来发布者：播放不存在的流立即返回 StreamNotFound。
    const auto found = streams_.find(stream_key);
    if (found == streams_.end() || found->second.publisher == 0) {
        return false;
    }
    found->second.subscribers.insert(client);
    client_streams_[client] = stream_key;
    return true;
}

void StreamRegistry::remove(ClientId client) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 反向索引避免遍历全部流寻找连接。
    const auto assignment = client_streams_.find(client);
    if (assignment == client_streams_.end()) {
        return;
    }
    const auto found = streams_.find(assignment->second);
    if (found != streams_.end()) {
        if (found->second.publisher == client) {
            // 发布者离开后缓存失效，避免新播放器拿到过期编码参数。
            found->second.publisher = 0;
            found->second.bootstrap_messages.clear();
        }
        found->second.subscribers.erase(client);
        if (found->second.publisher == 0 &&
            found->second.subscribers.empty()) {
            streams_.erase(found);
        }
    }
    client_streams_.erase(assignment);
}

std::vector<ClientId> StreamRegistry::route(
    ClientId publisher, const std::string& stream_key,
    const protocol::Message& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = streams_.find(stream_key);
    // 防止未注册连接伪造媒体消息，只有当前发布者可以路由。
    if (found == streams_.end() || found->second.publisher != publisher) {
        return {};
    }

    if (isBootstrapMessage(message)) {
        // 每个 MessageType 只保留最新一条初始化消息：
        // metadata、AAC sequence header、AVC sequence header 各一条。
        auto& cached = found->second.bootstrap_messages;
        const auto same_type = std::find_if(
            cached.begin(), cached.end(), [&message](const auto& item) {
                return item.type == message.type;
            });
        if (same_type == cached.end()) {
            cached.push_back(message);
        } else {
            *same_type = message;
        }
    }
    // 返回订阅者快照，调用方在锁外执行网络发送。
    return {found->second.subscribers.begin(),
            found->second.subscribers.end()};
}

std::vector<protocol::Message> StreamRegistry::bootstrap(
    const std::string& stream_key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = streams_.find(stream_key);
    return found == streams_.end()
               ? std::vector<protocol::Message>{}
               : found->second.bootstrap_messages;
}

bool StreamRegistry::isBootstrapMessage(
    const protocol::Message& message) {
    // Data AMF0 通常是 @setDataFrame/onMetaData，新播放器需要先收到元数据。
    if (message.type == protocol::MessageType::kDataAmf0) {
        return true;
    }
    if (message.type == protocol::MessageType::kAudio) {
        // FLV Audio: SoundFormat 高 4 位为 10 表示 AAC；
        // AACPacketType == 0 表示 AudioSpecificConfig。
        return message.payload.size() >= 2 &&
               (message.payload[0] >> 4U) == 10 &&
               message.payload[1] == 0;
    }
    if (message.type == protocol::MessageType::kVideo) {
        // FLV Video: CodecID 低 4 位为 7 表示 AVC；
        // AVCPacketType == 0 表示 AVCDecoderConfigurationRecord。
        return message.payload.size() >= 2 &&
               (message.payload[0] & 0x0FU) == 7 &&
               message.payload[1] == 0;
    }
    return false;
}

}  // namespace rtmp::stream
