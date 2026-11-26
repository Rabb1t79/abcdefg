// 纯逻辑验证：不接触游戏进程，只验证“阶段 → 下一步动作”的判定表和配置解析。
#include "flow.hpp"
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void Check(bool condition, const char* what) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << what << '\n';
}
using tycoon::Action;
using tycoon::Phase;
Action Next(Phase phase, bool canFinish = false, bool canSkip = false, bool pack = false,
    std::int32_t picks = 0, std::int32_t hand = 0, std::int32_t cells = 0) {
    return tycoon::Decide({phase, canFinish, canSkip, pack, picks, hand, cells}).action;
}
}

int main() {
    Check(Next(Phase::Idle) == Action::StartGame, "idle starts a run");
    Check(Next(Phase::BlindSelection, false, true) == Action::SkipBlind, "skippable blind is skipped");
    Check(Next(Phase::BlindSelection) == Action::StartBlind, "locked blind is started");
    Check(Next(Phase::Playing, true) == Action::FinishBlind, "finishable blind ends directly");
    Check(Next(Phase::Playing, false, false, false, 0, 3, 25) == Action::PlaceBuilding, "hand and cells place a building");
    Check(Next(Phase::Playing, false, false, false, 0, 0, 25) == Action::Settle, "empty hand settles the board");
    Check(Next(Phase::Playing, false, false, false, 0, 3, 0) == Action::Settle, "full board settles the board");
    Check(Next(Phase::BlindCleared) == Action::FinishBlind, "cleared blind is finalized");
    Check(Next(Phase::Shop) == Action::SkipShop, "shop is skipped");
    Check(Next(Phase::ShopCleared) == Action::SkipShop, "cleared shop is skipped");
    Check(Next(Phase::Playing, true, false, true, 2, 3, 25) == Action::ConfirmPack, "pending pack is confirmed first");
    Check(Next(Phase::GameOver) == Action::None, "game over is terminal");
    Check(Next(Phase::GameOverFailed) == Action::None, "failed run is terminal");
    Check(Next(Phase::SettlementAnimating) == Action::None, "settlement animation is terminal");

    const auto parsed = tycoon::ParseSettings("{\"enabled\":true,\"fastFinish\":false}");
    Check(parsed.enabled && !parsed.fastFinish, "settings round trip");
    const auto defaults = tycoon::ParseSettings("{}");
    Check(!defaults.enabled && defaults.fastFinish, "settings defaults");
    const auto round = tycoon::ParseSettings(tycoon::FormatSettings({true, true}));
    Check(round.enabled && round.fastFinish, "formatted settings parse back");

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "tycoondeck flow checks passed\n";
    return EXIT_SUCCESS;
}
