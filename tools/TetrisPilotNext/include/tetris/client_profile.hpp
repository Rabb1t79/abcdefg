#pragma once

#include <cstdint>
#include <string_view>

namespace tetris {

// This profile is pinned to the executable reported by the target Windows
// client inspector. The installation path is deliberately not part of the
// identity check: the same exact executable may be installed elsewhere.
inline constexpr std::uint64_t kTargetExecutableSize = 1654784ull;
inline constexpr wchar_t kTargetExecutableSha256[] =
    L"9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42";
inline constexpr wchar_t kTargetProcessArchitecture[] = L"x86 on x64";

enum class ClientFingerprintStatus : std::uint8_t {
    Match,
    ExecutableSizeMismatch,
    ArchitectureMismatch,
    Sha256Mismatch,
};

inline bool EqualAsciiCaseInsensitive(std::wstring_view a,
                                      std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        wchar_t left = a[i];
        wchar_t right = b[i];
        if (left >= L'a' && left <= L'z') left -= (L'a' - L'A');
        if (right >= L'a' && right <= L'z') right -= (L'a' - L'A');
        if (left != right) return false;
    }
    return true;
}

inline ClientFingerprintStatus CheckTargetClientFingerprint(
    std::uint64_t executableSize,
    std::wstring_view sha256,
    std::wstring_view architecture) noexcept {
    if (executableSize != kTargetExecutableSize) {
        return ClientFingerprintStatus::ExecutableSizeMismatch;
    }
    if (!EqualAsciiCaseInsensitive(architecture, kTargetProcessArchitecture)) {
        return ClientFingerprintStatus::ArchitectureMismatch;
    }
    if (!EqualAsciiCaseInsensitive(sha256, kTargetExecutableSha256)) {
        return ClientFingerprintStatus::Sha256Mismatch;
    }
    return ClientFingerprintStatus::Match;
}

} // namespace tetris
