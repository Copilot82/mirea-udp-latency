#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "telemetry/telemetry.hpp"
#include <stdexcept>

TEST_CASE("Client clock RTT initializes SRTT and mean absolute jitter") {
    Telemetry t;
    t.OnPingSent(1, 0);
    REQUIRE(t.OnPong(1, 50'000) == ResponseStatus::Received);
    CHECK(t.SrttMs() == Catch::Approx(50));
    CHECK(t.MeanJitterMs() == 0);
    t.OnPingSent(2, 200'000);
    REQUIRE(t.OnPong(2, 300'000) == ResponseStatus::Received);
    CHECK(t.SrttMs() == Catch::Approx(56.25));
    t.OnPingSent(3, 400'000);
    REQUIRE(t.OnPong(3, 450'000) == ResponseStatus::Received);
    CHECK(t.SrttMs() == Catch::Approx(55.46875));
    CHECK(t.MeanJitterMs() == Catch::Approx(50));
    CHECK(t.SampleCount() == 3);
}

TEST_CASE("Unknown and duplicate responses cannot add successful samples") {
    Telemetry t;
    CHECK(t.OnPong(42, 10) == ResponseStatus::Unknown);
    t.OnPingSent(1, 0);
    REQUIRE(t.OnPong(1, 100) == ResponseStatus::Received);
    CHECK(t.OnPong(1, 500) == ResponseStatus::Duplicate);
    CHECK(t.SampleCount() == 1);
    CHECK(t.SrttMs() == Catch::Approx(0.1));
}

TEST_CASE("Timeout deadline is inclusive and expiration is idempotent") {
    Telemetry t;
    t.OnPingSent(1, 0);
    CHECK(t.ExpireOverdue(999'999) == 0);
    CHECK(t.ExpireOverdue(1'000'000) == 1);
    CHECK(t.ExpireOverdue(2'000'000) == 0);
    CHECK(t.OnPong(1, 2'000'001) == ResponseStatus::Late);
    CHECK(t.OnPong(1, 2'000'002) == ResponseStatus::Duplicate);
    CHECK(t.SampleCount() == 0);
}

TEST_CASE("Late reply creates one timeout even without an expiration tick") {
    Telemetry t;
    t.OnPingSent(7, 100);
    CHECK(t.OnPong(7, 100, 1'000'100) == ResponseStatus::Late);
    auto events = t.DrainEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[0].status == ResponseStatus::Timeout);
    CHECK(events[1].status == ResponseStatus::Late);
    CHECK_FALSE(events[1].rttMs);
    CHECK(t.OnPong(7, 100, 1'000'101) == ResponseStatus::Duplicate);
    CHECK(t.ExpireOverdue(9'000'000) == 0);
    auto s = t.Summary();
    CHECK(s.sent == 1);
    CHECK(s.timeouts == 1);
    CHECK(s.late == 1);
    CHECK(s.duplicates == 1);
    CHECK(s.lossPercent == 100);
    CHECK_FALSE(s.meanMs);
    CHECK_FALSE(s.srttMs);
}

TEST_CASE("Echo mismatch and backward receive time cannot complete a flight") {
    Telemetry t;
    t.OnPingSent(1, 1000);
    CHECK(t.OnPong(1, 999, 2000) == ResponseStatus::Invalid);
    CHECK(t.OnPong(1, 1000, 999) == ResponseStatus::Invalid);
    CHECK(t.PendingCount() == 1);
    CHECK(t.OnPong(1, 1000, 2000) == ResponseStatus::Received);
    CHECK(t.Summary().invalid == 2);
    CHECK(t.Summary().received == 1);
    CHECK(t.SrttMs() == Catch::Approx(1));
}

TEST_CASE("Bounded history evicts completed records but never pending requests") {
    Telemetry t(1'000'000, 2, "bounded");
    t.OnPingSent(1, 0);
    t.OnPingSent(2, 1);
    CHECK_THROWS_AS(t.OnPingSent(3, 2), std::length_error);
    CHECK(t.Summary().sent == 2);
    CHECK_THROWS_AS(t.OnPingSent(1, 2), std::logic_error);
    REQUIRE(t.OnPong(2, 1, 2) == ResponseStatus::Received);
    t.OnPingSent(3, 3);
    CHECK(t.RecordCount() == 2);
    CHECK(t.FindRecord(1));
    CHECK_FALSE(t.FindRecord(2));
    CHECK(t.OnPong(2, 1, 4) == ResponseStatus::Unknown);
    const auto* record = t.FindRecord(3);
    REQUIRE(record);
    CHECK(record->experimentId == "bounded");
    CHECK(record->attempts == 1);
    CHECK(record->sequenceNumber == 3);
    CHECK(record->sample == 3);
}

TEST_CASE("Sequence wrap and old timestamp replay do not corrupt a new flight") {
    Telemetry t;
    t.OnPingSent(65535, 1);
    CHECK(t.OnPong(65535, 1, 2) == ResponseStatus::Received);
    t.OnPingSent(0, 3);
    CHECK(t.OnPong(0, 3, 4) == ResponseStatus::Received);
    t.OnPingSent(65535, 5);
    CHECK(t.OnPong(65535, 1, 6) == ResponseStatus::Invalid);
    CHECK(t.PendingCount() == 1);
    CHECK(t.OnPong(65535, 5, 7) == ResponseStatus::Received);
    CHECK(t.Summary().received == 3);
}

TEST_CASE("Summary uses successful RTTs only and has well defined empty values") {
    Telemetry t;
    auto s = t.Summary();
    CHECK(s.sent == 0);
    CHECK(s.lossPercent == 0);
    CHECK(s.jitterMs == 0);
    CHECK_FALSE(s.minMs);
    CHECK_FALSE(s.meanMs);
    t.OnPingSent(1, 0);
    t.OnPingSent(2, 100);
    t.OnPingSent(3, 200);
    CHECK(t.OnPong(1, 0, 10'000) == ResponseStatus::Received);
    CHECK(t.OnPong(3, 200, 30'200) == ResponseStatus::Received);
    CHECK(t.ExpireOverdue(1'000'100) == 1);
    s = t.Summary();
    CHECK(s.sent == 3);
    CHECK(s.received == 2);
    CHECK(s.timeouts == 1);
    CHECK(s.minMs == 10);
    CHECK(s.maxMs == 30);
    CHECK(s.meanMs == 20);
    CHECK(s.medianMs == 20);
    CHECK(s.srttMs == 12.5);
    CHECK(s.jitterMs == 20);
    CHECK(s.lossPercent == Catch::Approx(100.0 / 3));
    CHECK(t.PendingCount() == 0);
}

TEST_CASE("Reordered replies contribute jitter in receive order") {
    Telemetry t;
    t.OnPingSent(1, 0);
    t.OnPingSent(2, 10'000);
    CHECK(t.OnPong(2, 10'000, 20'000) == ResponseStatus::Received);
    CHECK(t.OnPong(1, 0, 30'000) == ResponseStatus::Received);
    CHECK(t.SrttMs() == 12.5);
    CHECK(t.MeanJitterMs() == 20);
}

TEST_CASE("Invalid configurations and backward sends are rejected") {
    CHECK_THROWS_AS(Telemetry(0), std::invalid_argument);
    CHECK_THROWS_AS(Telemetry(100, 0), std::invalid_argument);
    CHECK_THROWS_AS(Telemetry(100, 65537), std::invalid_argument);
    Telemetry t;
    t.OnPingSent(1, 10);
    CHECK_THROWS_AS(t.OnPingSent(2, 9), std::invalid_argument);
    CHECK(t.Summary().sent == 1);
}

TEST_CASE("Evicted sequence cannot reuse the same timestamp") {
    Telemetry t(1'000'000, 1);
    t.OnPingSent(1, 1000);
    REQUIRE(t.OnPong(1, 1000, 1000) == ResponseStatus::Received);
    CHECK_THROWS_AS(t.OnPingSent(2, 1000), std::invalid_argument);
    t.OnPingSent(2, 1001);
    REQUIRE(t.OnPong(2, 1001, 1001) == ResponseStatus::Received);
    t.OnPingSent(1, 1002);
    CHECK(t.OnPong(1, 1000, 1003) == ResponseStatus::Invalid);
    CHECK(t.PendingCount() == 1);
}
