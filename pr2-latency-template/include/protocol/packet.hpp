#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// type:u8, sequence:u16, payloadSize:u16, version:u16, requiresAck:u8.
// Многобайтовые поля передаются в big-endian.
constexpr std::uint16_t kProtocolVersion = 2;
constexpr std::size_t kHeaderSize = 8;
enum class PacketType : std::uint8_t { Ping=1, Pong=2, Movement=3, Shoot=4, Ack=5 };
struct PacketHeader {
    PacketType type;
    std::uint16_t sequenceNumber;
    std::uint16_t payloadSize;
    std::uint16_t protocolVersion;
    std::uint8_t requiresAck = 0;
};
struct PingPayload { std::uint64_t clientSendTimeUs; };
struct PongPayload {
    std::uint64_t clientSendTimeUs;
    std::uint64_t serverReceiveTimeUs;
    std::uint64_t serverSendTimeUs;
};
struct AckPayload { std::uint16_t acknowledgedSequence; };
void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value);
void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value);
std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end);
std::vector<std::uint8_t> SerializePing(std::uint16_t sequence, std::uint64_t sentUs);
std::optional<PingPayload> ParsePing(const std::uint8_t*, std::size_t, PacketHeader&);
std::vector<std::uint8_t> SerializePong(std::uint16_t sequence, const PongPayload&);
std::optional<PongPayload> ParsePong(const std::uint8_t*, std::size_t, PacketHeader&);
std::vector<std::uint8_t> SerializeShoot(std::uint16_t sequence);
std::vector<std::uint8_t> SerializeAck(std::uint16_t sequence, std::uint16_t acknowledgedSequence);
std::optional<AckPayload> ParseAck(const std::uint8_t*, std::size_t, PacketHeader&);
std::optional<PacketHeader> ParseHeader(const std::uint8_t*, std::size_t);
const char* PacketError(const std::uint8_t*, std::size_t);
