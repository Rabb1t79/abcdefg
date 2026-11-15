#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tetris {

struct MemorySnapshotSpan {
    std::uint64_t base{};
    std::uint64_t allocationBase{};
    std::uint32_t protection{};
    std::vector<std::uint8_t> bytes;
};

struct MemorySnapshotFile {
    std::uint32_t processId{};
    std::uint64_t capturedAtUnixMs{};
    std::uint64_t executableSize{};
    std::string architecture;
    std::string executableSha256;
    std::uint64_t eligibleBytes{};
    std::uint64_t capturedBytes{};
    std::uint64_t skippedBytes{};
    std::vector<MemorySnapshotSpan> spans;
};

struct MemorySnapshotDiffSummary {
    std::uint64_t comparedBytes{};
    std::uint64_t changedBytes{};
    std::uint64_t changedRuns{};
    std::uint64_t changedPages{};
    std::uint64_t changedAllocationGroups{};
};

bool WriteMemorySnapshotFile(const std::filesystem::path& path,
                             const MemorySnapshotFile& snapshot,
                             std::string& error);
bool ReadMemorySnapshotFile(const std::filesystem::path& path,
                            MemorySnapshotFile& snapshot,
                            std::string& error);
bool CompareMemorySnapshots(const MemorySnapshotFile& before,
                            const MemorySnapshotFile& after,
                            const std::filesystem::path& reportPath,
                            MemorySnapshotDiffSummary& summary,
                            std::string& error);
bool CompareMemorySnapshotFiles(const std::filesystem::path& beforePath,
                                const std::filesystem::path& afterPath,
                                const std::filesystem::path& reportPath,
                                MemorySnapshotDiffSummary& summary,
                                std::string& error);

} // namespace tetris
