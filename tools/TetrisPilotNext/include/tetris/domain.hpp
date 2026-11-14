#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tetris {

constexpr int kBoardWidth = 10;
constexpr int kBoardHeight = 20;
constexpr std::uint16_t kFullRowMask = (1u << kBoardWidth) - 1u;

struct Cell {
    int x{};
    int y{};
    friend bool operator==(const Cell& a, const Cell& b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
};

enum class PieceKind : std::uint8_t { I, O, T, S, Z, J, L, Count };

struct Board {
    std::array<std::uint16_t, kBoardHeight> rows{};

    bool Occupied(int x, int y) const noexcept {
        return x >= 0 && x < kBoardWidth && y >= 0 && y < kBoardHeight
            && (rows[static_cast<std::size_t>(y)] & (1u << x)) != 0;
    }

    bool Set(int x, int y, bool value = true) noexcept {
        if (x < 0 || x >= kBoardWidth || y < 0 || y >= kBoardHeight) return false;
        const auto bit = static_cast<std::uint16_t>(1u << x);
        if (value) rows[static_cast<std::size_t>(y)] |= bit;
        else rows[static_cast<std::size_t>(y)] &= static_cast<std::uint16_t>(~bit);
        return true;
    }

    friend bool operator==(const Board& a, const Board& b) noexcept {
        return a.rows == b.rows;
    }
    friend bool operator!=(const Board& a, const Board& b) noexcept {
        return !(a == b);
    }
};

struct PieceRotation {
    std::vector<Cell> cells;
};

struct PieceDefinition {
    PieceKind kind{PieceKind::I};
    std::vector<PieceRotation> rotations;
};

class PieceCatalog {
public:
    bool Set(PieceKind kind, std::vector<std::vector<Cell>> rotations);
    const PieceDefinition* Find(PieceKind kind) const noexcept;

private:
    std::array<std::optional<PieceDefinition>,
        static_cast<std::size_t>(PieceKind::Count)> definitions_{};
};

struct ActivePiece {
    PieceKind kind{PieceKind::I};
    int rotation{};
    int x{};
    int y{};
};

enum class GamePhase : std::uint8_t {
    Unknown, Menu, Ready, Playing, Paused, GameOver, Transition
};

struct GameSnapshot {
    Board lockedBoard{};
    std::optional<ActivePiece> active;
    std::vector<PieceKind> next;
    std::optional<PieceKind> hold;
    GamePhase phase{GamePhase::Unknown};
    std::optional<std::int64_t> score;
    std::uint64_t frameId{};
};

} // namespace tetris
