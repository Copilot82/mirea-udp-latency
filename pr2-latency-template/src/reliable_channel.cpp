#include "reliability/reliable_channel.hpp"
#include <stdexcept>
#include <utility>

ReliableChannel::ReliableChannel(std::uint32_t maxAttempts) : maxAttempts_(maxAttempts) {
    if (maxAttempts < 1 || maxAttempts > 10)
        throw std::invalid_argument("maxAttempts must be 1..10");
}
void ReliableChannel::CheckTime(std::uint64_t nowUs) {
    if (nowUs < lastTimeUs_) throw std::invalid_argument("clock moved backwards");
    lastTimeUs_ = nowUs;
}
void ReliableChannel::OnSent(std::uint16_t seq, std::vector<std::uint8_t> bytes, std::uint64_t nowUs) {
    CheckTime(nowUs);
    if (bytes.empty() || bytes.size() > 65507) throw std::invalid_argument("invalid datagram size");
    // Номер не переиспользуется в сессии: старый ACK не подтвердит новую команду.
    if (used_.test(seq)) throw std::logic_error("sequence already used in this session");
    if (pending_.size() >= 1024) throw std::length_error("pending capacity exceeded");
    pending_.emplace(seq, PendingPacket{seq, std::move(bytes), nowUs, nowUs, 1});
    used_.set(seq);
}
bool ReliableChannel::OnAckReceived(std::uint16_t seq, std::uint64_t nowUs) {
    CheckTime(nowUs);
    const auto found = pending_.find(seq);
    if (found == pending_.end()) return false;
    results_.push_back({std::move(found->second), true, nowUs});
    pending_.erase(found);
    return true;
}
std::vector<PendingPacket> ReliableChannel::CollectForRetransmission(std::uint64_t nowUs, std::uint64_t rtoUs) {
    CheckTime(nowUs);
    if (!rtoUs) throw std::invalid_argument("RTO must be positive");
    std::vector<PendingPacket> due;
    for (auto it = pending_.begin(); it != pending_.end();) {
        auto& packet = it->second;
        if (nowUs - packet.lastSentAtUs < rtoUs) { ++it; continue; }
        if (packet.attempts >= maxAttempts_) {
            results_.push_back({std::move(packet), false, nowUs});
            ++failedCount_;
            it = pending_.erase(it);
        } else {
            ++packet.attempts;
            packet.lastSentAtUs = nowUs;
            due.push_back(packet);
            ++it;
        }
    }
    return due;
}
std::vector<DeliveryResult> ReliableChannel::DrainResults() {
    std::vector<DeliveryResult> result;
    result.swap(results_);
    return result;
}
ReceiveResult ReceiveWindow::Observe(std::uint16_t sequence) {
    if (!initialized_) {
        initialized_ = true;
        newest_ = sequence;
        seen_.set(0);
        return ReceiveResult::New;
    }
    const auto forward = static_cast<std::uint16_t>(sequence - newest_);
    if (forward && forward < 32768) {
        seen_ <<= forward;
        newest_ = sequence;
        seen_.set(0);
        return ReceiveResult::New;
    }
    const auto age = static_cast<std::uint16_t>(newest_ - sequence);
    if (age >= seen_.size()) return ReceiveResult::Stale;
    if (seen_.test(age)) return ReceiveResult::Duplicate;
    seen_.set(age);
    return ReceiveResult::New;
}
