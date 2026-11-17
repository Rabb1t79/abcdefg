#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <winnt.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {
constexpr wchar_t kTargetExe[] = L"tetris.exe";
constexpr unsigned long long kExpectedFileSize = 1654784ULL;
constexpr char kExpectedSha256[] =
    "9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42";

std::vector<std::wstring> g_report;

void Log(const std::wstring& line) {
    g_report.push_back(line);
    std::wcout << line << L"\n";
}

void SaveReport() {
    HANDLE file = CreateFileW(L"TetrisBinaryInspector-report.txt", GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    for (const auto& line : g_report) {
        std::string text;
        text.reserve(line.size() + 2);
        for (wchar_t ch : line) {
            text.push_back(ch >= 0x20 && ch <= 0x7e ? static_cast<char>(ch) : '?');
        }
        text += "\r\n";
        DWORD written = 0;
        if (!WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                       &written, nullptr) || written != text.size()) break;
    }
    CloseHandle(file);
}

std::wstring Hex(std::uintptr_t value) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << value;
    return out.str();
}

std::wstring Win32Error(DWORD error) {
    return L"Win32 error " + std::to_wstring(error);
}

struct ProcessEntry {
    DWORD pid = 0;
    std::wstring name;
};

std::vector<ProcessEntry> FindTargetProcesses() {
    std::vector<ProcessEntry> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log(L"ERROR: process enumeration failed: " + Win32Error(GetLastError()));
        return found;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, kTargetExe) == 0)
                found.push_back({entry.th32ProcessID, entry.szExeFile});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool ParsePid(const wchar_t* value, DWORD& pid) {
    if (!value || !*value) return false;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value, &end, 10);
    if (!end || *end != L'\0' || parsed == 0 || parsed > MAXDWORD) return false;
    pid = static_cast<DWORD>(parsed);
    return true;
}

bool GetProcessArchitecture(HANDLE process, std::wstring& architecture) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto query2 = kernel
        ? reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(kernel, "IsWow64Process2"))
        : nullptr;

    if (query2) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (!query2(process, &processMachine, &nativeMachine)) return false;

        if (processMachine == IMAGE_FILE_MACHINE_I386)
            architecture = L"x86 (WOW64)";
        else if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN &&
                 nativeMachine == IMAGE_FILE_MACHINE_I386)
            architecture = L"x86 (native)";
        else if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN &&
                 nativeMachine == IMAGE_FILE_MACHINE_AMD64)
            architecture = L"x64";
        else
            architecture = L"other machine type";
        return true;
    }

    BOOL wow64 = FALSE;
    if (!IsWow64Process(process, &wow64)) return false;
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    if (wow64)
        architecture = L"x86 (WOW64)";
    else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        architecture = L"x64";
    else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        architecture = L"x86 (native)";
    else
        architecture = L"other architecture";
    return true;
}

std::wstring GetImagePath(HANDLE process) {
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) return L"";
    return std::wstring(buffer.data(), length);
}

struct FileHash {
    bool ok = false;
    unsigned long long size = 0;
    std::string sha256;
    std::wstring error;
};

