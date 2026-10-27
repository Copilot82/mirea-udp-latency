#include <catch2/catch_test_macros.hpp>
#include "protocol/packet.hpp"
#include <limits>

TEST_CASE("PING is exactly the big endian wire fixture") {
    const std::vector<std::uint8_t> expected{
        1, 0x12, 0x34, 0, 8, 0, 2, 0, 1, 2, 3, 4, 5, 6, 7, 8};
    REQUIRE(SerializePing(0x1234, 0x0102030405060708ULL) == expected);
    PacketHeader h{};
    auto p = ParsePing(expected.data(), expected.size(), h);
    REQUIRE(p);
    CHECK(h.sequenceNumber == 0x1234);
    CHECK(h.payloadSize == 8);
    CHECK(p->clientSendTimeUs == 0x0102030405060708ULL);
}

TEST_CASE("PONG preserves all fields without platform padding") {
    const std::vector<std::uint8_t> expected{
        2, 0xff, 0xff, 0, 24, 0, 2, 0,
        0, 0, 0, 0, 0, 0, 0, 7,
        0, 0, 0, 0, 0, 0, 0, 8,
        0, 0, 0, 0, 0, 0, 0, 9};
    REQUIRE(SerializePong(65535, {7, 8, 9}) == expected);
    PacketHeader h{};
    auto p = ParsePong(expected.data(), expected.size(), h);
    REQUIRE(p);
    CHECK(h.sequenceNumber == 65535);
    CHECK(p->clientSendTimeUs == 7);
    CHECK(p->serverReceiveTimeUs == 8);
    CHECK(p->serverSendTimeUs == 9);
}

TEST_CASE("Every truncated prefix is rejected without modifying the output header") {
    const auto ping = SerializePing(1, 123);
    const auto pong = SerializePong(1, {123, 234, 345});
    REQUIRE(ping.size() == 16);
    REQUIRE(pong.size() == 32);
    for (std::size_t n = 0; n < ping.size(); ++n) {
        PacketHeader h{PacketType::Shoot, 777, 0, 1};
        CHECK_FALSE(ParsePing(ping.data(), n, h));
        CHECK(h.sequenceNumber == 777);
    }
    for (std::size_t n = 0; n < pong.size(); ++n) {
        PacketHeader h{};
        CHECK_FALSE(ParsePong(pong.data(), n, h));
    }
}

TEST_CASE("Wrong type version length and trailing bytes are rejected") {
    for (bool isPong : {false, true}) {
        const auto valid = isPong ? SerializePong(1, {2, 3, 4}) : SerializePing(1, 2);
        REQUIRE_FALSE(valid.empty());
        for (std::size_t field : {0u, 3u, 4u, 5u, 6u, 7u}) {
            auto bytes = valid;
            bytes[field] = 255;
            PacketHeader h{};
            CHECK_FALSE((isPong ? ParsePong(bytes.data(), bytes.size(), h).has_value()
                               : ParsePing(bytes.data(), bytes.size(), h).has_value()));
        }
        auto bytes = valid;
        bytes.push_back(0);
        PacketHeader h{};
        CHECK_FALSE((isPong ? ParsePong(bytes.data(), bytes.size(), h).has_value()
                           : ParsePing(bytes.data(), bytes.size(), h).has_value()));
    }
    PacketHeader h{};
    CHECK_FALSE(ParsePing(nullptr, 15, h));
    CHECK_FALSE(ParsePong(nullptr, 31, h));
}

TEST_CASE("Integer helpers are big endian and failed reads do not advance") {
    std::vector<std::uint8_t> bytes;
    WriteU16(bytes, 0xabcd);
    WriteU64(bytes, std::numeric_limits<std::uint64_t>::max());
    REQUIRE(bytes == std::vector<std::uint8_t>{0xab, 0xcd, 255, 255, 255, 255, 255, 255, 255, 255});
    const auto* cursor = bytes.data();
    const auto* end = cursor + bytes.size();
    CHECK(ReadU16(cursor, end) == 0xabcd);
    const auto* saved = cursor;
    CHECK_FALSE(ReadU64(cursor, end - 1));
    CHECK(cursor == saved);
    CHECK(ReadU64(cursor, end) == std::numeric_limits<std::uint64_t>::max());
    CHECK_FALSE(ReadU16(cursor, end));
    CHECK(cursor == end);
}

TEST_CASE("Cross type parsing and reversed server timestamps are rejected") {
    PacketHeader h{};
    auto ping = SerializePing(0, 0);
    auto pong = SerializePong(0, {0, 10, 9});
    CHECK_FALSE(ParsePong(ping.data(), ping.size(), h));
    CHECK_FALSE(ParsePing(pong.data(), pong.size(), h));
    CHECK_FALSE(ParsePong(pong.data(), pong.size(), h));
    auto valid = SerializePong(0, {0, 0, 0});
    REQUIRE(ParsePong(valid.data(), valid.size(), h));
    CHECK(h.sequenceNumber == 0);
    const std::uint8_t* empty = nullptr;
    CHECK_FALSE(ReadU16(empty, nullptr));
    CHECK_FALSE(ReadU64(empty, nullptr));
}
