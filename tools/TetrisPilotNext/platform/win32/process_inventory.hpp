#pragma once

#ifdef _WIN32
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tetris::win32 {

struct ModuleRecord {
    std::wstring name;
    std::wstring path;
    std::uintptr_t base{};
    std::uint64_t size{};
};

struct ImageRegionRecord {
    std::uintptr_t base{};
    std::uintptr_t allocationBase{};
    std::uint64_t size{};
    std::uint32_t protection{};
};

struct ProcessInventory {
    DWORD processId{};
    std::wstring executablePath;
    std::wstring architecture;
    std::uint64_t executableSize{};
    std::wstring executableSha256;
    std::wstring fingerprintError;
    std::vector<ModuleRecord> modules;
    std::vector<ImageRegionRecord> imageRegions;
    std::wstring moduleError;
    std::wstring memoryMapError;
};

// Metadata only: no raw game-memory reads or writes are performed.
bool InspectProcessMetadata(DWORD processId, ProcessInventory& out,
                            std::wstring& error);

} // namespace tetris::win32
#endif