std::string ToHex(const std::vector<UCHAR>& bytes) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (UCHAR byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

FileHash HashFileSha256(const std::wstring& path) {
    FileHash result;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.error = L"cannot open executable file: " + Win32Error(GetLastError());
        return result;
    }

    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0) {
        result.error = L"cannot query executable file size: " + Win32Error(GetLastError());
        CloseHandle(file);
        return result;
    }
    result.size = static_cast<unsigned long long>(fileSize.QuadPart);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, hashLength = 0, returned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = L"BCryptOpenAlgorithmProvider(SHA256) failed";
        CloseHandle(file);
        return result;
    }

    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = L"BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }

    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &returned, 0);
    if (!BCRYPT_SUCCESS(status) || hashLength != 32) {
        result.error = L"BCryptGetProperty(BCRYPT_HASH_LENGTH) failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }

    std::vector<UCHAR> hashObject(objectLength), digest(hashLength);
    status = BCryptCreateHash(algorithm, &hash, hashObject.data(),
        objectLength, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        result.error = L"BCryptCreateHash failed";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        CloseHandle(file);
        return result;
    }

    std::vector<UCHAR> buffer(1024 * 1024);
    bool readOk = true;
    for (;;) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                      &bytesRead, nullptr)) {
            result.error = L"ReadFile failed while hashing: " + Win32Error(GetLastError());
            readOk = false;
            break;
        }
        if (bytesRead == 0) break;
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
        if (!BCRYPT_SUCCESS(status)) {
            result.error = L"BCryptHashData failed";
            readOk = false;
            break;
        }
    }

    if (readOk) {
        status = BCryptFinishHash(hash, digest.data(), hashLength, 0);
        if (BCRYPT_SUCCESS(status)) {
            result.sha256 = ToHex(digest);
            result.ok = true;
        } else {
            result.error = L"BCryptFinishHash failed";
        }
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

struct ModuleInfo {
    std::uintptr_t base = 0;
    DWORD size = 0;
    std::wstring path;
};

bool FindMainModule(DWORD pid, ModuleInfo& module) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log(L"ERROR: module enumeration failed: " + Win32Error(GetLastError()));
        return false;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, kTargetExe) == 0) {
                module.base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                module.size = entry.modBaseSize;
                module.path = entry.szExePath;
                found = true;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

template <typename T>
bool ReadRemote(HANDLE process, std::uintptr_t address, T& value) {
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
        &value, sizeof(T), &bytesRead) && bytesRead == sizeof(T);
}

bool ValidateRemotePe32(HANDLE process, const ModuleInfo& module) {
    IMAGE_DOS_HEADER dos{};
    if (!ReadRemote(process, module.base, dos)) {
        Log(L"ERROR: cannot read remote DOS header: " + Win32Error(GetLastError()));
        return false;
    }
    if (dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) ||
        dos.e_lfanew > 1024 * 1024) {
        Log(L"ERROR: remote image has an invalid DOS header.");
        return false;
    }

    const std::uintptr_t ntAddress =
        module.base + static_cast<std::uintptr_t>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    IMAGE_OPTIONAL_HEADER32 optionalHeader{};
    if (!ReadRemote(process, ntAddress, signature) ||
        !ReadRemote(process, ntAddress + sizeof(DWORD), fileHeader) ||
        !ReadRemote(process, ntAddress + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER),
                    optionalHeader)) {
        Log(L"ERROR: cannot read remote PE headers: " + Win32Error(GetLastError()));
        return false;
    }

    if (signature != IMAGE_NT_SIGNATURE ||
        fileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
        optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        Log(L"ERROR: loaded image is not a valid PE32 x86 executable.");
        return false;
    }

    Log(L"Remote PE: PE32 / IMAGE_FILE_MACHINE_I386 confirmed.");
    Log(L"Remote PE SizeOfImage: " + std::to_wstring(optionalHeader.SizeOfImage));
    Log(L"Remote PE AddressOfEntryPoint RVA: " + Hex(optionalHeader.AddressOfEntryPoint));
    Log(L"Remote module base: " + Hex(module.base));
    Log(L"Remote module size (Toolhelp): " + std::to_wstring(module.size));
    if (optionalHeader.SizeOfImage != module.size) {
        Log(L"WARNING: PE SizeOfImage differs from Toolhelp module size; offsets stay disabled.");
        return false;
    }
    return true;
}

