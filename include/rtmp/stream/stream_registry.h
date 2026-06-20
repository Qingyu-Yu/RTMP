#pragma once

#include "rtmp/protocol/message.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rtmp::stream {

using ClientId = std::uintptr_t;

class StreamRegistry {
public:
    [[nodiscard]] bool publish(ClientId client, const std::string& stream_key);
    [[nodiscard]] bool subscribe(ClientId client,
                                 const std::string& stream_key);
    void remove(ClientId client);

    [[nodiscard]] std::vector<ClientId> route(
        ClientId publisher, const std::string& stream_key,
        const protocol::Message& message);
    [[nodiscard]] std::vector<protocol::Message> bootstrap(
        const std::string& stream_key) const;

private:
    struct Stream {
        ClientId publisher{0};
        std::unordered_set<ClientId> subscribers;
        std::vector<protocol::Message> bootstrap_messages;
    };

    static bool isBootstrapMessage(const protocol::Message& message);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Stream> streams_;
    std::unordered_map<ClientId, std::string> client_streams_;
};

}  // namespace rtmp::stream
