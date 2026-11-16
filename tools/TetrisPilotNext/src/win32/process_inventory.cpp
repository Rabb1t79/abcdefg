#include "process_inventory.hpp"
#include "tetris/private_memory_policy.hpp"
#include "tetris/client_profile.hpp"
#include "tetris/memory_snapshot.hpp"

#ifdef _WIN32
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <limits>
#include <sstream>
#include <utility>
#include <unordered_map>

namespace tetris::win32 {
namespace {

std::wstring MachineLabel(USHORT machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386: return L"x86";
    case IMAGE_FILE_MACHINE_AMD64: return L"x64";
    case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
    case IMAGE_FILE_MACHINE_UNKNOWN: return L"native/default";
    default: {
        std::wostringstream stream;
        stream << L"machine 0x" << std::hex << machine;
        return stream.str();
    }
    }
}

std::wstring ProcessArchitecture(HANDLE process) {
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const auto fn = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (fn) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (fn(process, &processMachine, &nativeMachine)) {
            return processMachine == IMAGE_FILE_MACHINE_UNKNOWN
                ? MachineLabel(nativeMachine)
                : MachineLabel(processMachine) + L" on " + MachineLabel(nativeMachine);
        }
    }

    BOOL wow64 = FALSE;
    if (IsWow64Process(process, &wow64)) {
        if (wow64) return L"x86 under x64";
        SYSTEM_INFO info{};
        GetNativeSystemInfo(&info);
        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) return L"x64/native";
        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) return L"ARM64/native";
    }
    return L"unknown";
}


bool ComputeExecutableFingerprint(const std::wstring& path,
                                  std::uint64_t& fileSize,
                                  std::wstring& sha256,
                                  std::wstring& error) {
    fileSize = 0;
    sha256.clear();
    error.clear();

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"Cannot open executable for SHA-256: " + std::to_wstring(GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        error = L"GetFileSizeEx failed: " + std::to_wstring(GetLastError());
        CloseHandle(file);
        return false;
    }
    fileSize = static_cast<std::uint64_t>(size.QuadPart);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> hashObject;
    std::vector<UCHAR> digest;
    bool success = false;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        error = L"BCryptOpenAlgorithmProvider failed: " +
            std::to_wstring(static_cast<unsigned long>(status));
        CloseHandle(file);
        return false;
    }

    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
        &resultLength, 0);
    if (BCRYPT_SUCCESS(status)) {
        status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
            &resultLength, 0);
    }
    if (!BCRYPT_SUCCESS(status) || objectLength == 0 || hashLength == 0) {
        error = L"BCryptGetProperty failed: " +
            std::to_wstring(static_cast<unsigned long>(status));
    } else {
        hashObject.resize(objectLength);
        digest.resize(hashLength);
        status = BCryptCreateHash(algorithm, &hash, hashObject.data(),
            objectLength, nullptr, 0, 0);
        if (!BCRYPT_SUCCESS(status)) {
            error = L"BCryptCreateHash failed: " +
                std::to_wstring(static_cast<unsigned long>(status));
        } else {
            std::vector<UCHAR> buffer(1024 * 1024);
            for (;;) {
                DWORD bytesRead = 0;
                if (!ReadFile(file, buffer.data(),
                    static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
                    error = L"ReadFile failed: " + std::to_wstring(GetLastError());
                    break;
                }
                if (bytesRead == 0) {
                    status = BCryptFinishHash(hash, digest.data(), hashLength, 0);
                    if (!BCRYPT_SUCCESS(status)) {
                        error = L"BCryptFinishHash failed: " +
                            std::to_wstring(static_cast<unsigned long>(status));
                        break;
                    }
                    std::wostringstream formatted;
                    formatted << std::uppercase << std::hex << std::setfill(L'0');
                    for (UCHAR byte : digest) {
                        formatted << std::setw(2) << static_cast<unsigned int>(byte);
                    }
                    sha256 = formatted.str();
                    success = true;
                    break;
                }
                status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
                if (!BCRYPT_SUCCESS(status)) {
                    error = L"BCryptHashData failed: " +
                        std::to_wstring(static_cast<unsigned long>(status));
                    break;
                }
            }
        }
    }

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return success;
}


