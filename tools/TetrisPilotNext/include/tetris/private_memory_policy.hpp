#pragma once

#include <cstdint>

namespace tetris {

// Keep the detailed report useful without dumping thousands of tiny pages.
inline constexpr std::uint64_t kMinimumListedPrivateRegionSize = 16ull * 1024ull;

// Windows PAGE_* values. Guarded and no-access pages are deliberately excluded.
inline bool IsReadableMemoryProtection(std::uint32_t protection) noexcept {
    constexpr std::uint32_t kPageGuard = 0x100u;
    if ((protection & kPageGuard) != 0) return false;

    switch (protection & 0xFFu) {
    case 0x02u: // PAGE_READONLY
    case 0x04u: // PAGE_READWRITE
    case 0x08u: // PAGE_WRITECOPY
    case 0x20u: // PAGE_EXECUTE_READ
    case 0x40u: // PAGE_EXECUTE_READWRITE
    case 0x80u: // PAGE_EXECUTE_WRITECOPY
        return true;
    default:
        return false;
    }
}

} // namespace tetris
