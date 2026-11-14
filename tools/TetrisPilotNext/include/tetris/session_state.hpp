#pragma once

#include <cstdint>

namespace tetris {

enum class RunnerState : std::uint8_t {
    Detached, Attached, Calibrating, Ready, Planning, Executing,
    Verifying, Recovering, GameOver, Stopped, Faulted
};

enum class RunnerEvent : std::uint8_t {
    ProcessAttached, CalibrationStarted, CalibrationPassed, SnapshotAccepted,
    PlanReady, ActionSubmitted, StateMatched, StateMismatch, RecoverySucceeded,
    GameOverDetected, StopRequested, Fault
};

class SessionStateMachine {
public:
    RunnerState State() const noexcept { return state_; }
    bool Apply(RunnerEvent event) noexcept;

private:
    RunnerState state_{RunnerState::Detached};
};

} // namespace tetris
