#include "tetris/memory_snapshot.hpp"

#include <algorithm>
#include <cstdio>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace tetris {
namespace {

constexpr std::array<char, 8> kMagic{{'T', 'P', 'N', 'S', 'N', 'A', 'P', '1'}};
constexpr std::uint32_t kFormatVersion = 2;
constexpr std::uint32_t kLegacyFormatVersion = 1;
constexpr std::uint64_t kMaximumSpanCount = 1000000;
constexpr std::uint32_t kMaximumStringLength = 4096;
constexpr std::uint32_t kMaximumSpanBytes = 1024u * 1024u;
constexpr std::uint64_t kMaximumSnapshotBytes = 1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumDetailedRuns = 1000;
constexpr std::size_t kMaximumPages = 100;
constexpr std::size_t kMaximumAllocationGroups = 50;
constexpr std::size_t kMaximumDetailedAllocationGroups = 12;
constexpr std::size_t kMaximumRunsPerAllocation = 64;
constexpr std::size_t kMaximumContextPrivateAllocations = 8;
constexpr std::size_t kMaximumContextRunsPerAllocation = 8;
constexpr std::uint64_t kContextBytes = 16;

bool WriteU32(std::ostream& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.put(static_cast<char>((value >> (i * 8)) & 0xffu));
    return static_cast<bool>(out);
}

bool WriteU64(std::ostream& out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.put(static_cast<char>((value >> (i * 8)) & 0xffu));
    return static_cast<bool>(out);
}

bool ReadU32(std::istream& in, std::uint32_t& value) {
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const int byte = in.get();
        if (byte == EOF) return false;
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (i * 8);
    }
    return true;
}

bool ReadU64(std::istream& in, std::uint64_t& value) {
    value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const int byte = in.get();
        if (byte == EOF) return false;
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8);
    }
    return true;
}

bool WriteString(std::ostream& out, const std::string& value) {
    if (value.size() > kMaximumStringLength) return false;
    return WriteU32(out, static_cast<std::uint32_t>(value.size())) &&
        (value.empty() || static_cast<bool>(out.write(value.data(),
            static_cast<std::streamsize>(value.size()))));
}

bool ReadString(std::istream& in, std::string& value) {
    std::uint32_t size = 0;
    if (!ReadU32(in, size) || size > kMaximumStringLength) return false;
    value.assign(size, '\0');
    return size == 0 || static_cast<bool>(in.read(value.data(),
        static_cast<std::streamsize>(size)));
}

std::string HexBytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

struct ChangedRun {
    std::uint64_t base{};
    std::uint64_t length{};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
};

void CloseRun(bool& active, ChangedRun& current,
              std::vector<ChangedRun>& details,
              MemorySnapshotDiffSummary& summary) {
    if (!active) return;
    ++summary.changedRuns;
    if (details.size() < kMaximumDetailedRuns)
        details.push_back(std::move(current));
    current = {};
    active = false;
}

std::string SnapshotTime(std::uint64_t timestamp) {
    std::ostringstream out;
    out << timestamp << " (Unix milliseconds UTC)";
    return out.str();
}

const char* RegionKindName(MemoryRegionKind kind) {
    return kind == MemoryRegionKind::Image ? "MEM_IMAGE" : "MEM_PRIVATE";
}

} // namespace

