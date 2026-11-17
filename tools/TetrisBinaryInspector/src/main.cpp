#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <winnt.h>
#include <bcrypt.h>

#include <cstdint>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
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


bool ReadDiskFile(const std::wstring& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > 64 * 1024 * 1024) return false;
    input.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return input.good() || input.eof();
}

void AnalyzeDiskImage(const std::wstring& path) {
    std::vector<std::uint8_t> bytes;
    if (!ReadDiskFile(path, bytes)) {
        Log(L"Static PE analysis: could not read the validated executable.");
        return;
    }

    IMAGE_DOS_HEADER dos{};
    if (bytes.size() < sizeof(dos)) {
        Log(L"Static PE analysis: file too small for DOS header.");
        return;
    }
    std::memcpy(&dos, bytes.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
        Log(L"Static PE analysis: invalid DOS header.");
        return;
    }

    const std::size_t ntOffset = static_cast<std::size_t>(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    IMAGE_OPTIONAL_HEADER32 optionalHeader{};
    const std::size_t fileHeaderOffset = ntOffset + sizeof(DWORD);
    const std::size_t optionalOffset = fileHeaderOffset + sizeof(IMAGE_FILE_HEADER);
    if (ntOffset > bytes.size() || fileHeaderOffset > bytes.size() ||
        optionalOffset > bytes.size() ||
        sizeof(optionalHeader) > bytes.size() - optionalOffset ||
        sizeof(fileHeader) > bytes.size() - fileHeaderOffset ||
        sizeof(signature) > bytes.size() - ntOffset) {
        Log(L"Static PE analysis: truncated PE headers.");
        return;
    }

    std::memcpy(&signature, bytes.data() + ntOffset, sizeof(signature));
    std::memcpy(&fileHeader, bytes.data() + fileHeaderOffset, sizeof(fileHeader));
    std::memcpy(&optionalHeader, bytes.data() + optionalOffset, sizeof(optionalHeader));
    if (signature != IMAGE_NT_SIGNATURE ||
        fileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        optionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        Log(L"Static PE analysis: target is not PE32/I386.");
        return;
    }

    Log(L"");
    Log(L"=== Static PE32 analysis (exact-hash file) ===");
    Log(L"COFF TimeDateStamp: " + Hex(fileHeader.TimeDateStamp));
    Log(L"NumberOfSections: " + std::to_wstring(fileHeader.NumberOfSections));
    Log(L"Characteristics: " + Hex(fileHeader.Characteristics));
    Log(L"Preferred ImageBase: " + Hex(optionalHeader.ImageBase));
    Log(L"SectionAlignment: " + Hex(optionalHeader.SectionAlignment));
    Log(L"FileAlignment: " + Hex(optionalHeader.FileAlignment));
    Log(L"SizeOfHeaders: " + std::to_wstring(optionalHeader.SizeOfHeaders));
    Log(L"Subsystem: " + std::to_wstring(optionalHeader.Subsystem));
    Log(L"DllCharacteristics: " + Hex(optionalHeader.DllCharacteristics));
    Log(L"BaseOfCode RVA: " + Hex(optionalHeader.BaseOfCode));
    Log(L"BaseOfData RVA: " + Hex(optionalHeader.BaseOfData));
    Log(L"");

    const std::size_t sectionTable =
        optionalOffset + fileHeader.SizeOfOptionalHeader;
    if (sectionTable > bytes.size() ||
        static_cast<std::size_t>(fileHeader.NumberOfSections) >
            (bytes.size() - sectionTable) / sizeof(IMAGE_SECTION_HEADER)) {
        Log(L"Static PE analysis: section table is truncated.");
        return;
    }

    std::vector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
    if (!sections.empty()) {
        std::memcpy(sections.data(), bytes.data() + sectionTable,
                    sections.size() * sizeof(IMAGE_SECTION_HEADER));
    }

    auto rvaToOffset = [&](std::uint32_t rva, std::size_t& offset) -> bool {
        if (rva < optionalHeader.SizeOfHeaders && rva < bytes.size()) {
            offset = rva;
            return true;
        }
        for (const auto& section : sections) {
            const std::uint32_t span =
                (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);
            if (rva < section.VirtualAddress ||
                rva - section.VirtualAddress >= span) {
                continue;
            }
            const std::uint32_t delta = rva - section.VirtualAddress;
            if (delta >= section.SizeOfRawData) return false;
            const std::uint64_t raw =
                static_cast<std::uint64_t>(section.PointerToRawData) + delta;
            if (raw >= bytes.size()) return false;
            offset = static_cast<std::size_t>(raw);
            return true;
        }
        return false;
    };

    Log(L"Sections:");
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const auto& section = sections[i];
        std::string name;
        for (char ch : section.Name) {
            if (ch == '\0') break;
            name.push_back(ch);
        }
        Log(L"  " + std::to_wstring(i) + L" " +
            std::wstring(name.begin(), name.end()) +
            L" RVA=" + Hex(section.VirtualAddress) +
            L" VSize=" + Hex(section.Misc.VirtualSize) +
            L" Raw=" + Hex(section.PointerToRawData) +
            L"/" + Hex(section.SizeOfRawData) +
            L" Flags=" + Hex(section.Characteristics));
    }

    Log(L"");
    Log(L"Imported modules and symbols:");
    const auto& importDir = optionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    std::size_t totalImports = 0;
    if (importDir.VirtualAddress == 0 || importDir.Size == 0) {
        Log(L"  No import directory.");
    } else {
        std::size_t descriptorOffset = 0;
        if (!rvaToOffset(importDir.VirtualAddress, descriptorOffset)) {
            Log(L"  Import directory RVA cannot be mapped to file data.");
        } else {
            bool descriptorsEnded = false;
            for (std::size_t d = 0; d < 256; ++d) {
                const std::size_t off =
                    descriptorOffset + d * sizeof(IMAGE_IMPORT_DESCRIPTOR);
                if (off > bytes.size() ||
                    sizeof(IMAGE_IMPORT_DESCRIPTOR) > bytes.size() - off) break;
                IMAGE_IMPORT_DESCRIPTOR descriptor{};
                std::memcpy(&descriptor, bytes.data() + off, sizeof(descriptor));
                if (descriptor.Name == 0) {
                    descriptorsEnded = true;
                    break;
                }

                std::size_t dllOffset = 0;
                std::string dllName;
                if (rvaToOffset(descriptor.Name, dllOffset)) {
                    for (std::size_t n = 0; n < 260 &&
                         dllOffset + n < bytes.size(); ++n) {
                        const char ch = static_cast<char>(bytes[dllOffset + n]);
                        if (ch == '\0') break;
                        dllName.push_back(ch);
                    }
                }
                Log(L"  DLL: " + std::wstring(dllName.begin(), dllName.end()));

                const std::uint32_t thunkRva = descriptor.OriginalFirstThunk
                    ? descriptor.OriginalFirstThunk : descriptor.FirstThunk;
                bool thunkMapped = false;
                std::size_t thunkOffset = 0;
                thunkMapped = rvaToOffset(thunkRva, thunkOffset);
                if (!thunkMapped) {
                    Log(L"    (thunk array not file-backed)");
                    continue;
                }

                std::size_t moduleImports = 0;
                for (std::size_t t = 0; t < 1024; ++t) {
                    const std::size_t itemOffset = thunkOffset + t * sizeof(std::uint32_t);
                    if (itemOffset > bytes.size() ||
                        sizeof(std::uint32_t) > bytes.size() - itemOffset) break;
                    std::uint32_t thunk = 0;
                    std::memcpy(&thunk, bytes.data() + itemOffset, sizeof(thunk));
                    if (thunk == 0) break;

                    if ((thunk & 0x80000000u) != 0) {
                        Log(L"    Ordinal #" + std::to_wstring(thunk & 0xffffu));
                    } else {
                        std::size_t nameOffset = 0;
                        std::string functionName;
                        if (rvaToOffset(thunk, nameOffset) &&
                            nameOffset + sizeof(WORD) < bytes.size()) {
                            for (std::size_t n = nameOffset + sizeof(WORD);
                                 n < bytes.size() && n - nameOffset < 512; ++n) {
                                const char ch = static_cast<char>(bytes[n]);
                                if (ch == '\0') break;
                                functionName.push_back(ch);
                            }
                        }
                        if (!functionName.empty()) {
                            Log(L"    " + std::wstring(functionName.begin(), functionName.end()));
                        } else {
                            Log(L"    (unmapped import name RVA " + Hex(thunk) + L")");
                        }
                    }
                    ++moduleImports;
                    ++totalImports;
                    if (totalImports >= 1200) {
                        Log(L"  Import output capped at 1200 symbols.");
                        break;
                    }
                }
                Log(L"    Count: " + std::to_wstring(moduleImports));
                if (totalImports >= 1200) break;
            }
            if (!descriptorsEnded)
                Log(L"  WARNING: import descriptor list did not terminate within bounds.");
        }
    }
    Log(L"Total imported symbols reported: " + std::to_wstring(totalImports));

    Log(L"");
    Log(L"Relevant printable ASCII strings (file offsets):");
    const char* keywords[] = {
        "tetris", "game", "score", "next", "piece", "block", "board",
        "line", "level", "drop", "rotate", "marathon", "challenge",
        "menu", "play", "grid", "combo", "hold", "start", "clear",
        "time", "lock", "keyboard", "input", "battle"
    };
    std::size_t reportedStrings = 0;
    for (std::size_t i = 0; i < bytes.size() && reportedStrings < 120;) {
        if (bytes[i] < 0x20 || bytes[i] > 0x7e) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < bytes.size() && bytes[i] >= 0x20 && bytes[i] <= 0x7e &&
               i - start < 256) ++i;
        if (i - start < 5) continue;
        std::string value(reinterpret_cast<const char*>(bytes.data() + start), i - start);
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        bool relevant = false;
        for (const char* keyword : keywords) {
            if (lower.find(keyword) != std::string::npos) {
                relevant = true;
                break;
            }
        }
        if (!relevant) continue;
        Log(L"  File+" + Hex(start) + L": " +
            std::wstring(value.begin(), value.end()));
        ++reportedStrings;
    }
    if (reportedStrings == 0)
        Log(L"  No matching printable ASCII strings found.");
    else if (reportedStrings == 120)
        Log(L"  String output capped at 120 matches.");

    Log(L"=== End static PE32 analysis ===");
    Log(L"");
}

