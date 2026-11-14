#include "tetris/solver.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>
#include <utility>

namespace tetris {
namespace {

struct BoardQuality {
    int holes{};
    int maxHeight{};
    int aggregateHeight{};
    int bumpiness{};
};

BoardQuality Measure(const Board& board) {
    BoardQuality q{};
    std::array<int, kBoardWidth> heights{};
    for (int x = 0; x < kBoardWidth; ++x) {
        int first = kBoardHeight;
        for (int y = 0; y < kBoardHeight; ++y) {
            if (board.Occupied(x, y)) {
                first = y;
                break;
            }
        }
        heights[static_cast<std::size_t>(x)] = kBoardHeight - first;
        q.maxHeight = (std::max)(q.maxHeight, heights[static_cast<std::size_t>(x)]);
        q.aggregateHeight += heights[static_cast<std::size_t>(x)];
        if (first < kBoardHeight) {
            for (int y = first; y < kBoardHeight; ++y) {
                if (!board.Occupied(x, y)) ++q.holes;
            }
        }
    }
    for (int x = 0; x + 1 < kBoardWidth; ++x) {
        q.bumpiness += std::abs(heights[static_cast<std::size_t>(x)] -
                                heights[static_cast<std::size_t>(x + 1)]);
    }
    return q;
}

bool Better(int linesA, const BoardQuality& a, int rotA, int xA,
            int linesB, const BoardQuality& b, int rotB, int xB) {
    if (linesA != linesB) return linesA > linesB;
    if (a.holes != b.holes) return a.holes < b.holes;
    if (a.maxHeight != b.maxHeight) return a.maxHeight < b.maxHeight;
    if (a.aggregateHeight != b.aggregateHeight) return a.aggregateHeight < b.aggregateHeight;
    if (a.bumpiness != b.bumpiness) return a.bumpiness < b.bumpiness;
    if (rotA != rotB) return rotA < rotB;
    return xA < xB;
}

bool CanPlace(const Board& board, const std::vector<Cell>& shape, int x, int y) {
    for (const Cell& cell : shape) {
        const int px = x + cell.x;
        const int py = y + cell.y;
        if (px < 0 || px >= kBoardWidth || py >= kBoardHeight) return false;
        if (py >= 0 && board.Occupied(px, py)) return false;
    }
    return true;
}

bool PlaceAndClear(const Board& board, const std::vector<Cell>& shape,
                   int x, int y, Board& out, int& cleared) {
    out = board;
    for (const Cell& cell : shape) {
        const int px = x + cell.x;
        const int py = y + cell.y;
        if (py < 0 || !out.Set(px, py, true)) return false;
    }

    Board compact{};
    int write = kBoardHeight - 1;
    for (int read = kBoardHeight - 1; read >= 0; --read) {
        const bool full = board.rows[static_cast<std::size_t>(read)] == kFullRowMask ||
                          out.rows[static_cast<std::size_t>(read)] == kFullRowMask;
        if (!full) compact.rows[static_cast<std::size_t>(write--)] =
            out.rows[static_cast<std::size_t>(read)];
    }
    cleared = write + 1;
    out = compact;
    return true;
}

std::vector<Placement> Enumerate(const Board& board, PieceKind kind,
                                 const PieceCatalog& catalog,
                                 std::size_t& visited) {
    std::vector<Placement> result;
    const PieceDefinition* definition = catalog.Find(kind);
    if (!definition) return result;

    for (std::size_t rotation = 0; rotation < definition->rotations.size(); ++rotation) {
        const auto& shape = definition->rotations[rotation].cells;
        int maxX = 0;
        int maxY = 0;
        for (const Cell& c : shape) {
            maxX = (std::max)(maxX, c.x);
            maxY = (std::max)(maxY, c.y);
        }
        for (int x = -maxX; x < kBoardWidth; ++x) {
            int y = -maxY - 1;
            while (CanPlace(board, shape, x, y + 1)) ++y;
            ++visited;
            bool aboveTop = false;
            for (const Cell& c : shape) if (y + c.y < 0) aboveTop = true;
            if (aboveTop || !CanPlace(board, shape, x, y)) continue;

            Board placed{};
            int lines = 0;
            if (PlaceAndClear(board, shape, x, y, placed, lines)) {
                result.push_back(Placement{kind, static_cast<int>(rotation), x, y, lines, placed});
            }
        }
    }
    return result;
}

struct BestPath {
    bool found{};
    Placement first{};
    int lines{};
    Board finalBoard{};
    BoardQuality quality{};
    int firstRotation{};
    int firstX{};
};

void Search(const Board& board, const std::vector<PieceKind>& sequence,
            const PieceCatalog& catalog, int index, int depth,
            int linesSoFar, const Placement* first,
            std::size_t& visited, BestPath& best) {
    if (index >= depth) {
        const BoardQuality quality = Measure(board);
        const int rotation = first ? first->rotation : 0;
        const int x = first ? first->x : 0;
        if (!best.found || Better(linesSoFar, quality, rotation, x,
                                  best.lines, best.quality,
                                  best.firstRotation, best.firstX)) {
            best.found = true;
            if (first) best.first = *first;
            best.lines = linesSoFar;
            best.finalBoard = board;
            best.quality = quality;
            best.firstRotation = rotation;
            best.firstX = x;
        }
        return;
    }

    const auto placements = Enumerate(board, sequence[static_cast<std::size_t>(index)],
                                      catalog, visited);
    for (const Placement& placement : placements) {
        const Placement* root = first ? first : &placement;
        Search(placement.resultingBoard, sequence, catalog, index + 1, depth,
               linesSoFar + placement.linesCleared, root, visited, best);
    }
}

} // namespace

bool PieceCatalog::Set(PieceKind kind, std::vector<std::vector<Cell>> rotations) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= definitions_.size() || rotations.empty()) return false;

    PieceDefinition definition;
    definition.kind = kind;
    for (auto& cells : rotations) {
        if (cells.size() != 4) return false;
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
        if (std::adjacent_find(cells.begin(), cells.end()) != cells.end()) return false;
        bool duplicate = false;
        for (const auto& existing : definition.rotations) {
            if (existing.cells == cells) duplicate = true;
        }
        if (!duplicate) definition.rotations.push_back(PieceRotation{std::move(cells)});
    }
    if (definition.rotations.empty()) return false;
    definitions_[index] = std::move(definition);
    return true;
}

const PieceDefinition* PieceCatalog::Find(PieceKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= definitions_.size() || !definitions_[index]) return nullptr;
    return &*definitions_[index];
}

SearchResult ExhaustiveSolver::Solve(const Board& board,
                                     const std::vector<PieceKind>& sequence,
                                     const PieceCatalog& catalog,
                                     int maxDepth) const {
    SearchResult result{};
    if (sequence.empty() || maxDepth <= 0) return result;
    const int depth = (std::min)(maxDepth, static_cast<int>(sequence.size()));
    BestPath best{};
    Search(board, sequence, catalog, 0, depth, 0, nullptr,
           result.visitedNodes, best);
    if (!best.found) return result;

    result.found = true;
    result.firstMove = best.first;
    result.totalLinesCleared = best.lines;
    result.depth = depth;
    return result;
}

} // namespace tetris
