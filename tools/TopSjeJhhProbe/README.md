# TopSjeJhhProbe

A small Windows x86 utility for selectively inspecting the **Tetris Online Poland (TOP)** client resources used by the recorded `TO-P_CHS_05` build.

This tool is deliberately narrow. It is not a general Tetris archive extractor, does not inspect other games, and does not assume that interfaces from standalone AI projects are exported by the TOP client.

## Pinned target

Before opening any archives, the program verifies the `tetris.exe` located in the supplied TOP directory against the exact executable previously validated for this project:

- File name: `tetris.exe`
- File size: `1,654,784` bytes
- SHA-256: `9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42`
- Architecture recorded for this executable: x86 / PE32

If either file size or SHA-256 differs, the program stops and does not inspect the archives. This is intentional; it must not silently switch to a different TOP build.

## What it reads

The program only looks for `.JHH` files beneath these two directories in the supplied client folder:

- `ai\`
- `config\`

For each archive it parses the TOP SJE.JHH index, then extracts only:

- `*.lua` entries from archives whose internal folder identifier begins with `ai`
- `*.ini` entries from archives whose internal folder identifier begins with `config`

No other archive members are extracted. It does not attempt to unpack the full client, graphics, audio, account data, server data, or unrelated resources.

The archive layout and encryption handling were independently implemented using the TOP-specific SJE.JHH format documented by [BSoD-Ultimate/SjeJhhUtil](https://github.com/BSoD-Ultimate/SjeJhhUtil). That project identifies its archive format as belonging to the TOP client. TOPLauncher also documents reading `config/SJE.JHH` and `config_jpn.ini` from the TOP client. The files actually found in your CHS_05 installation remain the authoritative evidence for this project.

## Safety and scope

- Opens the TOP executable and archive files read-only.
- Does not open or manipulate a running game process.
- Does not inject a DLL, create a remote thread, patch code, or write process memory.
- Does not change, replace, or repackage any original TOP client file.
- Writes selected decrypted copies and a report to a separate output directory.
- Rejects an output directory that resolves inside the TOP client folder, so extracted files cannot be written into the installed client tree.
- Applies bounds checks to archive index records, file offsets, entry lengths, and output paths.
- Refuses to run against a `tetris.exe` with a different fingerprint.

The extracted Lua and INI files are installed-resource evidence only. Their presence does not prove that the running TOP executable loads a particular file or exports the functions named inside it. This utility does not infer runtime game-state APIs or memory offsets.

## Run

Open a Command Prompt or PowerShell window and run:

```powershell
TopSjeJhhProbe.exe --root "D:\TO-P_CHS_05"
```

To choose a separate output location:

```powershell
TopSjeJhhProbe.exe --root "D:\TO-P_CHS_05" --out "D:\TOP-SJEJHH-Selected"
```

By default, the output directory is created next to the client folder:

```text
D:\TOP-SJEJHH-Selected\
```

The directory structure preserves the archive's relative location, for example:

```text
TOP-SJEJHH-Selected\
  ai\
    <archive-name>.JHH.contents\
      <selected Lua files>
  config\
    <archive-name>.JHH.contents\
      <selected INI files>
  TopSjeJhhProbe-report.txt
```

The report lists each archive, its internal folder identifier, the number of indexed entries, selected members, and extracted members. Send the complete report first; only the small selected output files should be needed for the next source analysis.

## Self-test

Run:

```powershell
TopSjeJhhProbe.exe --self-test
```

The self-test creates a temporary synthetic SJE.JHH archive, checks the index parser, decrypts one TOP-style Lua payload, compares the result, and removes the temporary file.

## Build

The GitHub Actions workflow builds the 32-bit Windows executable and runs the self-test. No local Visual Studio installation is required to obtain the packaged artifact.

## Research references

- [BSoD-Ultimate/SjeJhhUtil](https://github.com/BSoD-Ultimate/SjeJhhUtil) — TOP-specific SJE.JHH format and cryptographic behavior.
- [BSoD-Ultimate/TOPLauncher](https://github.com/BSoD-Ultimate/TOPLauncher) — TOP launcher source; its game configuration code reads `config/SJE.JHH` and `config_jpn.ini`.

These references are used only for the TOP archive/config format. They do not establish the runtime layout of `TO-P_CHS_05`, and no memory offsets are guessed by this utility.
