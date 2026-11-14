#include "process_inventory.hpp"

#ifdef _WIN32
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

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
            std::array<UCHAR, 1024 * 1024> buffer{};
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

void EnumerateImageRegions(HANDLE process, ProcessInventory& out) {
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
        const auto next = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE) {
            out.imageRegions.push_back(ImageRegionRecord{
                base,
                reinterpret_cast<std::uintptr_t>(mbi.AllocationBase),
                static_cast<std::uint64_t>(mbi.RegionSize),
                static_cast<std::uint32_t>(mbi.Protect)
            });
        }
        if (next <= cursor) break;
        cursor = next;
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
    EnumerateImageRegions(process, out);
    CloseHandle(process);
    return true;
}

} // namespace tetris::win32
#endif
