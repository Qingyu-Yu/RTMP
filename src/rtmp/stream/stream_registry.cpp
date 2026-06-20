#include "rtmp/stream/stream_registry.h"

#include <algorithm>

namespace rtmp::stream {

bool StreamRegistry::publish(ClientId client, const std::string& stream_key) {
    std::lock_guard<std::mutex> lock(mutex_);
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
    const auto assignment = client_streams_.find(client);
    if (assignment == client_streams_.end()) {
        return;
    }
    const auto found = streams_.find(assignment->second);
    if (found != streams_.end()) {
        if (found->second.publisher == client) {
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
    if (found == streams_.end() || found->second.publisher != publisher) {
        return {};
    }

    if (isBootstrapMessage(message)) {
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
    if (message.type == protocol::MessageType::kDataAmf0) {
        return true;
    }
    if (message.type == protocol::MessageType::kAudio) {
        return message.payload.size() >= 2 &&
               (message.payload[0] >> 4U) == 10 &&
               message.payload[1] == 0;
    }
    if (message.type == protocol::MessageType::kVideo) {
        return message.payload.size() >= 2 &&
               (message.payload[0] & 0x0FU) == 7 &&
               message.payload[1] == 0;
    }
    return false;
}

}  // namespace rtmp::stream
