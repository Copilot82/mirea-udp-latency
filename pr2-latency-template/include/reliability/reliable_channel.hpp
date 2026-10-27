#pragma once
#include <bitset>
#include <cstdint>
#include <map>
#include <vector>

struct PendingPacket {
    std::uint16_t sequence;
    std::vector<std::uint8_t> rawBytes;
    std::uint64_t firstSentAtUs;
    std::uint64_t lastSentAtUs;
    std::uint32_t attempts = 1;
};
struct DeliveryResult {
    PendingPacket packet;
    bool acknowledged;
    std::uint64_t finishedAtUs;
};
class ReliableChannel {
public:
    explicit ReliableChannel(std::uint32_t maxAttempts = 5);
    void OnSent(std::uint16_t sequence, std::vector<std::uint8_t> rawBytes, std::uint64_t nowUs);
    bool OnAckReceived(std::uint16_t sequence, std::uint64_t nowUs);
    std::vector<PendingPacket> CollectForRetransmission(std::uint64_t nowUs, std::uint64_t rtoUs);
    std::vector<DeliveryResult> DrainResults();
    std::size_t PendingCount() const { return pending_.size(); }
    std::size_t FailedCount() const { return failedCount_; }
private:
    void CheckTime(std::uint64_t nowUs);
    std::uint32_t maxAttempts_;
    std::map<std::uint16_t, PendingPacket> pending_;
    std::bitset<65536> used_;
    std::vector<DeliveryResult> results_;
    std::size_t failedCount_ = 0;
    std::uint64_t lastTimeUs_ = 0;
};

enum class ReceiveResult { New, Duplicate, Stale };
// Окно отдельно для каждого UDP-адреса отправителя.
class ReceiveWindow {
public:
    ReceiveResult Observe(std::uint16_t sequence);
private:
    bool initialized_ = false;
    std::uint16_t newest_ = 0;
    std::bitset<1024> seen_;
};
