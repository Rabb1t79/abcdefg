#pragma once

#include "tetris/domain.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace tetris {

struct SnapshotRead {
    bool ok{};
    GameSnapshot snapshot{};
    std::string provider;
    std::string diagnostic;
};

class IGameStateSource {
public:
    virtual ~IGameStateSource() = default;
    virtual SnapshotRead ReadSnapshot() = 0;
};

enum class ActionKind : std::uint8_t {
    MoveLeft, MoveRight, RotateClockwise, RotateCounterClockwise,
    SoftDrop, HardDrop, Hold, Pause, Restart
};

struct GameAction {
    ActionKind kind{ActionKind::HardDrop};
    std::uint16_t repeat{1};
};

struct ActionReceipt {
    bool submitted{};
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point submittedAt{};
    std::string backend;
    std::string diagnostic;
};

class IGameActionSink {
public:
    virtual ~IGameActionSink() = default;
    virtual ActionReceipt Dispatch(const GameAction& action) = 0;
};

} // namespace tetris
