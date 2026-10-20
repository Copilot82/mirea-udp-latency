#include "app/csv_log.hpp"
#include "app/options.hpp"
#include "protocol/packet.hpp"
#include "telemetry/telemetry.hpp"
#include "transport/udp.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
int Run(int argc, char** argv) {
    if (argc < 6 || argc > 9)
        throw std::invalid_argument("usage: client IP PORT EXPERIMENT COUNT INTERVAL_MS [CSV] [TIMEOUT_MS] [GRACE_MS]");
    const auto port = static_cast<std::uint16_t>(app::Number(argv[2], 1, 65535));
    if (!app::ValidId(argv[3])) throw std::invalid_argument("invalid experiment id (letters/digits/_/- only)");
    const auto count = app::Number(argv[4], 1, 1'000'000);
    const auto intervalUs = app::Number(argv[5], 1, 60000) * 1000;
    const auto timeoutUs = argc > 7 ? app::Number(argv[7], 1, 60000) * 1000 : 1'000'000;
    const auto graceUs = argc > 8 ? app::Number(argv[8], 0, 60000) * 1000 : 200'000;
    if (std::min(count, timeoutUs / intervalUs + 2) > 1024)
        throw std::invalid_argument("interval/timeout would exceed 1024 flight records");
    const transport::Endpoint peer(argv[1], port);
    transport::UdpSocket socket;
    Telemetry telemetry(timeoutUs, 1024, argv[3]);
    const auto start = transport::NowUs();
    app::CsvLog csv(argc > 6 ? argv[6] : "docs/latency_samples.csv", argv[3], start);
    std::uint64_t sent = 0, nextSend = start, end = 0;
    while (true) {
        auto now = transport::NowUs();
        telemetry.ExpireOverdue(now);
        if (sent < count && now >= nextSend) {
            const auto seq = static_cast<std::uint16_t>(sent & 0xffff);
            // Timestamp immediately before serialization/send; record only a successful sendto.
            const auto sentUs = transport::NowUs();
            socket.SendTo(peer, SerializePing(seq, sentUs));
            telemetry.OnPingSent(seq, sentUs);
            ++sent;
            nextSend = start + sent * intervalUs; // Absolute schedule avoids response-time drift.
        }
        for (const auto& event : telemetry.DrainEvents()) csv.Write(event);
        now = transport::NowUs();
        if (sent == count && telemetry.PendingCount() == 0) {
            if (!end) end = now + graceUs;
            if (now >= end) break;
        }
        auto waitUs = std::uint64_t{10'000};
        if (sent < count) waitUs = std::min(waitUs, nextSend > now ? nextSend - now : 0);
        if (end) waitUs = std::min(waitUs, end > now ? end - now : 0);
        const auto data = socket.Receive(waitUs);
        if (!data) continue;
        if (!(data->peer == peer)) {
            std::cerr << "foreign_response peer=" << data->peer.ToString() << '\n';
            csv.Write({ResponseStatus::Foreign, 0, 0, std::nullopt, data->receivedAtUs, {}, {}});
            continue;
        }
        PacketHeader header{};
        const auto pong = ParsePong(data->bytes.data(), data->bytes.size(), header);
        if (!pong) {
            const char* reason = PacketError(data->bytes.data(), data->bytes.size());
            std::cerr << "invalid_response reason=" << (reason ? reason : "expected_pong_or_invalid_server_time") << '\n';
            csv.Write({ResponseStatus::Invalid, 0, 0, std::nullopt, data->receivedAtUs, {}, {}});
            continue;
        }
        const auto status = telemetry.OnPong(header.sequenceNumber, pong->clientSendTimeUs, data->receivedAtUs);
        if (status != ResponseStatus::Received)
            std::cerr << StatusName(status) << " sequence=" << header.sequenceNumber << '\n';
        for (const auto& event : telemetry.DrainEvents()) csv.Write(event);
    }
    const auto s = telemetry.Summary();
    std::cout << std::fixed << std::setprecision(6) << "sent=" << s.sent << " received=" << s.received
              << " timeout=" << s.timeouts << " loss_percent=" << s.lossPercent << " srtt_ms=";
    if (s.srttMs) std::cout << *s.srttMs; else std::cout << "NA";
    std::cout << " jitter_ms=" << s.jitterMs << '\n';
    return 0;
}
}
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (const std::exception& e) { std::cerr << "client: " << e.what() << '\n'; return 1; }
}
