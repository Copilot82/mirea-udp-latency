# UDP telemetry implementation plan

**Goal:** завершить шаблон, проверить метрики, получить 300 живых измерений.
**Spec:** [Design.md](Design.md)
**Architecture:** protocol / telemetry / transport; CLI applications; Python scripts.
**Stack:** C++17, CMake >=3.16, Catch2 3.5.2, Python 3, matplotlib.
**Execution:** inline; one independent automated review of the complete branch.

## Constraints and review focus

7-byte header; PING=15/PONG=31; monotonic client clocks; timeout=1000000 us;
no retries; six 50-request series at 200 ms. Reject replay after sequence reuse,
foreign traffic, double-counted loss; bound flight history and scheduled replies;
receiving a delayed PING must not block subsequent requests.

## Task 1: protocol

Files: include/protocol/packet.hpp, src/protocol.cpp, tests/test_protocol.cpp.
Interfaces: existing Serialize/ParsePing/Pong and Write/ReadU16/U64.
- [x] Write golden fixtures, round trip, all truncated prefixes, wrong type/version/size, null input tests.
- [x] Run against stubs and observe assertion failures.
- [x] Implement explicit encoding/validation; run full suite; commit.

## Task 2: telemetry

Files: include/telemetry/telemetry.hpp, src/telemetry.cpp, tests/test_telemetry.cpp.
Interfaces: OnPingSent(seq,us), OnPong(seq,echo,now), ExpireOverdue,
DrainEvents(), Summary(), FindRecord(); retain original two-argument OnPong.
- [x] Pin SRTT 50,56.25,55.46875 for RTT 50,100,50; jitter 50.
- [x] Test deadline, unknown/duplicate/late, echo mismatch, invalid time,
      bounded records, active collision, sequence reuse, empty summary.
- [x] Observe failures; implement state transitions/aggregates; run suite; commit.

## Task 3: transport and applications

Files: include/transport, src/transport.cpp, include/app, client/server, integration test.
Interfaces: Endpoint, UdpSocket::SendTo/Receive, NowUs; positional CLI in README.
- [ ] Write subprocess tests for malformed/foreign packets, late/duplicate,
      no server, overlapping delayed responses, invalid CLI.
- [ ] Run against skeleton; implement RAII socket, event loops and CSV.
- [ ] Validate bounds, output failures, bounded delayed queue and shutdown.
- [ ] Run Debug/Release and sanitizers; commit.

## Task 4: measured artifacts and delivery

Files: scripts/run_experiments.py, scripts/analyze.py, README.md, docs/*.
- [ ] Run all six real series; retain CSV, raw logs and configuration JSON.
- [ ] Independently calculate report and three figures from CSV; inspect renders.
- [ ] Document exact wire format, environment, method and limitations.
- [ ] Obtain independent automated review; fix substantive findings and verify.
- [ ] Commit artifacts, publish GitHub/GitLab import, issues and PR.
