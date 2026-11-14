#pragma once

#include "tetris/domain.hpp"

#include <cstddef>
#include <vector>

namespace tetris {

struct Placement {
    PieceKind kind{PieceKind::I};
    int rotation{};
    int x{};
    int y{};
    int linesCleared{};
    Board resultingBoard{};
};

struct SearchResult {
    bool found{};
    Placement firstMove{};
    int totalLinesCleared{};
    std::size_t visitedNodes{};
    int depth{};
};

class ExhaustiveSolver {
public:
    SearchResult Solve(const Board& board,
                       const std::vector<PieceKind>& sequence,
                       const PieceCatalog& catalog,
                       int maxDepth = 3) const;
};

} // namespace tetris
