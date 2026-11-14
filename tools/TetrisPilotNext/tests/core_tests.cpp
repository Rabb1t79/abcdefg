#include "tetris/domain.hpp"
#include "tetris/session_state.hpp"
#include "tetris/solver.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace tetris;

static PieceCatalog MakeCatalog() {
    PieceCatalog catalog;
    const std::vector<Cell> horizontal{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    const std::vector<Cell> vertical{{0, 0}, {0, 1}, {0, 2}, {0, 3}};
    assert(catalog.Set(PieceKind::I, {horizontal, vertical}));
    const std::vector<Cell> square{{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    assert(catalog.Set(PieceKind::O, {square}));
    return catalog;
}

int main() {
    Board board{};
    assert(board.Set(3, 19));
    assert(board.Occupied(3, 19));
    assert(!board.Set(-1, 0));

    const PieceCatalog catalog = MakeCatalog();
    ExhaustiveSolver solver;

    const SearchResult emptyBoard = solver.Solve(Board{}, {PieceKind::I}, catalog, 1);
    assert(emptyBoard.found);
    assert(emptyBoard.depth == 1);
    assert(emptyBoard.visitedNodes > 0);
    assert(emptyBoard.firstMove.kind == PieceKind::I);

    Board almostFull{};
    for (int x = 1; x < kBoardWidth; ++x) assert(almostFull.Set(x, 19));
    const SearchResult clearOne = solver.Solve(almostFull, {PieceKind::I}, catalog, 1);
    assert(clearOne.found);
    assert(clearOne.firstMove.linesCleared == 1);
    assert(clearOne.totalLinesCleared == 1);

    const SearchResult lookahead = solver.Solve(Board{},
        {PieceKind::I, PieceKind::O}, catalog, 2);
    assert(lookahead.found);
    assert(lookahead.depth == 2);

    SessionStateMachine machine;
    assert(machine.State() == RunnerState::Detached);
    assert(machine.Apply(RunnerEvent::ProcessAttached));
    assert(machine.Apply(RunnerEvent::CalibrationStarted));
    assert(machine.Apply(RunnerEvent::CalibrationPassed));
    assert(machine.Apply(RunnerEvent::SnapshotAccepted));
    assert(machine.Apply(RunnerEvent::PlanReady));
    assert(machine.Apply(RunnerEvent::ActionSubmitted));
    assert(machine.Apply(RunnerEvent::StateMismatch));
    assert(machine.State() == RunnerState::Recovering);
    assert(machine.Apply(RunnerEvent::RecoverySucceeded));
    assert(machine.State() == RunnerState::Ready);
    assert(!machine.Apply(RunnerEvent::PlanReady));

    std::cout << "TetrisPilotNext core tests passed\n";
    return 0;
}
