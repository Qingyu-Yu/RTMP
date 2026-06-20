#include "rtmp/amf/amf0.h"
#include "rtmp/chunk/chunk_codec.h"
#include "rtmp/session/rtmp_session.h"
#include "rtmp/stream/stream_registry.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

rtmp::protocol::Message command(
    std::uint32_t stream_id, const std::vector<rtmp::amf::Value>& values) {
    rtmp::protocol::Message message;
    message.chunk_stream_id = 3;
    message.stream_id = stream_id;
    message.type = rtmp::protocol::MessageType::kCommandAmf0;
    message.payload = rtmp::amf::encode(values);
    return message;
}

bool testAmfRoundTrip() {
    const std::vector<rtmp::amf::Value> input{
        rtmp::amf::Value("connect"),
        rtmp::amf::Value(1.0),
        rtmp::amf::Value::object({
            {"app", rtmp::amf::Value("live")},
            {"enabled", rtmp::amf::Value(true)},
        }),
    };
    std::vector<rtmp::amf::Value> output;
    std::string error;
    if (!expect(rtmp::amf::decode(rtmp::amf::encode(input), output, &error),
                "AMF0 values should decode after encoding") ||
        !expect(output.size() == 3, "AMF0 value count should be preserved")) {
        return false;
    }
    const auto* app = output[2].find("app");
    return expect(output[0].string() == "connect",
                  "AMF0 string should be preserved") &&
           expect(output[1].number() == 1.0,
                  "AMF0 number should be preserved") &&
           expect(app != nullptr && app->string() == "live",
                  "AMF0 object property should be preserved");
}

bool testChunkFragmentationAndExtendedTimestamp() {
    rtmp::protocol::Message input;
    input.timestamp = 0x01020304U;
    input.stream_id = 7;
    input.chunk_stream_id = 6;
    input.type = rtmp::protocol::MessageType::kVideo;
    input.payload.resize(600);
    for (std::size_t i = 0; i < input.payload.size(); ++i) {
        input.payload[i] = static_cast<std::uint8_t>(i % 251U);
    }

    const std::string encoded = rtmp::chunk::ChunkEncoder::encode(input, 128);
    rtmp::chunk::ChunkDecoder decoder;
    std::vector<rtmp::protocol::Message> messages;
    for (std::size_t offset = 0; offset < encoded.size(); offset += 17) {
        const std::size_t size =
            std::min<std::size_t>(17, encoded.size() - offset);
        auto result = decoder.consume(encoded.data() + offset, size);
        if (!expect(result.ok, "fragmented chunks should decode")) {
            return false;
        }
        messages.insert(messages.end(), result.messages.begin(),
                        result.messages.end());
    }
    return expect(messages.size() == 1,
                  "chunk decoder should emit one complete message") &&
           expect(messages[0].timestamp == input.timestamp,
                  "extended timestamp should be preserved") &&
           expect(messages[0].stream_id == input.stream_id,
                  "message stream id should be preserved") &&
           expect(messages[0].payload == input.payload,
                  "chunk payload should be preserved");
}

bool testSessionCommands() {
    rtmp::session::RtmpSession session;
    const auto connect = command(
        0, {rtmp::amf::Value("connect"), rtmp::amf::Value(1.0),
            rtmp::amf::Value::object(
                {{"app", rtmp::amf::Value("live")}})});
    const std::string connect_bytes =
        rtmp::chunk::ChunkEncoder::encode(connect);
    const auto connect_result =
        session.consume(connect_bytes.data(), connect_bytes.size());
    if (!expect(connect_result.ok && connect_result.outbound.size() == 4,
                "connect should produce control messages and _result")) {
        return false;
    }

    const auto publish = command(
        1, {rtmp::amf::Value("publish"), rtmp::amf::Value(0.0),
            rtmp::amf::Value{}, rtmp::amf::Value("camera")});
    const std::string publish_bytes =
        rtmp::chunk::ChunkEncoder::encode(publish);
    const auto publish_result =
        session.consume(publish_bytes.data(), publish_bytes.size());
    return expect(publish_result.ok && publish_result.events.size() == 1,
                  "publish should emit one routing event") &&
           expect(publish_result.events[0].stream_key == "live/camera",
                  "publish event should contain normalized stream key");
}

bool testStreamRegistry() {
    rtmp::stream::StreamRegistry streams;
    if (!expect(streams.publish(1, "live/camera"),
                "first publisher should own stream") ||
        !expect(!streams.publish(2, "live/camera"),
                "second publisher should be rejected") ||
        !expect(streams.subscribe(3, "live/camera"),
                "subscriber should join an existing stream")) {
        return false;
    }

    rtmp::protocol::Message sequence_header;
    sequence_header.type = rtmp::protocol::MessageType::kVideo;
    sequence_header.payload = {0x17, 0x00};
    const auto recipients =
        streams.route(1, "live/camera", sequence_header);
    return expect(recipients.size() == 1 && recipients[0] == 3,
                  "publisher media should route to subscribers") &&
           expect(streams.bootstrap("live/camera").size() == 1,
                  "video sequence header should be cached");
}

}  // namespace

int main() {
    if (!testAmfRoundTrip() ||
        !testChunkFragmentationAndExtendedTimestamp() ||
        !testSessionCommands() ||
        !testStreamRegistry()) {
        return 1;
    }
    std::cout << "All RTMP protocol tests passed\n";
    return 0;
}
