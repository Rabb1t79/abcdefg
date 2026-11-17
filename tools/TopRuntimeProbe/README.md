# TopRuntimeProbe

Read-only external Windows x86 utility for validating the static `TO-P_CHS_05` board-object chain and exporting one 10x40 occupancy snapshot.

This is a diagnostic probe, not the final Tetris AI reader. It intentionally does not infer current-piece, rotation, NEXT, HOLD, Game Over, animation, or input-state offsets.

## Pinned executable

The probe refuses to scan unless all of the following match the uploaded client already analyzed for this project:

- Image name: `tetris.exe`
- File size: `1,654,784` bytes
- SHA-256: `9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42`
- Loaded image size: `0x0019D000`
- Process architecture: x86 / PE32

The module base is obtained from the target process at runtime. The probe adds the statically identified RVAs to that base instead of assuming a fixed VA.

## What it validates

The probe scans committed readable memory for an object whose first DWORD equals the `CTetrisLuaAI` vtable (`moduleBase + 0x00135C7C`). It then requires this complete chain:

```text
CTetrisLuaAI
  +0x114 -> CGamePlay*   (vtable RVA 0x00138104)
      +0x04 -> CPlayfield* (vtable RVA 0x0013811C)
      +0x08 -> CPlayer*    (vtable RVA 0x0013810C)

CPlayfield
  +0x08 -> width  (must be 10)
  +0x0C -> height (must be 40)
  +0x1C -> cell-record vector begin
  +0x20 -> cell-record vector end
  +0x24 -> cell-record vector capacity
```

It requires the vector length to equal `width * height * 0x14`, and checks that the first and last occupancy fields can be read.

Each cell record is 20 bytes. The DWORD at record offset `+0x0C` is interpreted according to the static snapshot-copy code:

- `0` = occupied
- nonzero = empty

The program prints and saves a 10x40 grid using `#` for occupied and `.` for empty. Rows are shown in raw memory order (`y=0` to `y=39`). The visual top/bottom orientation has not yet been dynamically verified; the probe does not flip rows or silently assume an origin.

## Run

1. Start the exact `TO-P_CHS_05` client.
2. Enter a gameplay state so the AI/gameplay objects have had a chance to be created.
3. Open PowerShell in the extracted probe folder and run:

```powershell
.\\TopRuntimeProbe.exe
```

If multiple `tetris.exe` processes are running:

```powershell
.\\TopRuntimeProbe.exe --list
.\\TopRuntimeProbe.exe --pid 1234
```

The result is also written to `TopRuntimeProbe-report.txt` in the current directory. Send the console output and this report back for verification against the visible board.

## Access and safety restrictions

- Uses `OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, ...)` only.
- Uses `VirtualQueryEx` and `ReadProcessMemory` for discovery and reads.
- Does not request `PROCESS_VM_WRITE` or `PROCESS_VM_OPERATION`.
- Does not call `WriteProcessMemory`, create remote threads, inject DLLs, patch code, or modify target memory.
- Does not invoke the target process's Lua functions.
- Refuses to run on a different executable fingerprint.
- If more than one candidate passes all checks, it refuses to choose one.

The probe needs sufficient permission to read the game process. Normally run it under the same Windows account and integrity level as the client; do not elevate unnecessarily.

## Validation status

The current object offsets and the cell-record decoding are static-analysis findings from the uploaded exact executable. The program's self-test covers the local 10x40 / 20-byte / `+0x0C` decoder only. It does not emulate a game process and cannot prove runtime validity in CI. The first run against the user's live game is the required dynamic validation step.

## Build

GitHub Actions builds a Win32/x86 package and runs the decoder self-test. The tool is independent of the TOP client and does not require local Visual Studio installation to obtain the CI artifact.
