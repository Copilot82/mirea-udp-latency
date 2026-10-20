#include "protocol/packet.hpp"

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end) {
    if (!cursor || !end || end - cursor < 2) return std::nullopt;
    const auto value = static_cast<std::uint16_t>((std::uint16_t(cursor[0]) << 8) | cursor[1]);
    cursor += 2;
    return value;
}

std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end) {
    if (!cursor || !end || end - cursor < 8) return std::nullopt;
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | *cursor++;
    return value;
}

const char* PacketError(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 7) return "short_header";
    std::uint16_t expected = 0;
    switch (static_cast<PacketType>(data[0])) {
        case PacketType::Ping: expected = 8; break;
        case PacketType::Pong: expected = 24; break;
        case PacketType::Movement: expected = 8; break;
        case PacketType::Shoot: expected = 0; break;
        default: return "unknown_type";
    }
    const auto* cursor = data + 1;
    const auto* end = data + 7; // Header only; payload not accessed until validated.
    ReadU16(cursor, end);
    const auto length = *ReadU16(cursor, end);
    const auto version = *ReadU16(cursor, end);
    if (version != kProtocolVersion) return "unsupported_version";
    if (length != expected || size != std::size_t(length) + 7) return "payload_size_mismatch";
    return nullptr;
}

std::optional<PacketHeader> ParseHeader(const std::uint8_t* data, std::size_t size) {
    if (PacketError(data, size)) return std::nullopt;
    const auto* cursor = data + 1;
    const auto* end = data + 7;
    const auto seq = *ReadU16(cursor, end);
    const auto length = *ReadU16(cursor, end);
    const auto version = *ReadU16(cursor, end);
    return PacketHeader{static_cast<PacketType>(data[0]), seq, length, version};
}

namespace {
std::vector<std::uint8_t> Header(PacketType type, std::uint16_t seq, std::uint16_t length) {
    std::vector<std::uint8_t> out;
    out.reserve(7 + length);
    out.push_back(static_cast<std::uint8_t>(type));
    WriteU16(out, seq);
    WriteU16(out, length);
    WriteU16(out, kProtocolVersion);
    return out;
}
}

std::vector<std::uint8_t> SerializePing(std::uint16_t seq, std::uint64_t sent) {
    auto out = Header(PacketType::Ping, seq, 8);
    WriteU64(out, sent);
    return out;
}

std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader) {
    const auto h = ParseHeader(data, size);
    if (!h || h->type != PacketType::Ping) return std::nullopt;
    const auto* cursor = data + 7;
    const auto sent = ReadU64(cursor, data + size);
    if (!sent) return std::nullopt;
    outHeader = *h;
    return PingPayload{*sent};
}

std::vector<std::uint8_t> SerializePong(std::uint16_t seq, const PongPayload& p) {
    auto out = Header(PacketType::Pong, seq, 24);
    WriteU64(out, p.clientSendTimeUs);
    WriteU64(out, p.serverReceiveTimeUs);
    WriteU64(out, p.serverSendTimeUs);
    return out;
}

std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader) {
    const auto h = ParseHeader(data, size);
    if (!h || h->type != PacketType::Pong) return std::nullopt;
    const auto* cursor = data + 7;
    const auto* end = data + size;
    const auto sent = ReadU64(cursor, end);
    const auto received = ReadU64(cursor, end);
    const auto replied = ReadU64(cursor, end);
    if (!sent || !received || !replied || *replied < *received) return std::nullopt;
    outHeader = *h;
    return PongPayload{*sent, *received, *replied};
}