bool WriteMemorySnapshotFile(const std::filesystem::path& path,
                             const MemorySnapshotFile& snapshot,
                             std::string& error) {
    error.clear();
    if (snapshot.processId == 0 || snapshot.architecture.empty() ||
        snapshot.executableSha256.size() != 64 ||
        snapshot.spans.size() > kMaximumSpanCount) {
        error = "Invalid snapshot metadata.";
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Cannot create snapshot file.";
        return false;
    }

    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    bool ok = static_cast<bool>(out) &&
        WriteU32(out, kFormatVersion) &&
        WriteU32(out, snapshot.processId) &&
        WriteU64(out, snapshot.capturedAtUnixMs) &&
        WriteU64(out, snapshot.executableSize) &&
        WriteU64(out, snapshot.eligibleBytes) &&
        WriteU64(out, snapshot.capturedBytes) &&
        WriteU64(out, snapshot.skippedBytes) &&
        WriteString(out, snapshot.architecture) &&
        WriteString(out, snapshot.executableSha256) &&
        WriteU64(out, static_cast<std::uint64_t>(snapshot.spans.size()));

    std::uint64_t totalBytes = 0;
    for (const auto& span : snapshot.spans) {
        if (!ok) break;
        const auto kindValue = static_cast<std::uint32_t>(span.regionKind);
        const bool validKind =
            (span.regionKind == MemoryRegionKind::Private && span.moduleName.empty()) ||
            (span.regionKind == MemoryRegionKind::Image && !span.moduleName.empty());
        if (!validKind || (kindValue != static_cast<std::uint32_t>(MemoryRegionKind::Private) &&
                           kindValue != static_cast<std::uint32_t>(MemoryRegionKind::Image)) ||
            span.bytes.empty() || span.bytes.size() > kMaximumSpanBytes ||
            span.base > (std::numeric_limits<std::uint64_t>::max)() - span.bytes.size() ||
            totalBytes > kMaximumSnapshotBytes - span.bytes.size()) {
            error = "Snapshot contains an invalid or oversized memory span.";
            ok = false;
            break;
        }
        ok = WriteU64(out, span.base) &&
            WriteU64(out, span.allocationBase) &&
            WriteU32(out, span.protection) &&
            WriteU32(out, kindValue) &&
            WriteString(out, span.moduleName) &&
            WriteU32(out, static_cast<std::uint32_t>(span.bytes.size())) &&
            static_cast<bool>(out.write(
                reinterpret_cast<const char*>(span.bytes.data()),
                static_cast<std::streamsize>(span.bytes.size())));
        totalBytes += static_cast<std::uint64_t>(span.bytes.size());
    }

    out.flush();
    if (!ok || !out) {
        if (error.empty()) error = "Failed while writing snapshot file.";
        return false;
    }
    return true;
}

bool ReadMemorySnapshotFile(const std::filesystem::path& path,
                            MemorySnapshotFile& snapshot,
                            std::string& error) {
    error.clear();
    snapshot = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open snapshot file.";
        return false;
    }

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint32_t version = 0;
    std::uint64_t spanCount = 0;
    if (!in || magic != kMagic ||
        !ReadU32(in, version) ||
        (version != kFormatVersion && version != kLegacyFormatVersion) ||
        !ReadU32(in, snapshot.processId) ||
        !ReadU64(in, snapshot.capturedAtUnixMs) ||
        !ReadU64(in, snapshot.executableSize) ||
        !ReadU64(in, snapshot.eligibleBytes) ||
        !ReadU64(in, snapshot.capturedBytes) ||
        !ReadU64(in, snapshot.skippedBytes) ||
        !ReadString(in, snapshot.architecture) ||
        !ReadString(in, snapshot.executableSha256) ||
        !ReadU64(in, spanCount)) {
        error = "Invalid snapshot header or unsupported format version.";
        return false;
    }
    if (snapshot.processId == 0 || snapshot.architecture.empty() ||
        snapshot.executableSha256.size() != 64 || spanCount > kMaximumSpanCount) {
        error = "Snapshot metadata failed validation.";
        return false;
    }

    std::uint64_t totalBytes = 0;
    snapshot.spans.reserve(static_cast<std::size_t>(spanCount));
    for (std::uint64_t i = 0; i < spanCount; ++i) {
        MemorySnapshotSpan span;
        std::uint32_t dataSize = 0;
        if (!ReadU64(in, span.base) ||
            !ReadU64(in, span.allocationBase) ||
            !ReadU32(in, span.protection)) {
            error = "Snapshot contains an invalid memory-span header.";
            return false;
        }
        if (version >= kFormatVersion) {
            std::uint32_t kindValue = 0;
            if (!ReadU32(in, kindValue) || !ReadString(in, span.moduleName) ||
                (kindValue != static_cast<std::uint32_t>(MemoryRegionKind::Private) &&
                 kindValue != static_cast<std::uint32_t>(MemoryRegionKind::Image))) {
                error = "Snapshot contains an invalid memory-region type.";
                return false;
            }
            span.regionKind = static_cast<MemoryRegionKind>(kindValue);
            const bool validKind =
                (span.regionKind == MemoryRegionKind::Private && span.moduleName.empty()) ||
                (span.regionKind == MemoryRegionKind::Image && !span.moduleName.empty());
            if (!validKind) {
                error = "Snapshot memory-region metadata failed validation.";
                return false;
            }
        } else {
            span.regionKind = MemoryRegionKind::Private;
        }
        if (!ReadU32(in, dataSize) ||
            dataSize == 0 || dataSize > kMaximumSpanBytes ||
            span.base > (std::numeric_limits<std::uint64_t>::max)() - dataSize ||
            totalBytes > kMaximumSnapshotBytes - dataSize) {
            error = "Snapshot contains an invalid memory-span header.";
            return false;
        }
        span.bytes.resize(dataSize);
        if (!in.read(reinterpret_cast<char*>(span.bytes.data()),
                     static_cast<std::streamsize>(dataSize))) {
            error = "Snapshot file ended inside a memory span.";
            return false;
        }
        totalBytes += dataSize;
        snapshot.spans.push_back(std::move(span));
    }

    if (snapshot.capturedBytes != totalBytes) {
        error = "Snapshot byte count does not match its span data.";
        return false;
    }
    return true;
}