std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"Snapshot operation failed.";
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), count);
    return output;
}

void EnumerateModules(DWORD processId, ProcessInventory& out) {
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        out.moduleError = L"CreateToolhelp32Snapshot failed: " + std::to_wstring(GetLastError());
        return;
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module)) {
        do {
            ModuleRecord record;
            record.name = module.szModule;
            record.path = module.szExePath;
            record.base = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            record.size = module.modBaseSize;
            out.modules.push_back(std::move(record));
            module.dwSize = sizeof(module);
        } while (Module32NextW(snapshot, &module));
    } else {
        out.moduleError = L"Module32FirstW failed: " + std::to_wstring(GetLastError());
    }
    CloseHandle(snapshot);
}

void EnumerateMemoryRegions(HANDLE process, ProcessInventory& out) {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const std::uintptr_t maximum = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    std::size_t guard = 0;

    while (cursor < maximum && guard++ < 100000) {
        MEMORY_BASIC_INFORMATION mbi{};
        const SIZE_T queried = VirtualQueryEx(
            process, reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));
        if (!queried) {
            const DWORD code = GetLastError();
            if (code != ERROR_INVALID_PARAMETER) {
                out.memoryMapError = L"VirtualQueryEx stopped: " + std::to_wstring(code);
            }
            break;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto allocationBase = reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
        const auto next = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT) {
            const auto size = static_cast<std::uint64_t>(mbi.RegionSize);
            const auto protection = static_cast<std::uint32_t>(mbi.Protect);
            if (mbi.Type == MEM_IMAGE) {
                out.imageRegions.push_back(ImageRegionRecord{
                    base, allocationBase, size, protection
                });
            } else if (mbi.Type == MEM_PRIVATE) {
                ++out.privateRegionCount;
                out.privateCommittedBytes += size;
                if (tetris::IsReadableMemoryProtection(protection)) {
                    ++out.readablePrivateRegionCount;
                    out.readablePrivateBytes += size;
                    if (size >= tetris::kMinimumListedPrivateRegionSize) {
                        out.readablePrivateRegions.push_back(PrivateRegionRecord{
                            base, allocationBase, size, protection
                        });
                    } else {
                        ++out.filteredReadablePrivateRegionCount;
                    }
                }
            }
        }
        if (next <= cursor) break;
        cursor = next;
    }

    if (guard >= 100000 && cursor < maximum && out.memoryMapError.empty()) {
        out.memoryMapError = L"VirtualQueryEx safety limit reached.";
    }
}

} // namespace

bool InspectProcessMetadata(DWORD processId, ProcessInventory& out,
                            std::wstring& error) {
    out = {};
    error.clear();
    out.processId = processId;
    if (processId == 0) {
        error = L"Invalid process ID.";
        return false;
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION,
        FALSE, processId);
    if (!process) {
        error = L"OpenProcess failed: " + std::to_wstring(GetLastError());
        return false;
    }

    std::vector<wchar_t> path(32768, L'\0');
    DWORD pathLength = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &pathLength)) {
        out.executablePath.assign(path.data(), pathLength);
    } else {
        error = L"QueryFullProcessImageNameW failed: " + std::to_wstring(GetLastError());
        CloseHandle(process);
        return false;
    }

    out.architecture = ProcessArchitecture(process);
    ComputeExecutableFingerprint(out.executablePath, out.executableSize,
        out.executableSha256, out.fingerprintError);
    EnumerateModules(processId, out);
    EnumerateMemoryRegions(process, out);
    CloseHandle(process);
    return true;
}


