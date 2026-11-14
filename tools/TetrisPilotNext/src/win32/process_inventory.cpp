#include "process_inventory.hpp"

#ifdef _WIN32
#include <tlhelp32.h>

#include <algorithm>
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
    EnumerateModules(processId, out);
    EnumerateImageRegions(process, out);
    CloseHandle(process);
    return true;
}

} // namespace tetris::win32
#endif
