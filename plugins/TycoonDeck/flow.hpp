#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace tycoon {
// 与 HTGame.EHTBalatroPhase 一致；只列插件会分支的取值。
enum class Phase : std::uint8_t {
    Idle = 0, BlindSelection = 1, Playing = 2, BlindCleared = 3,
    Shop = 4, ShopCleared = 5, GameOver = 6, GameOverFailed = 7, SettlementAnimating = 8,
};
enum class Action : std::uint8_t {
    None, StartGame, StartBlind, SkipBlind, FinishBlind, PlaceBuilding, SkipShop, ConfirmPack, Settle,
};
struct Observation {
    Phase phase{};
    bool canFinish{};
    bool canSkip{};
    bool pendingPack{};
    std::int32_t pickCount{};
    std::int32_t handCount{};
    std::int32_t cellCount{};
};
// key 是本地化目录的键，fallback 是代码里的英文回退；两者必须同时给，目录缺项时显示回退而不是键名。
struct Decision {
    Action action{};
    const char* key{};
    const char* fallback{};
};
struct Settings {
    bool enabled{};
    bool fastFinish{true};
};

// 这个玩法是“把建筑卡放上格子 → 结算计分”，没有扑克式出牌，所以 Playing 阶段只做放卡或结算。
inline Decision Decide(const Observation& value) {
    if (value.pendingPack && value.pickCount > 0) return {Action::ConfirmPack, "status.pack", "Confirming the card pack choice."};
    switch (value.phase) {
    case Phase::Idle: return {Action::StartGame, "status.starting", "Requesting a new run."};
    case Phase::BlindSelection:
        return value.canSkip ? Decision{Action::SkipBlind, "status.skip_blind", "Skipping the current blind."}
                             : Decision{Action::StartBlind, "status.start_blind", "Starting the current blind."};
    case Phase::Playing:
        if (value.canFinish) return {Action::FinishBlind, "status.finish_blind", "Ending the current blind."};
        if (value.handCount > 0 && value.cellCount > 0) return {Action::PlaceBuilding, "status.place_building", "Placing a building card."};
        return {Action::Settle, "status.settle", "Settling the current board."};
    case Phase::BlindCleared: return {Action::FinishBlind, "status.finish_blind", "Ending the current blind."};
    case Phase::Shop:
    case Phase::ShopCleared: return {Action::SkipShop, "status.skip_shop", "Skipping the shop."};
    default: return {};
    }
}
inline const char* PhaseKey(Phase phase) {
    switch (phase) {
    case Phase::Idle: return "phase.idle";
    case Phase::BlindSelection: return "phase.blind_selection";
    case Phase::Playing: return "phase.playing";
    case Phase::BlindCleared: return "phase.blind_cleared";
    case Phase::Shop: return "phase.shop";
    case Phase::ShopCleared: return "phase.shop_cleared";
    case Phase::GameOver: return "phase.game_over";
    case Phase::GameOverFailed: return "phase.game_over_failed";
    case Phase::SettlementAnimating: return "phase.settlement";
    default: return "phase.idle";
    }
}
inline const char* PhaseFallback(Phase phase) {
    switch (phase) {
    case Phase::Idle: return "Idle";
    case Phase::BlindSelection: return "Blind selection";
    case Phase::Playing: return "Playing";
    case Phase::BlindCleared: return "Blind cleared";
    case Phase::Shop: return "Shop";
    case Phase::ShopCleared: return "Shop cleared";
    case Phase::GameOver: return "Run ended";
    case Phase::GameOverFailed: return "Run failed";
    case Phase::SettlementAnimating: return "Settling";
    default: return "Idle";
    }
}
// 配置文档由本插件自己写，解析只需认这两项；未知字段忽略。
inline Settings ParseSettings(std::string_view document) {
    Settings value{};
    const auto flag = [&](std::string_view key, bool fallback) {
        const auto at = document.find("\"" + std::string(key) + "\"");
        if (at == std::string_view::npos) return fallback;
        const auto colon = document.find(':', at);
        if (colon == std::string_view::npos) return fallback;
        const auto rest = document.substr(colon + 1, 16);
        if (rest.find("true") != std::string_view::npos) return true;
        if (rest.find("false") != std::string_view::npos) return false;
        return fallback;
    };
    value.enabled = flag("enabled", false);
    value.fastFinish = flag("fastFinish", true);
    return value;
}
inline std::string FormatSettings(const Settings& value) {
    return std::string{"{\"enabled\":"} + (value.enabled ? "true" : "false") +
        ",\"fastFinish\":" + (value.fastFinish ? "true" : "false") + "}";
}
}
