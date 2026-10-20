# Development evidence

- Fresh clone of upstream b01e060; work on feature/latency-measurement.
- Initial standalone checks against stubs: wire fixture FAIL; 50 ms RTT FAIL.
- Implemented protocol/telemetry: both checks PASS; Catch2 suite passes.
- Catch2 download changed from full Git clone to SHA256-pinned v3.5.2 archive.
- Working tree is an isolated new clone; existing PR1 files remain outside it.
- User explicitly selected GitHub/GitLab instead of GitVerse; imported original history.
- Integration RED: skeleton exits before READY. Implemented RAII UDP, CSV and event loops.
- macOS UDP send buffer rejected a 65507-byte fixture; replaced real-network oversize fixture with 8192 bytes. Parser still handles full datagram lengths.
- Experiment: 300 actual UDP PINGs, 298 received, 2 timeouts; all six raw logs retained.
- Independent automated reviewer found equal-timestamp replay after eviction and destructive runner preflight; regressions reproduced RED and fixed GREEN.
- Dynamic report parameters now come from metadata.
- Final Debug/Release/UBSan: all 3 CTest targets passed (17 Catch2 scenarios/197 assertions, 7 socket scenarios, 5 analysis/preflight tests).
- Local ASan could not start due to runtime initialization deadlock before main; documented, Linux ASan CI added.
- Three rendered figures inspected; box plot caption overlap fixed and re-rendered.
- Published: https://github.com/Copilot82/mirea-udp-latency/tree/feature/latency-measurement; PR #4; human teammate review tracked separately in issue #3.
