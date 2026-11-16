#include "tetris/domain.hpp"
#include "tetris/private_memory_policy.hpp"
#include "tetris/memory_snapshot.hpp"
#include "tetris/client_profile.hpp"
#include "tetris/game_ports.hpp"
#include "tetris/session_state.hpp"
#include "tetris/solver.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace tetris;

static void Check(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "FAILED: " << expression << "\n";
        std::exit(EXIT_FAILURE);
    }
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression)

class TestStateSource final : public IGameStateSource {
public:
    SnapshotRead ReadSnapshot() override {
        SnapshotRead result;
        result.ok = true;
        result.provider = "unit-test";
        result.snapshot.phase = GamePhase::Playing;
        result.snapshot.frameId = 42;
        return result;
    }
};

class TestActionSink final : public IGameActionSink {
public:
    ActionReceipt Dispatch(const GameAction& action) override {
        ActionReceipt result;
        result.submitted = action.repeat > 0;
        result.sequence = 7;
        result.backend = "unit-test";
        return result;
    }
};

static PieceCatalog MakeCatalog() {
    PieceCatalog catalog;
    const std::vector<Cell> horizontal{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    const std::vector<Cell> vertical{{0, 0}, {0, 1}, {0, 2}, {0, 3}};
    CHECK(catalog.Set(PieceKind::I, {horizontal, vertical}));
    const std::vector<Cell> square{{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    CHECK(catalog.Set(PieceKind::O, {square}));
    return catalog;
}

int main() {
    CHECK(kMinimumListedPrivateRegionSize == 16ull * 1024ull);
    CHECK(IsReadableMemoryProtection(0x02u)); // PAGE_READONLY
    CHECK(IsReadableMemoryProtection(0x04u)); // PAGE_READWRITE
    CHECK(IsReadableMemoryProtection(0x08u)); // PAGE_WRITECOPY
    CHECK(IsReadableMemoryProtection(0x20u)); // PAGE_EXECUTE_READ
    CHECK(IsReadableMemoryProtection(0x40u)); // PAGE_EXECUTE_READWRITE
    CHECK(IsReadableMemoryProtection(0x80u)); // PAGE_EXECUTE_WRITECOPY
    CHECK(!IsReadableMemoryProtection(0x01u)); // PAGE_NOACCESS
    CHECK(!IsReadableMemoryProtection(0x10u)); // PAGE_EXECUTE
    CHECK(!IsReadableMemoryProtection(0x104u)); // PAGE_GUARD | PAGE_READWRITE
    CHECK(IsWritableMemoryProtection(0x04u)); // PAGE_READWRITE
    CHECK(IsWritableMemoryProtection(0x08u)); // PAGE_WRITECOPY
    CHECK(IsWritableMemoryProtection(0x40u)); // PAGE_EXECUTE_READWRITE
    CHECK(IsWritableMemoryProtection(0x80u)); // PAGE_EXECUTE_WRITECOPY
    CHECK(!IsWritableMemoryProtection(0x02u)); // PAGE_READONLY
    CHECK(!IsWritableMemoryProtection(0x104u)); // PAGE_GUARD | PAGE_READWRITE

    // The exact reported target client is accepted, including lowercase hex.
    CHECK(CheckTargetClientFingerprint(
        kTargetExecutableSize,
        kTargetExecutableSha256,
        kTargetProcessArchitecture) == ClientFingerprintStatus::Match);
    CHECK(CheckTargetClientFingerprint(
        kTargetExecutableSize,
        L"9951c928f15f71839ce97ccdFA29CF040eb6699c8ca54f82c7b31b0561784c42",
        kTargetProcessArchitecture) == ClientFingerprintStatus::Match);
    CHECK(CheckTargetClientFingerprint(
        kTargetExecutableSize - 1,
        kTargetExecutableSha256,
        kTargetProcessArchitecture) == ClientFingerprintStatus::ExecutableSizeMismatch);
    CHECK(CheckTargetClientFingerprint(
        kTargetExecutableSize,
        L"0000000000000000000000000000000000000000000000000000000000000000",
        kTargetProcessArchitecture) == ClientFingerprintStatus::Sha256Mismatch);
    CHECK(CheckTargetClientFingerprint(
        kTargetExecutableSize,
        kTargetExecutableSha256,
        L"x64") == ClientFingerprintStatus::ArchitectureMismatch);


    // Snapshot files round-trip, compare bytes at matching virtual addresses,
    // and reject snapshots that belong to different process instances.
    const auto testToken = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temp = std::filesystem::temp_directory_path();
    const auto beforePath = temp / ("tpn-before-" + testToken + ".tpsnap");
    const auto afterPath = temp / ("tpn-after-" + testToken + ".tpsnap");
    const auto mismatchPath = temp / ("tpn-mismatch-" + testToken + ".tpsnap");
    const auto legacyPath = temp / ("tpn-legacy-" + testToken + ".tpsnap");
    const auto diffPath = temp / ("tpn-diff-" + testToken + ".txt");

    MemorySnapshotFile snapshotBefore;
    snapshotBefore.processId = 14120;
    snapshotBefore.capturedAtUnixMs = 1000;
    snapshotBefore.executableSize = 1654784;
    snapshotBefore.architecture = "x86 on x64";
    snapshotBefore.executableSha256 =
        "9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42";
    snapshotBefore.eligibleBytes = 12;
    snapshotBefore.capturedBytes = 12;
    MemorySnapshotSpan beforeSpan;
    beforeSpan.base = 0x1000;
    beforeSpan.allocationBase = 0x1000;
    beforeSpan.protection = 0x04;
    beforeSpan.bytes = {10, 11, 12, 13, 14, 15, 16, 17};
    snapshotBefore.spans.push_back(beforeSpan);

    MemorySnapshotSpan imageSpan;
    imageSpan.base = 0x3000;
    imageSpan.allocationBase = 0x3000;
    imageSpan.protection = 0x04;
    imageSpan.regionKind = MemoryRegionKind::Image;
    imageSpan.moduleName = "tetris.exe";
    imageSpan.bytes = {1, 2, 3, 4};
    snapshotBefore.spans.push_back(imageSpan);

    MemorySnapshotFile snapshotAfter = snapshotBefore;
    snapshotAfter.capturedAtUnixMs = 2000;
    snapshotAfter.spans[0].bytes[1] = 99;
    snapshotAfter.spans[0].bytes[2] = 98;
    snapshotAfter.spans[0].bytes[5] = 88;
    snapshotAfter.spans[1].bytes[2] = 77;

    std::string snapshotError;
    CHECK(WriteMemorySnapshotFile(beforePath, snapshotBefore, snapshotError));
    CHECK(WriteMemorySnapshotFile(afterPath, snapshotAfter, snapshotError));

    MemorySnapshotFile roundTrip;
    CHECK(ReadMemorySnapshotFile(beforePath, roundTrip, snapshotError));
    CHECK(roundTrip.processId == snapshotBefore.processId);
    CHECK(roundTrip.spans.size() == 2);
    CHECK(roundTrip.spans[0].bytes == snapshotBefore.spans[0].bytes);
    CHECK(roundTrip.spans[0].regionKind == MemoryRegionKind::Private);
    CHECK(roundTrip.spans[1].regionKind == MemoryRegionKind::Image);
    CHECK(roundTrip.spans[1].moduleName == "tetris.exe");
    CHECK(roundTrip.spans[1].bytes == snapshotBefore.spans[1].bytes);

    MemorySnapshotDiffSummary diffSummary;
    CHECK(CompareMemorySnapshotFiles(
        beforePath, afterPath, diffPath, diffSummary, snapshotError));
    CHECK(diffSummary.comparedBytes == 12);
    CHECK(diffSummary.changedBytes == 4);
    CHECK(diffSummary.changedRuns == 3);
    CHECK(diffSummary.changedPages == 2);
    CHECK(diffSummary.changedAllocationGroups == 2);
    CHECK(std::filesystem::file_size(diffPath) > 0);

    // Existing v1 snapshots contain private spans only and remain readable.
    {
        std::ofstream legacy(legacyPath, std::ios::binary | std::ios::trunc);
        const char magic[8] = {'T', 'P', 'N', 'S', 'N', 'A', 'P', '1'};
        legacy.write(magic, sizeof(magic));
        const auto writeU32 = [&](std::uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                legacy.put(static_cast<char>((value >> (i * 8)) & 0xffu));
        };
        const auto writeU64 = [&](std::uint64_t value) {
            for (unsigned i = 0; i < 8; ++i)
                legacy.put(static_cast<char>((value >> (i * 8)) & 0xffu));
        };
        const auto writeString = [&](const std::string& value) {
            writeU32(static_cast<std::uint32_t>(value.size()));
            legacy.write(value.data(), static_cast<std::streamsize>(value.size()));
        };
        writeU32(1); // v1
        writeU32(14120);
        writeU64(1500);
        writeU64(1654784);
        writeU64(3);
        writeU64(3);
        writeU64(0);
        writeString("x86 on x64");
        writeString("9951C928F15F71839CE97CCDFA29CF040EB6699C8CA54F82C7B31B0561784C42");
        writeU64(1);
        writeU64(0x5000);
        writeU64(0x5000);
        writeU32(0x04);
        writeU32(3);
        const char legacyBytes[3] = {7, 8, 9};
        legacy.write(legacyBytes, sizeof(legacyBytes));
        legacy.close();
        CHECK(static_cast<bool>(legacy));
    }
    MemorySnapshotFile legacyRoundTrip;
    CHECK(ReadMemorySnapshotFile(legacyPath, legacyRoundTrip, snapshotError));
    CHECK(legacyRoundTrip.spans.size() == 1);
    CHECK(legacyRoundTrip.spans[0].regionKind == MemoryRegionKind::Private);
    CHECK(legacyRoundTrip.spans[0].moduleName.empty());
    CHECK(legacyRoundTrip.spans[0].bytes == std::vector<std::uint8_t>({7, 8, 9}));

    MemorySnapshotFile otherProcess = snapshotAfter;
    otherProcess.processId += 1;
    CHECK(WriteMemorySnapshotFile(mismatchPath, otherProcess, snapshotError));
    CHECK(!CompareMemorySnapshotFiles(
        beforePath, mismatchPath, diffPath, diffSummary, snapshotError));
    CHECK(!snapshotError.empty());

    std::filesystem::remove(beforePath);
    std::filesystem::remove(afterPath);
    std::filesystem::remove(mismatchPath);
    std::filesystem::remove(legacyPath);
    std::filesystem::remove(diffPath);

    Board board{};
    CHECK(board.Set(3, 19));
    CHECK(board.Occupied(3, 19));
    CHECK(!board.Set(-1, 0));

    const PieceCatalog catalog = MakeCatalog();

    TestStateSource source;
    const SnapshotRead snapshot = source.ReadSnapshot();
    CHECK(snapshot.ok);
    CHECK(snapshot.snapshot.phase == GamePhase::Playing);
    CHECK(snapshot.snapshot.frameId == 42);
    TestActionSink actionSink;
    const ActionReceipt receipt = actionSink.Dispatch(GameAction{ActionKind::MoveLeft, 1});
    CHECK(receipt.submitted);
    CHECK(receipt.sequence == 7);
    ExhaustiveSolver solver;

    const SearchResult emptyBoard = solver.Solve(Board{}, {PieceKind::I}, catalog, 1);
    CHECK(emptyBoard.found);
    CHECK(emptyBoard.depth == 1);
    CHECK(emptyBoard.visitedNodes > 0);
    CHECK(emptyBoard.firstMove.kind == PieceKind::I);

    Board almostFull{};
    for (int x = 1; x < kBoardWidth; ++x) CHECK(almostFull.Set(x, 19));
    const SearchResult clearOne = solver.Solve(almostFull, {PieceKind::I}, catalog, 1);
    CHECK(clearOne.found);
    CHECK(clearOne.firstMove.linesCleared == 1);
    CHECK(clearOne.totalLinesCleared == 1);

    const SearchResult lookahead = solver.Solve(Board{},
        {PieceKind::I, PieceKind::O}, catalog, 2);
    CHECK(lookahead.found);
    CHECK(lookahead.depth == 2);

    // A missing/unmodeled later piece must not discard a valid current move.
    const SearchResult partial = solver.Solve(Board{},
        {PieceKind::I, PieceKind::T}, catalog, 2);
    CHECK(partial.found);
    CHECK(partial.depth == 1);
    CHECK(partial.firstMove.kind == PieceKind::I);

    SessionStateMachine machine;
    CHECK(machine.State() == RunnerState::Detached);
    CHECK(machine.Apply(RunnerEvent::ProcessAttached));
    CHECK(machine.Apply(RunnerEvent::CalibrationStarted));
    CHECK(machine.Apply(RunnerEvent::CalibrationPassed));
    CHECK(machine.Apply(RunnerEvent::SnapshotAccepted));
    CHECK(machine.Apply(RunnerEvent::PlanReady));
    CHECK(machine.Apply(RunnerEvent::ActionSubmitted));
    CHECK(machine.Apply(RunnerEvent::StateMismatch));
    CHECK(machine.State() == RunnerState::Recovering);
    CHECK(machine.Apply(RunnerEvent::RecoverySucceeded));
    CHECK(machine.State() == RunnerState::Ready);
    CHECK(!machine.Apply(RunnerEvent::PlanReady));
    CHECK(machine.Apply(RunnerEvent::GameOverDetected));
    CHECK(machine.State() == RunnerState::GameOver);
    CHECK(machine.Apply(RunnerEvent::RestartConfirmed));
    CHECK(machine.State() == RunnerState::Calibrating);
    CHECK(machine.Apply(RunnerEvent::CalibrationPassed));
    CHECK(machine.State() == RunnerState::Ready);

    std::cout << "TetrisPilotNext core tests passed\n";
    return EXIT_SUCCESS;
}
