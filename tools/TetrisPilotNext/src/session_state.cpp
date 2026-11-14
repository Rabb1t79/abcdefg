#include "tetris/session_state.hpp"

namespace tetris {

bool SessionStateMachine::Apply(RunnerEvent event) noexcept {
    const RunnerState s = state_;
    switch (event) {
    case RunnerEvent::ProcessAttached:
        if (s != RunnerState::Detached && s != RunnerState::Stopped) return false;
        state_ = RunnerState::Attached; return true;
    case RunnerEvent::CalibrationStarted:
        if (s != RunnerState::Attached) return false;
        state_ = RunnerState::Calibrating; return true;
    case RunnerEvent::CalibrationPassed:
        if (s != RunnerState::Calibrating) return false;
        state_ = RunnerState::Ready; return true;
    case RunnerEvent::SnapshotAccepted:
        if (s != RunnerState::Ready && s != RunnerState::Verifying &&
            s != RunnerState::Recovering) return false;
        state_ = RunnerState::Planning; return true;
    case RunnerEvent::PlanReady:
        if (s != RunnerState::Planning) return false;
        state_ = RunnerState::Executing; return true;
    case RunnerEvent::ActionSubmitted:
        if (s != RunnerState::Executing) return false;
        state_ = RunnerState::Verifying; return true;
    case RunnerEvent::StateMatched:
        if (s != RunnerState::Verifying) return false;
        state_ = RunnerState::Ready; return true;
    case RunnerEvent::StateMismatch:
        if (s != RunnerState::Verifying) return false;
        state_ = RunnerState::Recovering; return true;
    case RunnerEvent::RecoverySucceeded:
        if (s != RunnerState::Recovering) return false;
        state_ = RunnerState::Ready; return true;
    case RunnerEvent::GameOverDetected:
        if (s != RunnerState::Verifying && s != RunnerState::Ready &&
            s != RunnerState::Planning && s != RunnerState::Executing &&
            s != RunnerState::Recovering) return false;
        state_ = RunnerState::GameOver; return true;
    case RunnerEvent::RestartConfirmed:
        if (s != RunnerState::GameOver) return false;
        state_ = RunnerState::Calibrating; return true;
    case RunnerEvent::StopRequested:
        if (s == RunnerState::Faulted || s == RunnerState::Stopped) return false;
        state_ = RunnerState::Stopped; return true;
    case RunnerEvent::Fault:
        if (s == RunnerState::Stopped) return false;
        state_ = RunnerState::Faulted; return true;
    }
    return false;
}

} // namespace tetris
