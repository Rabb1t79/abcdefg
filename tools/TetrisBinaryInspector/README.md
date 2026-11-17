# TetrisBinaryInspector

A 32-bit external EXE for identifying and validating the historical Tetris Online Poland (TOP) tetris.exe process.

This is deliberately a read-only first stage. It does not inject a DLL, write process memory, patch code, create a remote thread, or synthesize game input.

## Recorded target fingerprint

Project history records the following fingerprint for the TOP executable used by the earlier vision project:

- Image name: tetris.exe
- File size: 1,654,784 bytes
- SHA-256: 9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42
- Expected architecture: x86 / PE32

This fingerprint is historical evidence, not proof that every current TOP installation has the same executable. The inspector refuses remote PE inspection if the on-disk file does not match exactly.

## What version 0.1 does

1. Finds a running tetris.exe process, or accepts an explicit PID.
2. Opens it with only PROCESS_QUERY_INFORMATION and PROCESS_VM_READ.
3. Reads the executable path and architecture.
4. Computes SHA-256 of the on-disk executable.
5. Requires an exact file-size and SHA-256 match.
6. Locates the loaded tetris.exe module and reads its DOS/PE headers using ReadProcessMemory.
7. Confirms PE32 / x86 and checks the loaded image size.
8. Writes TetrisBinaryInspector-report.txt beside the EXE.

It does not scan arbitrary process memory and does not claim to know the board, active piece, queue, score, or game-state offsets yet.

## Build

Use Visual Studio 2022 Build Tools on Windows with the x86 C++ toolchain and Windows SDK:

    cmake -S tools/TetrisBinaryInspector -B build/TetrisBinaryInspector -A Win32
    cmake --build build/TetrisBinaryInspector --config Release
    ctest --test-dir build/TetrisBinaryInspector -C Release --output-on-failure

GitHub Actions builds the same Win32 target and uploads a portable ZIP.

## Run

Start TOP and enter an active game session, then run:

    TetrisBinaryInspector.exe

If more than one tetris.exe process is found:

    TetrisBinaryInspector.exe --pid 1234

The process must be accessible to the current Windows user. Run the inspector at the same integrity level as the game; do not elevate unnecessarily.

Attach TetrisBinaryInspector-report.txt when reporting the result.

## Next reverse-engineering stage

Only after the process and image are validated:

- recover static code/data references from the exact matching executable;
- validate candidate addresses against real read-only process observations;
- establish the board and active-piece data layout;
- document each address, type, ownership/lifetime and validation condition;
- then consider a separate controller layer.

No game-state offsets are included in this release because none have yet been established from the exact executable in this work session. Guessing offsets would make the first build look more complete while making its output untrustworthy.
