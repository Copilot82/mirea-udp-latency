#include "app/options.hpp"
#include "protocol/packet.hpp"
#include "reliability/adaptive_timeout.hpp"
#include "reliability/reliable_channel.hpp"
#include "telemetry/telemetry.hpp"
#include "transport/udp.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {
int Run(int argc, char** argv) {
    if (argc < 7 || argc > 8)
        throw std::invalid_argument("usage: reliable_client IP PORT EXPERIMENT COUNT INTERVAL_MS CSV [MAX_ATTEMPTS]");
    const transport::Endpoint peer(argv[1], static_cast<std::uint16_t>(app::Number(argv[2], 1, 65535)));
    if (!app::ValidId(argv[3])) throw std::invalid_argument("invalid experiment id");
    const auto count = app::Number(argv[4], 1, 65536);
    const auto intervalUs = app::Number(argv[5], 1, 60000) * 1000;
    const auto maxAttempts = static_cast<std::uint32_t>(argc > 7 ? app::Number(argv[7], 1, 10) : 5);
    std::ofstream csv(argv[6]);
    if (!csv) throw std::runtime_error("cannot open delivery CSV");
    csv.exceptions(std::ios::badbit | std::ios::failbit);
    csv << std::fixed << std::setprecision(6)
        << "experiment_id;sequence;event;attempts;elapsed_ms;rto_ms;time_to_ack_ms;srtt_ms;rttvar_ms\n";
    ReliableChannel channel(maxAttempts);
    AdaptiveTimeout timeout;
    Telemetry telemetry(3'000'000, 1024, argv[3]);
    transport::UdpSocket socket;
    const auto start = transport::NowUs();
    std::uint64_t sent = 0, nextSend = start + 150'000, nextProbe = start;
    std::uint16_t probeSeq = 0;
    std::size_t acknowledged = 0, firstTry = 0, retransmissions = 0;
    auto log = [&](std::uint16_t seq, const char* event, unsigned attempts,
                   std::uint64_t now, double ackMs = -1) {
        csv << argv[3] << ';' << seq << ';' << event << ';' << attempts << ';'
            << double(now-start)/1000.0 << ';' << timeout.RtoMs() << ';';
        if (ackMs >= 0) csv << ackMs;
        csv << ';' << timeout.SrttMs() << ';' << timeout.RttVarMs() << '\n';
    };
    auto complete = [&]() {
        for (const auto& result : channel.DrainResults()) {
            const auto& p = result.packet;
            const double ackMs = double(result.finishedAtUs-p.firstSentAtUs)/1000.0;
            if (result.acknowledged) {
                ++acknowledged;
                if (p.attempts == 1) {
                    ++firstTry;
                    timeout.OnSample(ackMs); // Karn: RTT повторных отправок неоднозначен.
                }
                log(p.sequence, "acked", p.attempts, result.finishedAtUs, ackMs);
            } else {
                log(p.sequence, "failed", p.attempts, result.finishedAtUs);
                std::cerr << "SHOOT unconfirmed sequence=" << p.sequence << " attempts=" << p.attempts << '\n';
            }
        }
    };
    while (sent < count || channel.PendingCount()) {
        const auto now = transport::NowUs();
        telemetry.ExpireOverdue(now);
        if (now >= nextProbe) {
            socket.SendTo(peer, SerializePing(probeSeq, now));
            telemetry.OnPingSent(probeSeq++, now);
            nextProbe = now + 200'000;
        }
        telemetry.DrainEvents();
        if (sent < count && now >= nextSend && channel.PendingCount() < 512) {
            const auto seq = static_cast<std::uint16_t>(sent);
            const auto bytes = SerializeShoot(seq);
            channel.OnSent(seq, bytes, now);
            socket.SendTo(peer, bytes);
            log(seq, "sent", 1, now);
            ++sent;
            nextSend = now + intervalUs;
        }
        for (const auto& packet : channel.CollectForRetransmission(now, timeout.RtoUs())) {
            socket.SendTo(peer, packet.rawBytes);
            ++retransmissions;
            log(packet.sequence, "retransmit", packet.attempts, now);
        }
        complete();
        const auto data = socket.Receive(1'000);
        if (!data) continue;
        if (!(data->peer == peer)) { log(0, "foreign", 0, data->receivedAtUs); continue; }
        const auto h = ParseHeader(data->bytes.data(), data->bytes.size());
        if (!h) { log(0, "invalid", 0, data->receivedAtUs); continue; }
        PacketHeader parsed{};
        if (h->type == PacketType::Ack) {
            const auto ack = ParseAck(data->bytes.data(), data->bytes.size(), parsed);
            if (!channel.OnAckReceived(ack->acknowledgedSequence, data->receivedAtUs))
                log(ack->acknowledgedSequence, "ignored_ack", 0, data->receivedAtUs);
            complete();
        } else if (h->type == PacketType::Pong) {
            const auto pong = ParsePong(data->bytes.data(), data->bytes.size(), parsed);
            if (!pong) { log(0, "invalid", 0, data->receivedAtUs); continue; }
            const auto status = telemetry.OnPong(parsed.sequenceNumber, pong->clientSendTimeUs, data->receivedAtUs);
            if (status == ResponseStatus::Received) {
                timeout.OnSample(*telemetry.FindRecord(parsed.sequenceNumber)->rttMs);
                log(parsed.sequenceNumber, "pong_sample", 0, data->receivedAtUs);
            }
        } else log(h->sequenceNumber, "invalid", 0, data->receivedAtUs);
    }
    std::cout << std::fixed << std::setprecision(3) << "sent=" << sent << " acked=" << acknowledged
              << " first_try=" << firstTry << " retransmissions=" << retransmissions
              << " failed=" << channel.FailedCount() << " final_rto_ms=" << timeout.RtoMs() << '\n';
    return channel.FailedCount() ? 2 : 0;
}
}
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (const std::exception& e) { std::cerr << "reliable_client: " << e.what() << '\n'; return 1; }
}
