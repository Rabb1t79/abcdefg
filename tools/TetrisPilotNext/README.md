# Tetris Pilot Next

A clean-room, Windows-first automation program targeting one frozen Tetris Online client build.

This directory is a **new implementation**, not a port of old MisaMino / ZZZTOJ bot code. Those projects may be consulted as historical references for game behavior, but no stale runtime, process hooks, input code, or solver implementation is imported.

## Fixed-client target

The working assumption is that the selected Tetris Online client is the final public client build and will not receive further official updates. We therefore target **one exact executable build**, not a family of versions and not a generic multi-client compatibility framework.

The first Windows inspector reports the executable path, size, SHA-256, process architecture, loaded module ranges, and committed image-region layout. This lets us pin future client-specific offsets and layouts to a concrete executable fingerprint. The live adapter will reject an unrecognized executable rather than silently reusing guessed offsets.

The first report has now confirmed and pinned the target executable:

| Field | Confirmed value |
|---|---|
| Process architecture | x86 on x64 |
| Executable size | 1,654,784 bytes |
| SHA-256 | `9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42` |

The installation path is intentionally not part of the identity check. The executable must match all three pinned fields (size, architecture, and SHA-256) before a future client adapter is allowed to proceed. A mismatch is a hard rejection, not a fallback to guessed offsets.

The metadata report contains executable/module and virtual-memory region metadata only. The separate snapshot operation performs read-only `ReadProcessMemory` calls after validating the exact client fingerprint. It captures committed readable `MEM_PRIVATE` regions plus readable/writable `MEM_IMAGE` pages belonging only to modules installed beside the game executable. This includes the game's writable image data (such as its `.data` pages) while excluding Windows system DLL image pages. It never writes to or suspends the game process. Snapshot files contain raw bytes from process memory, so keep them local and do not share them publicly.

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
- `client_profile.hpp`: exact allowlist for the confirmed x86 client executable; size, architecture, and SHA-256 must all match.
- `solver.cpp`: pure deterministic search. It receives a board, known piece sequence, and exact client piece catalog. It has no Windows, capture, process, or input dependencies.
- `game_ports.hpp`: `IGameStateSource` reads normalized snapshots; `IGameActionSink` submits actions and returns dispatch receipts.
- `session_state.cpp`: explicit planning, execution, verification, recovery, Game Over and restart state transitions.
- `platform/win32/process_inventory.*`: fixed-client fingerprint and module/address-space inventory.
- The inspector counts committed `MEM_PRIVATE` regions and lists larger readable regions for the metadata report. The snapshot action captures all committed readable `MEM_PRIVATE` regions plus readable/writable `MEM_IMAGE` pages whose module path is in the same installation directory as the verified game executable. System DLL image pages and read-only code/constant pages are excluded. Reads are made in 64 KiB chunks and unreadable/skipped byte counts are recorded. Snapshot v2 records each span's memory type and game module name; the reader remains compatible with existing v1 private-memory snapshots.
- `memory_snapshot.cpp`: versioned binary snapshot format, guarded loader, same-address byte comparison, and compact difference report.
- `inspector_main.cpp`: Windows utility to select the game window, export metadata, capture snapshots, and compare two snapshots.


## TOP visual-piece reachability baseline

This increment adds two portable components:

- \`top_piece_profile.cpp\` builds the seven tetromino shapes using the same normalized screen-coordinate geometry already used by the repository's Tetris visual recognizer. The Up key is modeled as clockwise, matching that recognizer.
- \`reachability.cpp\` performs breadth-first search over left, right, optional soft-drop, and explicitly configured clockwise/counter-clockwise rotation transitions. Each result includes the actual action path ending in Hard Drop, landing coordinates, and the resulting board after line clears.

The rotation profile uses nominal 3×3 boxes for J/L/S/Z/T and a 4×4 box for I to calculate the geometric anchor shift when rotating normalized shapes. This is a pivot-alignment baseline, **not a verified TOP wall-kick table**. No wall-kick offsets are added. The planner only uses rotation transitions supplied in \`ReachabilityOptions\`; callers must add only offsets measured for the target client.

The search cap is explicit. If \`truncated\` is true, the returned landing list is incomplete and must not be treated as exhaustive. The planner models a discrete input graph; it does not yet simulate the client's gravity timer, lock delay, DAS/ARR, or frame-level input timing.

**Integration status:** this is a portable, unit-tested planning component in TetrisPilotNext. It is not yet wired to the live TOP window capture/input loop, and it does not replace the existing TetrisVisionPilotBenchmark executable. The next integration step is to feed verified visual snapshots into this planner and compare proposed paths against active-piece/Ghost feedback before enabling live dispatch.

## Implementation sequence

1. Validate the portable board model, solver and unit tests.
2. Build the Windows inspector and collect the one target client's fingerprint. The metadata report lists larger readable private-memory regions without reading their contents.
3. Capture controlled memory snapshots at selected game states, then compare them. Compare only snapshots from the same process ID and exact executable build. Use changed allocation groups, pages and byte runs to form candidates; do not treat a changed address as an identified game variable.
4. Trace only that client's state structures and implement a narrowly scoped read-only provider.
5. Compare the provider's snapshots with independent visual observations until board, active piece, NEXT, Hold, score and phase agree.
6. Implement the input adapter and action timing/receipt logging.
7. Connect the coordinator: read → plan → dispatch → re-read → reconcile → recover.
8. Validate complete games, score settlement, Game Over, restart and long-session stability.
9. Add GPU search only if measurements show CPU search is an actual bottleneck.

## Using memory snapshots

1. Run the game and select its window in the Inspector.
2. Choose **采集内存快照** and save a `.tpsnap` file for the current state.
3. Change one thing at a time where practical (for example, move the active piece once, or lock a piece and change the score), then capture another file. Keep the same game process running.
4. Choose **比较两份快照**, select the earlier and later files, and save the text difference report.
5. Repeat with different controlled changes. Comparing menu → active game, active piece → moved piece, and before → after a score change helps separate unrelated runtime churn from state-linked candidates.

The snapshot operation reads all committed, readable `MEM_PRIVATE` regions, including regions smaller than 16 KiB, plus readable/writable `MEM_IMAGE` pages belonging to modules beside the game executable. It excludes Windows system DLL images and read-only code/constant pages. Reads use 64 KiB chunks; unreadable portions are counted as skipped. The game process is not paused, so a snapshot is a short rolling sample rather than an atomic freeze-frame; capture while the relevant state is as stable as practical.

Snapshot files contain raw bytes from private process memory and writable game-module image pages; they may include unrelated runtime data. Keep them local and do not publish or share them. The difference report lists same-address changed bytes, their 4 KiB pages, allocation groups, and up to 1,000 contiguous changed ranges. It is a filtering aid, not proof that an address represents a board cell, piece, NEXT item, or score.

## Current scope

The deterministic board solver, TOP visual-piece profile, BFS reachability planner, client/action ports, session state machine, portable tests, and Windows process inspector are in place. The inspector remains a diagnostic utility, and the new reachability component is not yet connected to the live game capture/input loop. No in-process game-state addresses have been asserted or guessed.

## Build

On Windows 10 with Visual Studio 2022 + CMake:

```powershell
cmake -S tools/TetrisPilotNext -B build/TetrisPilotNext
cmake --build build/TetrisPilotNext --config Release
ctest --test-dir build/TetrisPilotNext -C Release --output-on-failure
```

Existing `TetrisVisionPilot` and `TetrisVisionPilotBenchmark` directories remain unchanged as diagnostic/comparison baselines.
