#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// A measurement has one terminal outcome (Received or Timeout).
// Late/Duplicate/Unknown/Invalid are additional diagnostic events.
enum class ResponseStatus { Pending, Received, Timeout, Late, Duplicate, Unknown, Invalid, Foreign };
const char* StatusName(ResponseStatus status);

struct FlightRecord {
    std::uint16_t sequenceNumber = 0;
    std::uint64_t sentAtUs = 0;
    std::string experimentId;
    ResponseStatus status = ResponseStatus::Pending;
    unsigned attempts = 1;
    std::size_t sample = 0;
    std::optional<double> rttMs;
    std::optional<double> srttMs;
};

struct MeasurementEvent {
    ResponseStatus status;
    std::size_t sample;
    std::uint16_t sequence;
    std::optional<std::uint64_t> sentAtUs;
    std::uint64_t observedAtUs;
    std::optional<double> rttMs;
    std::optional<double> srttMs;
};

struct TelemetrySummary {
    std::size_t sent = 0, received = 0, timeouts = 0;
    std::size_t late = 0, duplicates = 0, unknown = 0, invalid = 0;
    std::optional<double> minMs, maxMs, meanMs, medianMs, srttMs;
    double jitterMs = 0, lossPercent = 0;
};

class Telemetry {
public:
    explicit Telemetry(std::uint64_t timeoutUs = 1'000'000,
                       std::size_t maxRecords = 1024, std::string experimentId = "test");
    // Throws for full pending capacity, active sequence reuse or backward send time.
    void OnPingSent(std::uint16_t sequence, std::uint64_t nowUs);
    ResponseStatus OnPong(std::uint16_t sequence, std::uint64_t echoedSendUs, std::uint64_t nowUs);
    // Compatibility with the original template; wire consumers must use the 3-arg form.
    ResponseStatus OnPong(std::uint16_t sequence, std::uint64_t nowUs);
    std::size_t ExpireOverdue(std::uint64_t nowUs);
    std::vector<MeasurementEvent> DrainEvents();
    TelemetrySummary Summary() const;
    const FlightRecord* FindRecord(std::uint16_t sequence) const;
    std::size_t RecordCount() const { return records_.size(); }
    std::size_t PendingCount() const { return totals_.sent - totals_.received - totals_.timeouts; }
    double SrttMs() const { return srttMs_; }
    double MeanJitterMs() const;
    std::size_t SampleCount() const { return rtts_.size(); }

private:
    void Emit(ResponseStatus status, const FlightRecord* record,
              std::uint16_t sequence, std::uint64_t nowUs);
    void Timeout(FlightRecord& record, std::uint64_t nowUs);
    std::uint64_t timeoutUs_;
    std::size_t maxRecords_;
    std::string experimentId_;
    std::optional<std::uint64_t> lastSentUs_;
    std::unordered_map<std::uint16_t, FlightRecord> records_;
    std::deque<std::uint16_t> order_;
    std::vector<MeasurementEvent> events_;
    std::vector<double> rtts_;
    TelemetrySummary totals_;
    double srttMs_ = 0, jitterSumMs_ = 0;
};