bool CaptureReadableGameMemorySnapshot(DWORD processId,
    const std::wstring& path, std::wstring& error) {
    error.clear();
    if (processId == 0) {
        error = L"无效的进程 ID。";
        return false;
    }

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        FALSE, processId);
    if (!process) {
        error = L"OpenProcess 失败：" + std::to_wstring(GetLastError());
        return false;
    }

    auto fail = [&](const std::wstring& message) {
        error = message;
        CloseHandle(process);
        return false;
    };

    std::vector<wchar_t> executablePath(32768, L'\0');
    DWORD pathLength = static_cast<DWORD>(executablePath.size());
    if (!QueryFullProcessImageNameW(process, 0, executablePath.data(), &pathLength))
        return fail(L"QueryFullProcessImageNameW 失败：" + std::to_wstring(GetLastError()));

    const std::wstring architecture = ProcessArchitecture(process);
    std::uint64_t executableSize = 0;
    std::wstring executableSha256;
    std::wstring fingerprintError;
    if (!ComputeExecutableFingerprint(
        std::wstring(executablePath.data(), pathLength),
        executableSize, executableSha256, fingerprintError)) {
        return fail(L"无法验证客户端指纹：" + fingerprintError);
    }

    const auto fingerprint = CheckTargetClientFingerprint(
        executableSize, executableSha256, architecture);
    if (fingerprint != ClientFingerprintStatus::Match) {
        return fail(L"安全拒绝：目标 EXE 大小、架构或 SHA-256 与锁定客户端不匹配。");
    }

    // Include mapped writable data only from modules installed beside this exact
    // executable. This captures game-owned globals (for example .data sections)
    // without dumping unrelated Windows system DLLs.
    ProcessInventory moduleInventory;
    EnumerateModules(processId, moduleInventory);
    if (moduleInventory.modules.empty()) {
        return fail(L"无法枚举目标进程模块，拒绝生成不完整的游戏映像快照。");
    }

    const auto executableFullPath =
        std::filesystem::path(std::wstring(executablePath.data(), pathLength))
            .lexically_normal().wstring();
    const auto executableDirectory =
        std::filesystem::path(executableFullPath).parent_path().lexically_normal().wstring();
    std::unordered_map<std::uintptr_t, std::string> gameModuleNames;
    bool executableModuleFound = false;
    for (const auto& module : moduleInventory.modules) {
        const auto moduleDirectory =
            std::filesystem::path(module.path).parent_path().lexically_normal().wstring();
        if (CompareStringOrdinal(executableDirectory.c_str(), -1,
                moduleDirectory.c_str(), -1, TRUE) != CSTR_EQUAL) {
            continue;
        }

        gameModuleNames.emplace(module.base, ToUtf8(module.name));
        const auto moduleFullPath =
            std::filesystem::path(module.path).lexically_normal().wstring();
        if (CompareStringOrdinal(moduleFullPath.c_str(), -1,
                executableFullPath.c_str(), -1, TRUE) == CSTR_EQUAL) {
            executableModuleFound = true;
        }
    }
    if (!executableModuleFound) {
        return fail(L"已验证 EXE 指纹，但未在模块表中找到对应的游戏 EXE 映像。");
    }

    MemorySnapshotFile snapshot;
    snapshot.processId = processId;
    snapshot.executableSize = executableSize;
    snapshot.architecture = ToUtf8(architecture);
    snapshot.executableSha256 = ToUtf8(executableSha256);

    FILETIME fileTime{};
    GetSystemTimeAsFileTime(&fileTime);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    constexpr std::uint64_t kWindowsToUnixEpoch100ns = 116444736000000000ull;
    if (ticks.QuadPart >= kWindowsToUnixEpoch100ns) {
        snapshot.capturedAtUnixMs =
            (ticks.QuadPart - kWindowsToUnixEpoch100ns) / 10000ull;
    }

    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    std::uintptr_t cursor =
        reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
    const std::uintptr_t maximum =
        reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
    std::size_t queryCount = 0;
    constexpr std::size_t kMaximumQueries = 1000000;
    constexpr std::uint64_t kReadChunkBytes = 64ull * 1024ull;

    while (cursor < maximum && queryCount++ < kMaximumQueries) {
        MEMORY_BASIC_INFORMATION mbi{};
        const SIZE_T queried = VirtualQueryEx(
            process, reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));
        if (!queried) {
            const DWORD code = GetLastError();
            if (code != ERROR_INVALID_PARAMETER) {
                return fail(L"VirtualQueryEx 失败：" + std::to_wstring(code));
            }
            break;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto allocationBase =
            reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
        const auto next = base + mbi.RegionSize;

        const auto protection = static_cast<std::uint32_t>(mbi.Protect);
        const auto moduleIt = gameModuleNames.find(allocationBase);
        const bool readable = IsReadableMemoryProtection(protection);
        const bool privateRegion = mbi.Type == MEM_PRIVATE && readable;
        const bool gameImageRegion = mbi.Type == MEM_IMAGE &&
            moduleIt != gameModuleNames.end() &&
            readable && IsWritableMemoryProtection(protection);

        if (mbi.State == MEM_COMMIT && (privateRegion || gameImageRegion)) {
            const auto regionSize = static_cast<std::uint64_t>(mbi.RegionSize);
            snapshot.eligibleBytes += regionSize;
            std::uint64_t offset = 0;
            while (offset < regionSize) {
                const auto remaining = regionSize - offset;
                const auto request64 = (std::min)(remaining, kReadChunkBytes);
                const SIZE_T request = static_cast<SIZE_T>(request64);
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(request));
                SIZE_T bytesRead = 0;
                const auto address = base + static_cast<std::uintptr_t>(offset);
                ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address),
                    bytes.data(), request, &bytesRead);

                if (bytesRead > 0) {
                    bytes.resize(static_cast<std::size_t>(bytesRead));
                    MemorySnapshotSpan span;
                    span.base = static_cast<std::uint64_t>(address);
                    span.allocationBase = static_cast<std::uint64_t>(allocationBase);
                    span.protection = protection;
                    span.regionKind = privateRegion
                        ? MemoryRegionKind::Private : MemoryRegionKind::Image;
                    if (gameImageRegion) span.moduleName = moduleIt->second;
                    span.bytes = std::move(bytes);
                    snapshot.capturedBytes += static_cast<std::uint64_t>(bytesRead);
                    snapshot.spans.push_back(std::move(span));
                }
                if (bytesRead < request) {
                    snapshot.skippedBytes +=
                        static_cast<std::uint64_t>(request - bytesRead);
                }
                offset += request64;
            }
        }

        if (next <= cursor) break;
        cursor = next;
    }

    if (queryCount >= kMaximumQueries && cursor < maximum)
        return fail(L"VirtualQueryEx 达到安全查询上限，采集未完整完成。");
    CloseHandle(process);

    if (snapshot.spans.empty()) {
        error = L"没有成功读取到任何可读私有内存区域。";
        return false;
    }

    std::string writeError;
    if (!WriteMemorySnapshotFile(std::filesystem::path(path), snapshot, writeError)) {
        error = FromUtf8(writeError);
        return false;
    }
    return true;
}

bool CompareSnapshotFiles(const std::wstring& beforePath,
    const std::wstring& afterPath, const std::wstring& reportPath,
    std::wstring& error) {
    error.clear();
    MemorySnapshotDiffSummary summary;
    std::string compareError;
    if (!CompareMemorySnapshotFiles(
        std::filesystem::path(beforePath),
        std::filesystem::path(afterPath),
        std::filesystem::path(reportPath), summary, compareError)) {
        error = FromUtf8(compareError);
        return false;
    }
    return true;
}


} // namespace tetris::win32
#endif
