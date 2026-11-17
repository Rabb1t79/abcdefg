#pragma once

#include "tetris/solver.hpp"

#include <cstddef>
#include <vector>

namespace tetris {

enum class MovementAction : std::uint8_t {
    MoveLeft,
    MoveRight,
    SoftDrop,
    WaitForGravity,
    RotateClockwise,
    RotateCounterClockwise,
    HardDrop
};

// Offsets are measured in board cells and applied to the normalized
// bounding-box origin when changing rotation state. Keep only offsets verified
// for the target client, in the order that client tests them.
struct RotationTransitionRule {
    PieceKind kind{PieceKind::I};
    int fromRotation{};
    bool clockwise{};
    std::vector<Cell> anchorOffsets;
};

struct ReachabilityOptions {
    std::size_t maxVisitedStates{8192};
    bool allowSoftDrop{true};
    bool allowGravitySteps{false};
    std::vector<RotationTransitionRule> rotationTransitions;
};

struct ReachablePlacement {
    Placement placement{};
    // The last action is always HardDrop. Preceding actions are a shortest
    // path in the configured discrete movement graph.
    std::vector<MovementAction> path;
};

struct ReachabilityResult {
    bool validStart{};
    bool truncated{};
    std::size_t visitedStates{};
    std::vector<ReachablePlacement> landings;
};

class ReachabilityPlanner {
public:
    ReachabilityResult Enumerate(const Board& board,
                                 const ActivePiece& active,
                                 const PieceCatalog& catalog,
                                 const ReachabilityOptions& options = {}) const;
};

} // namespace tetris
