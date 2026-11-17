#include "tetris/top_piece_profile.hpp"

#include <algorithm>
#include <utility>

namespace tetris {
namespace {

std::vector<Cell> Normalize(std::vector<Cell> cells) {
    if (cells.empty()) return cells;
    int minX = cells.front().x;
    int minY = cells.front().y;
    for (const Cell& cell : cells) {
        minX = (std::min)(minX, cell.x);
        minY = (std::min)(minY, cell.y);
    }
    for (Cell& cell : cells) {
        cell.x -= minX;
        cell.y -= minY;
    }
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return a.y == b.y ? a.x < b.x : a.y < b.y;
    });
    return cells;
}

std::vector<Cell> RotateClockwise(const std::vector<Cell>& cells) {
    std::vector<Cell> rotated;
    rotated.reserve(cells.size());
    // Same screen-coordinate transform as the repository's TOP visual
    // recognizer: Up is clockwise and positive Y points down the board.
    for (const Cell& cell : cells) rotated.push_back({-cell.y, cell.x});
    return Normalize(std::move(rotated));
}

bool SameShape(const std::vector<Cell>& a, const std::vector<Cell>& b) {
    return a == b;
}

Cell ClockwiseAnchorOffset(const std::vector<Cell>& cells, int boxSize) {
    int maxY = 0;
    int minX = boxSize;
    for (const Cell& cell : cells) {
        maxY = (std::max)(maxY, cell.y);
        minX = (std::min)(minX, cell.x);
    }
    return Cell{boxSize - 1 - maxY, minX};
}

Cell CounterClockwiseAnchorOffset(const std::vector<Cell>& cells, int boxSize) {
    int maxX = 0;
    int minY = boxSize;
    for (const Cell& cell : cells) {
        maxX = (std::max)(maxX, cell.x);
        minY = (std::min)(minY, cell.y);
    }
    return Cell{minY, boxSize - 1 - maxX};
}

} // namespace

bool ConfigureTopPieceProfile(PieceCatalog& catalog,
                              ReachabilityOptions& options) {
    const std::vector<std::pair<PieceKind, std::vector<Cell>>> bases = {
        {PieceKind::I, {{0,1},{1,1},{2,1},{3,1}}},
        {PieceKind::O, {{0,0},{1,0},{0,1},{1,1}}},
        {PieceKind::T, {{1,0},{0,1},{1,1},{2,1}}},
        {PieceKind::S, {{1,0},{2,0},{0,1},{1,1}}},
        {PieceKind::Z, {{0,0},{1,0},{1,1},{2,1}}},
        {PieceKind::J, {{0,0},{0,1},{1,1},{2,1}}},
        {PieceKind::L, {{2,0},{0,1},{1,1},{2,1}}}
    };

    PieceCatalog configuredCatalog;
    ReachabilityOptions configuredOptions;
    configuredOptions.maxVisitedStates = 8192;
    configuredOptions.allowSoftDrop = true;

    for (const auto& entry : bases) {
        const PieceKind kind = entry.first;
        const int boxSize = kind == PieceKind::I ? 4 : 3;
        std::vector<std::vector<Cell>> rotations;
        std::vector<Cell> current = Normalize(entry.second);

        for (int turn = 0; turn < 4; ++turn) {
            bool duplicate = false;
            for (const auto& existing : rotations) {
                if (SameShape(existing, current)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) rotations.push_back(current);
            current = RotateClockwise(current);
        }

        if (!configuredCatalog.Set(kind, rotations)) return false;
        if (rotations.size() <= 1) continue;

        for (std::size_t rotation = 0; rotation < rotations.size(); ++rotation) {
            configuredOptions.rotationTransitions.push_back(RotationTransitionRule{
                kind, static_cast<int>(rotation), true,
                {ClockwiseAnchorOffset(rotations[rotation], boxSize)}
            });
            configuredOptions.rotationTransitions.push_back(RotationTransitionRule{
                kind, static_cast<int>(rotation), false,
                {CounterClockwiseAnchorOffset(rotations[rotation], boxSize)}
            });
        }
    }

    catalog = std::move(configuredCatalog);
    options = std::move(configuredOptions);
    return true;
}

} // namespace tetris
