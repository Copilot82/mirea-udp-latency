#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Формат заголовка пакета:
// [ packetType : 1 байт ][ sequenceNumber : 2 байта ][ payloadSize : 2 байта ][ protocolVersion : 2 байта ]
// Далее следует полезная нагрузка размером payloadSize байт.
// Все многобайтовые поля передаются в сетевом порядке байт (big-endian).

constexpr std::uint16_t kProtocolVersion = 1;

enum class PacketType : std::uint8_t {
    Ping = 1,
    Pong = 2,
    Movement = 3, // из ПР №1
    Shoot = 4     // из ПР №1
};

struct PacketHeader {
    PacketType type;
    std::uint16_t sequenceNumber;
    std::uint16_t payloadSize;
    std::uint16_t protocolVersion;
};

struct PingPayload {
    std::uint64_t clientSendTimeUs;
};

struct PongPayload {
    std::uint64_t clientSendTimeUs;
    std::uint64_t serverReceiveTimeUs;
    std::uint64_t serverSendTimeUs;
};

// --- Низкоуровневые помощники сериализации ---
// Явные операции big-endian; реализации в src/protocol.cpp.

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value);
void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value);

std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end);

// --- PING ---

// Ожидаемый размер результата: 7 (заголовок) + 8 (payload) = 15 байт.
std::vector<std::uint8_t> SerializePing(std::uint16_t sequenceNumber, std::uint64_t clientSendTimeUs);

// - совпадение типа пакета,
// - совпадение protocolVersion,
// - совпадение payloadSize фактическому размеру данных,
// - отсутствие выхода за границы буфера.
// При любой ошибке функция должна возвращать std::nullopt, а не бросать исключение
// и не читать за пределами буфера.
std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader);

// --- PONG ---

std::vector<std::uint8_t> SerializePong(std::uint16_t sequenceNumber, const PongPayload& payload);

std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader);

// Validates the complete datagram, including type-specific payload size.
std::optional<PacketHeader> ParseHeader(const std::uint8_t* data, std::size_t size);
// Returns nullptr on success, otherwise a stable diagnostic reason.
const char* PacketError(const std::uint8_t* data, std::size_t size);
