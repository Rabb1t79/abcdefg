# Tetris Pilot Next

A clean-room, Windows-first automation architecture for the locally hosted Tetris Online client.

This directory is a **new implementation**, not a port of old MisaMino / ZZZTOJ bot code. Those projects may be consulted as historical references for game behavior, but no stale runtime, process hooks, input code, or solver implementation is imported.

## Design goals

- Build and validate the core independently of any game client.
- Treat the game-specific implementation as an adapter, not as the solver.
- Make state acquisition replaceable: start with the current visual source, then add a read-only process-state provider only after its data layout is proven for the exact client build.
- Keep controls behind a separate action interface so input delivery can be measured and verified.
- Never assume generic SRS rules, spawn coordinates, score rules, or undocumented game internals. The adapter supplies a measured piece catalog and observed client rules.
- Prefer explicit state transitions, diagnostics, and recovery over silent guesses.

## Architecture layers

\`domain/\` (public headers) owns the 10×20 board model, piece identity, snapshots, and normalized coordinates.

\`solver.cpp\` is a pure deterministic search component. It receives a board, a sequence of known pieces, and a piece catalog supplied by the adapter. It has no Windows, screen-capture, process, or input dependencies.

\`game_ports.hpp\` defines the boundary between the core and client-specific providers:
- \`IGameStateSource\` supplies a normalized snapshot.
- \`IGameActionSink\` dispatches one explicit action and returns a receipt; dispatch is not mistaken for successful in-game execution.

\`session_state.cpp\` is a strict runner state machine. Planning, dispatch, verification, recovery, stop, and game-over are separate states.

\`platform/win32/process_inventory.*\` is a metadata-only diagnostic helper. It lists the target process executable, architecture, loaded module ranges, and committed image-region layout. It deliberately does **not** read, write, scan, or patch raw game memory. It provides a reproducible inventory before any game-specific state layout is researched.

## Implementation sequence

1. Portable board model, solver, and unit tests.
2. Win32 process/window inventory and a reproducible client report.
3. Read-only state provider for the exact client build, validated against captured snapshots.
4. Input adapter and action timing/receipt logging.
5. Coordinator loop: read → plan → dispatch → re-read → reconcile → recover.
6. Full-run score/game-over/restart and long-session tests.
7. Add optional GPU search only if CPU measurements show a real bottleneck.

## Current scope

Milestone 1 implements a deterministic multi-piece search with a client-supplied piece catalog and portable tests. Milestone 2 adds the Windows process inventory library and a small runnable inspector UI. The inspector is a diagnostic utility, not yet the automated player. There is not yet a runnable controller executable: the UI and live client adapter will be added only after these contracts are stable.

Build on Windows 10 with Visual Studio 2022 + CMake, or run the portable core tests on any C++17 toolchain:

\`\`\`powershell
cmake -S tools/TetrisPilotNext -B build/TetrisPilotNext
cmake --build build/TetrisPilotNext --config Release
ctest --test-dir build/TetrisPilotNext -C Release --output-on-failure
\`\`\`

Existing \`TetrisVisionPilot\` and \`TetrisVisionPilotBenchmark\` directories remain unchanged and are retained as diagnostic / comparison baselines.
