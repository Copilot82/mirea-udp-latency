#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "protocol/packet.hpp"
#include "reliability/adaptive_timeout.hpp"
#include "reliability/reliable_channel.hpp"
#include <limits>

TEST_CASE("ACK and SHOOT wire bytes are explicit and validated") {
    const std::vector<std::uint8_t> ack{5,0,9,0,2,0,2,0,0x12,0x34};
    CHECK(SerializeAck(9,0x1234) == ack);
    CHECK(SerializeShoot(0x1234) == std::vector<std::uint8_t>{4,0x12,0x34,0,0,0,2,1});
    PacketHeader h{};
    const auto parsed = ParseAck(ack.data(),ack.size(),h);
    REQUIRE(parsed);
    CHECK(parsed->acknowledgedSequence == 0x1234);
    CHECK(h.requiresAck == 0);
    for (std::size_t n=0;n<ack.size();++n) CHECK_FALSE(ParseAck(ack.data(),n,h));
    for (auto field : {0u,3u,4u,5u,6u,7u}) {
        auto invalid=ack;
        invalid[field]=255;
        CHECK_FALSE(ParseAck(invalid.data(),invalid.size(),h));
    }
    auto invalid=ack;
    invalid[7]=1;
    CHECK_FALSE(ParseHeader(invalid.data(),invalid.size()));
    auto shoot=SerializeShoot(1);
    shoot[7]=0;
    CHECK_FALSE(ParseHeader(shoot.data(),shoot.size()));
    auto ping=SerializePing(1,3);
    ping[7]=1;
    CHECK_FALSE(ParseHeader(ping.data(),ping.size()));
    invalid=ack;
    invalid.push_back(0);
    CHECK_FALSE(ParseHeader(invalid.data(),invalid.size()));
    CHECK_FALSE(ParseAck(nullptr,10,h));
}
TEST_CASE("Retransmit only once per RTO and stop immediately after ACK") {
    ReliableChannel channel(3);
    const auto bytes=SerializeShoot(7);
    channel.OnSent(7,bytes,0);
    CHECK(channel.CollectForRetransmission(199999,200000).empty());
    auto due=channel.CollectForRetransmission(200000,200000);
    REQUIRE(due.size()==1);
    CHECK(due[0].attempts==2);
    CHECK(due[0].rawBytes==bytes);
    CHECK(channel.CollectForRetransmission(200000,200000).empty());
    CHECK(channel.CollectForRetransmission(399999,200000).empty());
    CHECK(channel.OnAckReceived(7,400000));
    CHECK_FALSE(channel.OnAckReceived(7,400001));
    CHECK_FALSE(channel.OnAckReceived(99,400002));
    CHECK(channel.CollectForRetransmission(999999,200000).empty());
    auto results=channel.DrainResults();
    REQUIRE(results.size()==1);
    CHECK(results[0].acknowledged);
    CHECK(results[0].packet.attempts==2);
    CHECK(results[0].finishedAtUs==400000);
    CHECK(channel.DrainResults().empty());
}
TEST_CASE("Final attempt gets a full timeout before failure and late ACK is ignored") {
    ReliableChannel channel(2);
    channel.OnSent(7,SerializeShoot(7),0);
    REQUIRE(channel.CollectForRetransmission(100000,100000).size()==1);
    CHECK(channel.CollectForRetransmission(199999,100000).empty());
    CHECK(channel.PendingCount()==1);
    CHECK(channel.CollectForRetransmission(200000,100000).empty());
    CHECK(channel.PendingCount()==0);
    CHECK(channel.FailedCount()==1);
    CHECK_FALSE(channel.OnAckReceived(7,200001));
    REQUIRE(channel.DrainResults().size()==1);
    channel.CollectForRetransmission(400000,100000);
    CHECK(channel.FailedCount()==1);
    CHECK_THROWS(channel.OnSent(7,SerializeShoot(7),400001));
}
TEST_CASE("One-attempt mode fails without retransmission") {
    ReliableChannel channel(1);
    channel.OnSent(0,SerializeShoot(0),100);
    CHECK(channel.CollectForRetransmission(200,100).empty());
    CHECK(channel.FailedCount()==1);
    const auto results=channel.DrainResults();
    REQUIRE(results.size()==1);
    CHECK_FALSE(results[0].acknowledged);
    CHECK(results[0].packet.attempts==1);
}
TEST_CASE("Reliability validates time, capacity, sequence and configuration") {
    CHECK_THROWS(ReliableChannel(0));
    CHECK_THROWS(ReliableChannel(11));
    ReliableChannel channel;
    CHECK_THROWS(channel.OnSent(0,{},0));
    channel.OnSent(0,SerializeShoot(0),10);
    CHECK_THROWS(channel.OnSent(0,SerializeShoot(0),10));
    CHECK_THROWS(channel.OnAckReceived(0,9));
    CHECK_THROWS(channel.CollectForRetransmission(10,0));
    for (unsigned i=1;i<1024;++i) channel.OnSent(static_cast<std::uint16_t>(i),{4},10);
    CHECK_THROWS(channel.OnSent(1024,{4},10));
}
TEST_CASE("Adaptive RTO follows Jacobson with previous SRTT and clamps") {
    AdaptiveTimeout timeout;
    CHECK(timeout.RtoMs()==1000);
    timeout.OnSample(100);
    CHECK(timeout.SrttMs()==100);
    CHECK(timeout.RttVarMs()==50);
    CHECK(timeout.RtoMs()==300);
    timeout.OnSample(140);
    CHECK(timeout.SrttMs()==105);
    CHECK(timeout.RttVarMs()==47.5);
    CHECK(timeout.RtoMs()==295);
    CHECK(timeout.RtoUs()==295000);
    AdaptiveTimeout low;
    low.OnSample(0.1);
    CHECK(low.RtoMs()==100);
    AdaptiveTimeout high;
    high.OnSample(2000);
    CHECK(high.RtoMs()==3000);
    for (double invalid : {-1.0,60001.0,std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) CHECK_THROWS(timeout.OnSample(invalid));
    CHECK(timeout.RtoMs()==295);
}
TEST_CASE("Receive window handles duplicate, reordering, stale and sequence wrap") {
    ReceiveWindow window;
    CHECK(window.Observe(65534)==ReceiveResult::New);
    CHECK(window.Observe(0)==ReceiveResult::New);
    CHECK(window.Observe(65535)==ReceiveResult::New);
    CHECK(window.Observe(65534)==ReceiveResult::Duplicate);
    CHECK(window.Observe(65535)==ReceiveResult::Duplicate);
    CHECK(window.Observe(0)==ReceiveResult::Duplicate);
    CHECK(window.Observe(2000)==ReceiveResult::New);
    CHECK(window.Observe(0)==ReceiveResult::Stale);
    CHECK(window.Observe(1999)==ReceiveResult::New);
    CHECK(window.Observe(1999)==ReceiveResult::Duplicate);
    ReceiveWindow anotherPeer;
    CHECK(anotherPeer.Observe(1999)==ReceiveResult::New);
}