int RunInspector(DWORD requestedPid) {
    Log(L"TetrisBinaryInspector 0.1");
    Log(L"Mode: external process, read-only.");
    Log(L"Access rights: PROCESS_QUERY_INFORMATION | PROCESS_VM_READ.");
    Log(L"No process-memory write, injection, remote thread, or code patch APIs are used.");
    Log(L"Target image name: tetris.exe");
    Log(L"Expected file size: " + std::to_wstring(kExpectedFileSize));
    Log(L"Expected SHA256: " + std::wstring(kExpectedSha256, kExpectedSha256 + 64));
    Log(L"");

    DWORD pid = requestedPid;
    if (pid == 0) {
        const auto processes = FindTargetProcesses();
        if (processes.empty()) {
            Log(L"RESULT: no running tetris.exe process found.");
            Log(L"Start TOP and run this inspector again.");
            return 2;
        }
        if (processes.size() > 1) {
            Log(L"RESULT: more than one tetris.exe process found; specify a PID:");
            for (const auto& item : processes)
                Log(L"  PID " + std::to_wstring(item.pid) + L"  " + item.name);
            Log(L"Usage: TetrisBinaryInspector.exe --pid <PID>");
            return 3;
        }
        pid = processes.front().pid;
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) {
        Log(L"ERROR: OpenProcess failed: " + Win32Error(GetLastError()));
        return 4;
    }

    const std::wstring imagePath = GetImagePath(process);
    if (imagePath.empty()) {
        Log(L"ERROR: QueryFullProcessImageNameW failed: " + Win32Error(GetLastError()));
        CloseHandle(process);
        return 5;
    }
    Log(L"PID: " + std::to_wstring(pid));
    Log(L"Image path: " + imagePath);

    std::wstring architecture;
    if (!GetProcessArchitecture(process, architecture)) {
        Log(L"ERROR: process architecture query failed: " + Win32Error(GetLastError()));
        CloseHandle(process);
        return 6;
    }
    Log(L"Process architecture: " + architecture);
    if (architecture.find(L"x86") == std::wstring::npos) {
        Log(L"RESULT: architecture mismatch; only the known x86 target is accepted.");
        CloseHandle(process);
        return 7;
    }

    const FileHash hash = HashFileSha256(imagePath);
    if (!hash.ok) {
        Log(L"ERROR: " + hash.error);
        CloseHandle(process);
        return 8;
    }
    Log(L"On-disk file size: " + std::to_wstring(hash.size));
    Log(L"On-disk SHA256: " + std::wstring(hash.sha256.begin(), hash.sha256.end()));

    if (hash.size != kExpectedFileSize || hash.sha256 != kExpectedSha256) {
        Log(L"RESULT: UNKNOWN BUILD. Fingerprint mismatch; memory offsets remain disabled.");
        CloseHandle(process);
        return 9;
    }
    Log(L"Fingerprint: EXACT MATCH to the recorded target build.");

    ModuleInfo module;
    if (!FindMainModule(pid, module)) {
        Log(L"ERROR: could not locate tetris.exe in the target module list.");
        CloseHandle(process);
        return 10;
    }
    Log(L"Loaded module path: " + module.path);

    const bool peOk = ValidateRemotePe32(process, module);
    CloseHandle(process);
    if (!peOk) {
        Log(L"RESULT: executable fingerprint matched, but loaded PE validation failed.");
        return 11;
    }

    Log(L"RESULT: target process and loaded x86 image validated.");
    Log(L"NOTE: no game-state addresses are guessed or scanned in this build.");
    Log(L"Next: validate static disassembly against read-only live state samples.");
    return 0;
}

int SelfTest() {
    Log(L"TetrisBinaryInspector self-test");
    Log(sizeof(void*) == 4
        ? L"PASS: executable is compiled for 32-bit pointers."
        : L"FAIL: executable is not 32-bit.");
    Log(std::wcslen(kTargetExe) == 10
        ? L"PASS: target image name constant."
        : L"FAIL: target image name constant.");
    Log(std::strlen(kExpectedSha256) == 64
        ? L"PASS: expected SHA256 constant length."
        : L"FAIL: expected SHA256 constant length.");
    return (sizeof(void*) == 4 &&
            std::wcslen(kTargetExe) == 9 &&
            std::strlen(kExpectedSha256) == 64) ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    int exitCode = 0;
    if (argc == 2 && _wcsicmp(argv[1], L"--self-test") == 0) {
        exitCode = SelfTest();
    } else if (argc == 3 && _wcsicmp(argv[1], L"--pid") == 0) {
        DWORD pid = 0;
        if (!ParsePid(argv[2], pid)) {
            Log(L"Invalid PID.");
            Log(L"Usage: TetrisBinaryInspector.exe [--pid <PID>] [--self-test]");
            exitCode = 64;
        } else {
            exitCode = RunInspector(pid);
        }
    } else if (argc == 1) {
        exitCode = RunInspector(0);
    } else {
        Log(L"Usage: TetrisBinaryInspector.exe [--pid <PID>] [--self-test]");
        exitCode = 64;
    }
    SaveReport();
    return exitCode;
}
