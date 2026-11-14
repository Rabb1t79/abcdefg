# Tetris Pilot Next

A clean-room, Windows-first automation program targeting one frozen Tetris Online client build.

This directory is a **new implementation**, not a port of old MisaMino / ZZZTOJ bot code. Those projects may be consulted as historical references for game behavior, but no stale runtime, process hooks, input code, or solver implementation is imported.

## Fixed-client target

The working assumption is that the selected Tetris Online client is the final public client build and will not receive further official updates. We therefore target **one exact executable build**, not a family of versions and not a generic multi-client compatibility framework.

The first Windows inspector reports the executable path, size, SHA-256, process architecture, loaded module ranges, and committed image-region layout. This lets us pin future client-specific offsets and layouts to a concrete executable fingerprint. The live adapter will reject an unrecognized executable rather than silently reusing guessed offsets.

The report contains executable/module metadata and virtual-memory region metadata only. It does not dump, read, or modify raw game memory.

## Design goals

- Keep the game-specific implementation narrow and optimized for the one confirmed client build.
- Do not introduce multi-version compatibility layers or runtime version guessing.
- Build and validate the solver independently of the client.
- Use a read-only process-state provider once the exact data layout has been confirmed; retain visual capture as an independent validation/fallback path.
- Keep action dispatch separate from state acquisition, and verify the resulting game state after every action.
- Do not assume generic SRS rules, spawn coordinates, scoring rules, or undocumented internals before they are measured from this client.
- Prefer explicit state transitions, diagnostics, and recovery over silent guesses.

## Architecture

- `domain.hpp`: 10×20 board, piece definitions, active-piece coordinates, NEXT/Hold, phase and score snapshot.
- `solver.cpp`: pure deterministic search. It receives a board, known piece sequence, and exact client piece catalog. It has no Windows, capture, process, or input dependencies.
- `game_ports.hpp`: `IGameStateSource` reads normalized snapshots; `IGameActionSink` submits actions and returns dispatch receipts.
- `session_state.cpp`: explicit planning, execution, verification, recovery, Game Over and restart state transitions.
- `platform/win32/process_inventory.*`: fixed-client fingerprint and module/address-space inventory.
- `inspector_main.cpp`: small Windows utility to select the game window and export the report.

## Implementation sequence

1. Validate the portable board model, solver and unit tests.
2. Build the Windows inspector and collect the one target client's fingerprint.
3. Trace only that client's state structures and implement a narrowly scoped read-only provider.
4. Compare the provider's snapshots with independent visual observations until board, active piece, NEXT, Hold, score and phase agree.
5. Implement the input adapter and action timing/receipt logging.
6. Connect the coordinator: read → plan → dispatch → re-read → reconcile → recover.
7. Validate complete games, score settlement, Game Over, restart and long-session stability.
8. Add GPU search only if measurements show CPU search is an actual bottleneck.

## Current scope

The deterministic core, client/action ports, session state machine, portable tests, and Windows process inspector are in place. The inspector is a diagnostic utility, not yet the automated player. No in-process game-state addresses have been asserted or guessed.

## Build

On Windows 10 with Visual Studio 2022 + CMake:

```powershell
cmake -S tools/TetrisPilotNext -B build/TetrisPilotNext
cmake --build build/TetrisPilotNext --config Release
ctest --test-dir build/TetrisPilotNext -C Release --output-on-failure
```

Existing `TetrisVisionPilot` and `TetrisVisionPilotBenchmark` directories remain unchanged as diagnostic/comparison baselines.
