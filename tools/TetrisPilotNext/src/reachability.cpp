#include "tetris/reachability.hpp"

#include <deque>
#include <set>
#include <tuple>
#include <utility>

namespace tetris {
namespace {

using StateKey = std::tuple<int, int, int>;

struct SearchNode {
    int x{};
    int y{};
    int rotation{};
    std::vector<MovementAction> path;
};

bool CanPlace(const Board& board, const PieceRotation& rotation,
              int x, int y) {
    for (const Cell& cell : rotation.cells) {
        const int px = x + cell.x;
        const int py = y + cell.y;
        if (px < 0 || px >= kBoardWidth || py >= kBoardHeight) return false;
        if (py >= 0 && board.Occupied(px, py)) return false;
    }
    return true;
}

bool LockAndClear(const Board& board, const PieceRotation& rotation,
                  int x, int y, Board& result, int& cleared) {
    result = board;
    for (const Cell& cell : rotation.cells) {
        const int px = x + cell.x;
        const int py = y + cell.y;
        if (py < 0 || !result.Set(px, py, true)) return false;
    }

    Board compacted{};
    int write = kBoardHeight - 1;
    cleared = 0;
    for (int read = kBoardHeight - 1; read >= 0; --read) {
        if (result.rows[static_cast<std::size_t>(read)] == kFullRowMask) {
            ++cleared;
        } else {
            compacted.rows[static_cast<std::size_t>(write--)] =
                result.rows[static_cast<std::size_t>(read)];
        }
    }
    result = compacted;
    return true;
}

const RotationTransitionRule* FindRule(
    const ReachabilityOptions& options, PieceKind kind,
    int fromRotation, bool clockwise) {
    for (const RotationTransitionRule& rule : options.rotationTransitions) {
        if (rule.kind == kind && rule.fromRotation == fromRotation &&
            rule.clockwise == clockwise && !rule.anchorOffsets.empty()) {
            return &rule;
        }
    }
    return nullptr;
}

} // namespace

ReachabilityResult ReachabilityPlanner::Enumerate(
    const Board& board, const ActivePiece& active,
    const PieceCatalog& catalog, const ReachabilityOptions& options) const {
    ReachabilityResult result;
    const PieceDefinition* definition = catalog.Find(active.kind);
    if (!definition || definition->rotations.empty() ||
        active.rotation < 0 ||
        active.rotation >= static_cast<int>(definition->rotations.size())) {
        return result;
    }

    const PieceRotation& initialRotation =
        definition->rotations[static_cast<std::size_t>(active.rotation)];
    if (initialRotation.cells.size() != 4 ||
        !CanPlace(board, initialRotation, active.x, active.y)) {
        return result;
    }
    result.validStart = true;

    if (options.maxVisitedStates == 0) {
        result.truncated = true;
        return result;
    }

    std::deque<SearchNode> queue;
    std::set<StateKey> visited;
    std::set<StateKey> landingKeys;

    queue.push_back(SearchNode{active.x, active.y, active.rotation, {}});
    visited.emplace(active.x, active.y, active.rotation);
    result.visitedStates = 1;

    const int rotationCount = static_cast<int>(definition->rotations.size());
    auto enqueue = [&](SearchNode&& candidate) {
        const StateKey key{candidate.x, candidate.y, candidate.rotation};
        if (visited.find(key) != visited.end()) return true;
        if (result.visitedStates >= options.maxVisitedStates) {
            result.truncated = true;
            return false;
        }
        visited.emplace(key);
        ++result.visitedStates;
        queue.push_back(std::move(candidate));
        return true;
    };

    while (!queue.empty() && !result.truncated) {
        SearchNode current = std::move(queue.front());
        queue.pop_front();
        const PieceRotation& shape =
            definition->rotations[static_cast<std::size_t>(current.rotation)];

        // Hard drop is a terminal edge from every reachable state. Record
        // only landings that are fully inside the visible board.
        int landingY = current.y;
        while (CanPlace(board, shape, current.x, landingY + 1)) ++landingY;
        bool aboveTop = false;
        for (const Cell& cell : shape.cells) {
            if (landingY + cell.y < 0) {
                aboveTop = true;
                break;
            }
        }
        const StateKey landingKey{current.x, landingY, current.rotation};
        if (!aboveTop && landingKeys.insert(landingKey).second) {
            Board locked{};
            int lines = 0;
            if (LockAndClear(board, shape, current.x, landingY, locked, lines)) {
                ReachablePlacement landing;
                landing.placement = Placement{
                    active.kind, current.rotation, current.x, landingY, lines, locked
                };
                landing.path = current.path;
                landing.path.push_back(MovementAction::HardDrop);
                result.landings.push_back(std::move(landing));
            }
        }

        SearchNode next = current;
        if (CanPlace(board, shape, current.x - 1, current.y)) {
            next.x = current.x - 1;
            next.path.push_back(MovementAction::MoveLeft);
            if (!enqueue(std::move(next))) break;
        }

        next = current;
        if (CanPlace(board, shape, current.x + 1, current.y)) {
            next.x = current.x + 1;
            next.path.push_back(MovementAction::MoveRight);
            if (!enqueue(std::move(next))) break;
        }

        if (options.allowSoftDrop &&
            CanPlace(board, shape, current.x, current.y + 1)) {
            next = current;
            ++next.y;
            next.path.push_back(MovementAction::SoftDrop);
            if (!enqueue(std::move(next))) break;
        }

        if (options.allowGravitySteps &&
            CanPlace(board, shape, current.x, current.y + 1)) {
            next = current;
            ++next.y;
            next.path.push_back(MovementAction::WaitForGravity);
            if (!enqueue(std::move(next))) break;
        }

        if (rotationCount > 1) {
            const RotationTransitionRule* clockwise =
                FindRule(options, active.kind, current.rotation, true);
            if (clockwise) {
                const int targetRotation = (current.rotation + 1) % rotationCount;
                for (const Cell& offset : clockwise->anchorOffsets) {
                    const int targetX = current.x + offset.x;
                    const int targetY = current.y + offset.y;
                    const PieceRotation& targetShape =
                        definition->rotations[static_cast<std::size_t>(targetRotation)];
                    if (!CanPlace(board, targetShape, targetX, targetY)) continue;
                    next = current;
                    next.x = targetX;
                    next.y = targetY;
                    next.rotation = targetRotation;
                    next.path.push_back(MovementAction::RotateClockwise);
                    if (!enqueue(std::move(next))) break;
                    break; // First collision-free offset wins, as in a kick test.
                }
                if (result.truncated) break;
            }

            const RotationTransitionRule* counterClockwise =
                FindRule(options, active.kind, current.rotation, false);
            if (counterClockwise) {
                const int targetRotation =
                    (current.rotation + rotationCount - 1) % rotationCount;
                for (const Cell& offset : counterClockwise->anchorOffsets) {
                    const int targetX = current.x + offset.x;
                    const int targetY = current.y + offset.y;
                    const PieceRotation& targetShape =
                        definition->rotations[static_cast<std::size_t>(targetRotation)];
                    if (!CanPlace(board, targetShape, targetX, targetY)) continue;
                    next = current;
                    next.x = targetX;
                    next.y = targetY;
                    next.rotation = targetRotation;
                    next.path.push_back(MovementAction::RotateCounterClockwise);
                    if (!enqueue(std::move(next))) break;
                    break;
                }
            }
        }
    }

    return result;
}

} // namespace tetris