bool CompareMemorySnapshots(const MemorySnapshotFile& before,
                            const MemorySnapshotFile& after,
                            const std::filesystem::path& reportPath,
                            MemorySnapshotDiffSummary& summary,
                            std::string& error) {
    summary = {};
    error.clear();
    if (before.processId != after.processId ||
        before.executableSize != after.executableSize ||
        before.executableSha256 != after.executableSha256 ||
        before.architecture != after.architecture) {
        error = "Snapshots do not belong to the same process identity and client build.";
        return false;
    }

    auto a = before.spans;
    auto b = after.spans;
    const auto byBase = [](const MemorySnapshotSpan& left,
                           const MemorySnapshotSpan& right) {
        return left.base < right.base;
    };
    std::sort(a.begin(), a.end(), byBase);
    std::sort(b.begin(), b.end(), byBase);
    for (const auto* spans : {&a, &b}) {
        std::uint64_t previousEnd = 0;
        bool first = true;
        for (const auto& span : *spans) {
            const bool validKind =
                (span.regionKind == MemoryRegionKind::Private && span.moduleName.empty()) ||
                (span.regionKind == MemoryRegionKind::Image && !span.moduleName.empty());
            if (!validKind || span.bytes.empty() ||
                span.base > (std::numeric_limits<std::uint64_t>::max)() - span.bytes.size() ||
                (!first && span.base < previousEnd)) {
                error = "Snapshot contains overlapping or invalid memory spans.";
                return false;
            }
            previousEnd = span.base + static_cast<std::uint64_t>(span.bytes.size());
            first = false;
        }
    }

    std::map<std::uint64_t, std::uint64_t> changedByPage;
    std::map<std::uint64_t, std::uint64_t> changedByAllocation;
    std::map<std::uint64_t, std::pair<MemoryRegionKind, std::string>> allocationMetadata;
    std::vector<ChangedRun> details;
    details.reserve(kMaximumDetailedRuns);
    bool runActive = false;
    ChangedRun currentRun;
    std::uint64_t lastChangedAddress = 0;

    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        const auto aEnd = a[i].base + static_cast<std::uint64_t>(a[i].bytes.size());
        const auto bEnd = b[j].base + static_cast<std::uint64_t>(b[j].bytes.size());
        const auto start = (std::max)(a[i].base, b[j].base);
        const auto end = (std::min)(aEnd, bEnd);

        if (start < end) {
            const bool sameMapping =
                a[i].allocationBase == b[j].allocationBase &&
                a[i].regionKind == b[j].regionKind &&
                a[i].moduleName == b[j].moduleName;
            if (!sameMapping) {
                CloseRun(runActive, currentRun, details, summary);
            } else {
                if (runActive && lastChangedAddress + 1 != start)
                    CloseRun(runActive, currentRun, details, summary);

                for (std::uint64_t address = start; address < end; ++address) {
                const auto oldByte = a[i].bytes[static_cast<std::size_t>(address - a[i].base)];
                const auto newByte = b[j].bytes[static_cast<std::size_t>(address - b[j].base)];
                ++summary.comparedBytes;
                if (oldByte == newByte) {
                    CloseRun(runActive, currentRun, details, summary);
                    continue;
                }

                ++summary.changedBytes;
                ++changedByPage[address & ~std::uint64_t{0xfff}];
                auto& allocationCount = changedByAllocation[a[i].allocationBase];
                if (allocationCount++ == 0) {
                    allocationMetadata.emplace(a[i].allocationBase,
                        std::make_pair(a[i].regionKind, a[i].moduleName));
                }
                if (!runActive || lastChangedAddress + 1 != address) {
                    CloseRun(runActive, currentRun, details, summary);
                    runActive = true;
                    currentRun.base = address;
                }
                ++currentRun.length;
                if (currentRun.before.size() < 16) {
                    currentRun.before.push_back(oldByte);
                    currentRun.after.push_back(newByte);
                }
                lastChangedAddress = address;
                }
            }
        } else if (runActive && lastChangedAddress + 1 != start) {
            CloseRun(runActive, currentRun, details, summary);
        }

        if (aEnd <= bEnd) ++i;
        if (bEnd <= aEnd) ++j;
    }
    CloseRun(runActive, currentRun, details, summary);
    summary.changedPages = changedByPage.size();
    summary.changedAllocationGroups = changedByAllocation.size();

    std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
    if (!report) {
        error = "Cannot create memory-difference report.";
        return false;
    }

    report << "Tetris Pilot Next - read-only memory snapshot difference\n"
        << "Before process ID: " << before.processId << "\n"
        << "After process ID: " << after.processId << "\n"
        << "Architecture: " << before.architecture << "\n"
        << "Executable size: " << before.executableSize << "\n"
        << "Executable SHA-256: " << before.executableSha256 << "\n"
        << "Before captured at: " << SnapshotTime(before.capturedAtUnixMs) << "\n"
        << "After captured at: " << SnapshotTime(after.capturedAtUnixMs) << "\n\n"
        << "[Capture coverage]\n"
        << "Before eligible bytes: " << before.eligibleBytes << "\n"
        << "Before captured bytes: " << before.capturedBytes << "\n"
        << "Before skipped bytes: " << before.skippedBytes << "\n"
        << "After eligible bytes: " << after.eligibleBytes << "\n"
        << "After captured bytes: " << after.capturedBytes << "\n"
        << "After skipped bytes: " << after.skippedBytes << "\n\n"
        << "[Difference summary]\n"
        << "Bytes compared at matching addresses and memory mappings: " << summary.comparedBytes << "\n"
        << "Changed bytes: " << summary.changedBytes << "\n"
        << "Changed contiguous runs: " << summary.changedRuns << "\n"
        << "Changed 4 KiB pages: " << summary.changedPages << "\n"
        << "Changed allocation groups: " << summary.changedAllocationGroups << "\n"
        << "Detailed changed runs shown: " << details.size() << "\n"
        << "Unmatched or unreadable address ranges are excluded from the comparison.\n\n";

    report << "[Largest changed allocation groups]\n";
    std::vector<std::pair<std::uint64_t, std::uint64_t>> allocations(
        changedByAllocation.begin(), changedByAllocation.end());
    std::sort(allocations.begin(), allocations.end(),
        [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
    for (std::size_t index = 0;
         index < allocations.size() && index < kMaximumAllocationGroups; ++index) {
        const auto metadata = allocationMetadata.find(allocations[index].first);
        report << "AllocationBase: 0x" << std::hex << allocations[index].first
            << std::dec << " | changed bytes: " << allocations[index].second;
        if (metadata != allocationMetadata.end()) {
            report << " | region: " << RegionKindName(metadata->second.first);
            if (!metadata->second.second.empty())
                report << " | module: " << metadata->second.second;
        }
        report << "\n";
    }

    // Produce a second, allocation-focused detail pass. The original global
    // range list is address-sorted and can be consumed by low-address noise
    // before it reaches the most changed heaps or game image data.
    std::vector<std::uint64_t> detailedAllocationBases;
    for (std::size_t index = 0;
         index < allocations.size() && index < kMaximumDetailedAllocationGroups; ++index) {
        detailedAllocationBases.push_back(allocations[index].first);
    }
    // Small but meaningful changes in the game's mapped image (.data) are
    // easy to miss in a top-N heap ranking. Always include every changed
    // image allocation, even when it has only a handful of changed bytes.
    for (const auto& metadata : allocationMetadata) {
        if (metadata.second.first == MemoryRegionKind::Image &&
            std::find(detailedAllocationBases.begin(), detailedAllocationBases.end(),
                metadata.first) == detailedAllocationBases.end()) {
            detailedAllocationBases.push_back(metadata.first);
        }
    }
    std::map<std::uint64_t, std::vector<ChangedRun>> allocationDetails;
    bool allocationRunActive = false;
    std::uint64_t allocationRunBase = 0;
    ChangedRun allocationRun;
    std::uint64_t allocationLastChangedAddress = 0;
    auto closeAllocationRun = [&]() {
        if (!allocationRunActive) return;
        auto& runs = allocationDetails[allocationRunBase];
        if (runs.size() < kMaximumRunsPerAllocation)
            runs.push_back(std::move(allocationRun));
        allocationRun = {};
        allocationRunActive = false;
    };

    i = 0;
    j = 0;
    while (i < a.size() && j < b.size()) {
        const auto aEnd = a[i].base + static_cast<std::uint64_t>(a[i].bytes.size());
        const auto bEnd = b[j].base + static_cast<std::uint64_t>(b[j].bytes.size());
        const auto start = (std::max)(a[i].base, b[j].base);
        const auto end = (std::min)(aEnd, bEnd);
        if (start < end) {
            const bool sameMapping =
                a[i].allocationBase == b[j].allocationBase &&
                a[i].regionKind == b[j].regionKind &&
                a[i].moduleName == b[j].moduleName;
            const bool selectedAllocation = std::find(
                detailedAllocationBases.begin(), detailedAllocationBases.end(),
                a[i].allocationBase) != detailedAllocationBases.end();
            if (!sameMapping || !selectedAllocation) {
                closeAllocationRun();
            } else {
                if (allocationRunActive &&
                    allocationLastChangedAddress + 1 != start) {
                    closeAllocationRun();
                }
                for (std::uint64_t address = start; address < end; ++address) {
                    const auto oldByte =
                        a[i].bytes[static_cast<std::size_t>(address - a[i].base)];
                    const auto newByte =
                        b[j].bytes[static_cast<std::size_t>(address - b[j].base)];
                    if (oldByte == newByte) {
                        closeAllocationRun();
                        continue;
                    }

                    if (!allocationRunActive ||
                        allocationRunBase != a[i].allocationBase ||
                        allocationLastChangedAddress + 1 != address) {
                        closeAllocationRun();
                        allocationRunActive = true;
                        allocationRunBase = a[i].allocationBase;
                        allocationRun.base = address;
                    }
                    ++allocationRun.length;
                    if (allocationRun.before.size() < 16) {
                        allocationRun.before.push_back(oldByte);
                        allocationRun.after.push_back(newByte);
                    }
                    allocationLastChangedAddress = address;
                }
            }
        } else if (allocationRunActive &&
                   allocationLastChangedAddress + 1 != start) {
            closeAllocationRun();
        }
        if (aEnd <= bEnd) ++i;
        if (bEnd <= aEnd) ++j;
    }
    closeAllocationRun();

    report << "\n[Changed byte ranges grouped by largest allocations]\n";
    for (const auto allocationBase : detailedAllocationBases) {
        const auto count = changedByAllocation.find(allocationBase);
        const auto metadata = allocationMetadata.find(allocationBase);
        if (count == changedByAllocation.end()) continue;
        report << "AllocationBase: 0x" << std::hex << allocationBase
            << std::dec << " | changed bytes: " << count->second;
        if (metadata != allocationMetadata.end()) {
            report << " | region: " << RegionKindName(metadata->second.first);
            if (!metadata->second.second.empty())
                report << " | module: " << metadata->second.second;
        }
        report << "\n";
        const auto runs = allocationDetails.find(allocationBase);
        const std::size_t runCount = runs == allocationDetails.end()
            ? 0 : runs->second.size();
        if (runs != allocationDetails.end()) {
            for (const auto& run : runs->second) {
                report << "  Base: 0x" << std::hex << run.base << std::dec
                    << " | length: " << run.length
                    << " | before: " << HexBytes(run.before)
                    << " | after: " << HexBytes(run.after) << "\n";
            }
        }
        const auto allocationRunsCount = runCount;
        if (allocationRunsCount == kMaximumRunsPerAllocation) {
            report << "  Additional runs for this allocation may be omitted.\n";
        }
    }

    // Select a compact set of private-memory candidates (moderate change count)
    // plus every changed image allocation. Include unchanged bytes around each
    // selected range so pointer fields, adjacent coordinates and structure
    // boundaries are visible in the text report without sending raw snapshots.
    std::vector<std::uint64_t> contextAllocationBases;
    for (const auto& allocation : allocations) {
        if (contextAllocationBases.size() >= kMaximumContextPrivateAllocations)
            break;
        const auto metadata = allocationMetadata.find(allocation.first);
        if (metadata == allocationMetadata.end() ||
            metadata->second.first != MemoryRegionKind::Private ||
            allocation.second < 100 || allocation.second > 5000) {
            continue;
        }
        contextAllocationBases.push_back(allocation.first);
    }
    for (const auto& metadata : allocationMetadata) {
        if (metadata.second.first == MemoryRegionKind::Image &&
            std::find(contextAllocationBases.begin(), contextAllocationBases.end(),
                metadata.first) == contextAllocationBases.end()) {
            contextAllocationBases.push_back(metadata.first);
        }
    }

    const auto findSpanAt = [](const std::vector<MemorySnapshotSpan>& spans,
                               std::uint64_t address) -> const MemorySnapshotSpan* {
        const auto it = std::upper_bound(spans.begin(), spans.end(), address,
            [](std::uint64_t value, const MemorySnapshotSpan& span) {
                return value < span.base;
            });
        if (it == spans.begin()) return nullptr;
        const auto& candidate = *std::prev(it);
        const auto end = candidate.base +
            static_cast<std::uint64_t>(candidate.bytes.size());
        return address >= candidate.base && address < end ? &candidate : nullptr;
    };

    report << "\n[Candidate memory context windows]\n";
    report << "Each window shows 16 bytes before and after a changed range when available.\n";
    for (const auto allocationBase : contextAllocationBases) {
        const auto metadata = allocationMetadata.find(allocationBase);
        const auto runs = allocationDetails.find(allocationBase);
        if (metadata == allocationMetadata.end() || runs == allocationDetails.end())
            continue;

        const auto count = changedByAllocation.find(allocationBase);
        report << "AllocationBase: 0x" << std::hex << allocationBase << std::dec
            << " | region: " << RegionKindName(metadata->second.first);
        if (!metadata->second.second.empty())
            report << " | module: " << metadata->second.second;
        if (count != changedByAllocation.end())
            report << " | changed bytes: " << count->second;
        report << "\n";

        std::size_t emitted = 0;
        for (const auto& run : runs->second) {
            if (emitted >= kMaximumContextRunsPerAllocation) break;
            const auto* beforeSpan = findSpanAt(a, run.base);
            const auto* afterSpan = findSpanAt(b, run.base);
            if (!beforeSpan || !afterSpan ||
                beforeSpan->allocationBase != allocationBase ||
                afterSpan->allocationBase != allocationBase ||
                beforeSpan->regionKind != afterSpan->regionKind ||
                beforeSpan->moduleName != afterSpan->moduleName) {
                continue;
            }

            const auto beforeEnd = beforeSpan->base +
                static_cast<std::uint64_t>(beforeSpan->bytes.size());
            const auto afterEnd = afterSpan->base +
                static_cast<std::uint64_t>(afterSpan->bytes.size());
            const auto commonEnd = (std::min)(beforeEnd, afterEnd);
            const auto contextStart = run.base > beforeSpan->base + kContextBytes
                ? run.base - kContextBytes : beforeSpan->base;
            const auto changedEnd = run.base + run.length;
            const auto contextEnd = (std::min)(commonEnd, changedEnd + kContextBytes);
            if (contextStart >= contextEnd || run.base + run.length > commonEnd)
                continue;

            const auto beforeOffset = static_cast<std::size_t>(
                contextStart - beforeSpan->base);
            const auto afterOffset = static_cast<std::size_t>(
                contextStart - afterSpan->base);
            const auto contextLength = static_cast<std::size_t>(
                contextEnd - contextStart);
            std::vector<std::uint8_t> beforeWindow(
                beforeSpan->bytes.begin() + static_cast<std::ptrdiff_t>(beforeOffset),
                beforeSpan->bytes.begin() + static_cast<std::ptrdiff_t>(
                    beforeOffset + contextLength));
            std::vector<std::uint8_t> afterWindow(
                afterSpan->bytes.begin() + static_cast<std::ptrdiff_t>(afterOffset),
                afterSpan->bytes.begin() + static_cast<std::ptrdiff_t>(
                    afterOffset + contextLength));

            report << "  Changed: 0x" << std::hex << run.base << std::dec
                << " | length: " << run.length
                << " | allocation offset: 0x" << std::hex
                << (run.base - allocationBase) << std::dec << "\n"
                << "    before[" << contextLength << "]: "
                << HexBytes(beforeWindow) << "\n"
                << "    after [" << contextLength << "]: "
                << HexBytes(afterWindow) << "\n";
            ++emitted;
        }
        if (runs->second.size() > emitted)
            report << "  Context windows emitted: " << emitted
                << " (additional ranges omitted)\n";
    }

    report << "\n[Most changed 4 KiB pages]\n";
    std::vector<std::pair<std::uint64_t, std::uint64_t>> pages(
        changedByPage.begin(), changedByPage.end());
    std::sort(pages.begin(), pages.end(),
        [](const auto& left, const auto& right) {
            return left.second > right.second;
        });
    for (std::size_t index = 0; index < pages.size() && index < kMaximumPages; ++index) {
        report << "Page: 0x" << std::hex << pages[index].first
            << std::dec << " | changed bytes: " << pages[index].second << "\n";
    }

    report << "\n[Changed byte ranges]\n";
    for (const auto& run : details) {
        report << "Base: 0x" << std::hex << run.base << std::dec
            << " | length: " << run.length
            << " | before: " << HexBytes(run.before)
            << " | after: " << HexBytes(run.after) << "\n";
    }
    if (summary.changedRuns > details.size()) {
        report << "Additional changed runs omitted: "
            << (summary.changedRuns - details.size()) << "\n";
    }
    report << "\nThis report compares only bytes captured at matching virtual addresses.\n"
        << "It does not identify game variables by itself; correlate multiple controlled states.\n"
        << "No process memory was modified.\n";

    report.flush();
    if (!report) {
        error = "Failed while writing memory-difference report.";
        return false;
    }
    return true;
}

bool CompareMemorySnapshotFiles(const std::filesystem::path& beforePath,
                                const std::filesystem::path& afterPath,
                                const std::filesystem::path& reportPath,
                                MemorySnapshotDiffSummary& summary,
                                std::string& error) {
    MemorySnapshotFile before;
    MemorySnapshotFile after;
    if (!ReadMemorySnapshotFile(beforePath, before, error)) return false;
    if (!ReadMemorySnapshotFile(afterPath, after, error)) return false;
    return CompareMemorySnapshots(before, after, reportPath, summary, error);
}

} // namespace tetris