void EnumerateLoadedModules(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Log(L"Loaded module enumeration failed: " + Win32Error(GetLastError()));
        return;
    }

    Log(L"");
    Log(L"Loaded modules (first 96):");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::size_t count = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            Log(L"  " + std::wstring(entry.szModule) +
                L" base=" + Hex(reinterpret_cast<std::uintptr_t>(entry.modBaseAddr)) +
                L" size=" + std::to_wstring(entry.modBaseSize));
            ++count;
            if (count >= 96) {
                Log(L"  Module output capped at 96 entries.");
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    Log(L"Total modules reported: " + std::to_wstring(count));
    CloseHandle(snapshot);
}

void LogImageMemoryRegions(HANDLE process, const ModuleInfo& module) {
    Log(L"");
    Log(L"Committed image memory regions:");
    std::uintptr_t cursor = module.base;
    const std::uintptr_t end = module.base + module.size;
    std::size_t count = 0;
    while (cursor < end && count < 96) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(cursor),
                           &info, sizeof(info)) == 0) {
            Log(L"  VirtualQueryEx stopped: " + Win32Error(GetLastError()));
            break;
        }
        const std::uintptr_t regionBase =
            reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uintptr_t next = regionBase + info.RegionSize;
        if (info.State == MEM_COMMIT) {
            Log(L"  Base=" + Hex(regionBase) +
                L" Size=" + Hex(info.RegionSize) +
                L" Protect=" + Hex(info.Protect) +
                L" Type=" + Hex(info.Type));
            ++count;
        }
        if (next <= cursor) break;
        cursor = next;
    }
    Log(L"Committed image regions reported: " + std::to_wstring(count));
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
    Log(L"Remote PE Entry VA: " + Hex(module.base + optionalHeader.AddressOfEntryPoint));
    Log(L"Remote PE ImageBase (preferred): " + Hex(optionalHeader.ImageBase));
    Log(L"Remote PE DllCharacteristics: " + Hex(optionalHeader.DllCharacteristics));
    std::array<std::uint8_t, 48> entryBytes{};
    SIZE_T entryRead = 0;
    if (ReadProcessMemory(process,
        reinterpret_cast<LPCVOID>(module.base + optionalHeader.AddressOfEntryPoint),
        entryBytes.data(), entryBytes.size(), &entryRead) && entryRead > 0) {
        std::ostringstream bytesText;
        bytesText << std::uppercase << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < entryRead; ++i) {
            if (i) bytesText << ' ';
            bytesText << std::setw(2) << static_cast<unsigned>(entryBytes[i]);
        }
        const std::string raw = bytesText.str();
        Log(L"Remote entry bytes: " + std::wstring(raw.begin(), raw.end()));
    } else {
        Log(L"Remote entry bytes: unreadable (" + Win32Error(GetLastError()) + L")");
    }
    Log(L"Remote module base: " + Hex(module.base));
    Log(L"Remote module size (Toolhelp): " + std::to_wstring(module.size));
    if (optionalHeader.SizeOfImage != module.size) {
        Log(L"WARNING: PE SizeOfImage differs from Toolhelp module size; offsets stay disabled.");
        return false;
    }
    return true;
}

int RunInspector(DWORD requestedPid) {
    Log(L"TetrisBinaryInspector 0.2");
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
    AnalyzeDiskImage(imagePath);

    ModuleInfo module;
    if (!FindMainModule(pid, module)) {
        Log(L"ERROR: could not locate tetris.exe in the target module list.");
        CloseHandle(process);
        return 10;
    }
    Log(L"Loaded module path: " + module.path);
    EnumerateLoadedModules(pid);

    const bool peOk = ValidateRemotePe32(process, module);
    if (peOk) LogImageMemoryRegions(process, module);
    CloseHandle(process);
    if (!peOk) {
        Log(L"RESULT: executable fingerprint matched, but loaded PE validation failed.");
        return 11;
    }

    Log(L"RESULT: target process and loaded x86 image validated.");
    Log(L"NOTE: no game-state addresses are guessed or scanned in this build.");
    Log(L"Static imports, sections, relevant strings, loaded modules and image pages have been reported.");
    Log(L"Next: map validated game-state structures from the exact image, then compare with live reads.");
    return 0;
}

int SelfTest() {
    Log(L"TetrisBinaryInspector 0.2 self-test");
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
            std::wcslen(kTargetExe) == 10 &&
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
