#include "tetris/domain.hpp"
#include "tetris/game_ports.hpp"
#include "tetris/session_state.hpp"
#include "tetris/solver.hpp"

#include <cstdlib>
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
