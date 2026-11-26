#pragma once

// The Tycoon's Deck (铺满全城) run loop as a pure decision function, so the ordering of
// one native request per game phase can be reviewed and tested without a game process.

#include <cstddef>
#include <cstdint>

namespace tycoon {

// UHTBalatroLikeComponent::EHTBalatroPhase
enum class Phase : std::int32_t {
    idle = 0,
    blind_selection = 1,
    playing = 2,
    blind_cleared = 3,
    shop = 4,
    shop_cleared = 5,
    game_over = 6,
    game_over_failed = 7,
    settlement_animating = 8,
};

enum class Action : std::size_t {
    none = 0,
    start_blind,
    skip_blind,
    play_hand,
    finish_blind,
    confirm_pack,
    skip_shop,
    manual_settlement,
    stop,
};

struct Rules final {
    bool skip_blinds{true};
    bool skip_shop{true};
    bool confirm_packs{true};
    // Raises the local blind score to its target before the cash-out request, which is the
    // one lever a client still owns once the hand has been played.
    bool score_assist{true};
};

struct DeckState final {
    std::int32_t phase{-1};
    std::int32_t plays_left{};
    std::int32_t discards_left{};
    std::int32_t pack_pick_count{};
    bool can_finish{};
    bool can_skip_blind{};
    bool pending_pack{};
};

// One call per observation. Every branch uses a request the game itself exposes on the
// component, so the run advances exactly as if the matching button had been pressed.
[[nodiscard]] constexpr Action NextAction(const DeckState& state, const Rules& rules) noexcept {
    switch (state.phase) {
    case static_cast<std::int32_t>(Phase::blind_selection):
        return rules.skip_blinds && state.can_skip_blind ? Action::skip_blind : Action::start_blind;
    case static_cast<std::int32_t>(Phase::playing):
        // A pending pack blocks every other request, so it is resolved first.
        if (state.pending_pack && rules.confirm_packs) return Action::confirm_pack;
        if (state.can_finish) return Action::finish_blind;
        if (state.plays_left > 0) return Action::play_hand;
        return Action::manual_settlement;
    case static_cast<std::int32_t>(Phase::shop):
    case static_cast<std::int32_t>(Phase::shop_cleared):
        if (state.pending_pack && rules.confirm_packs) return Action::confirm_pack;
        return rules.skip_shop ? Action::skip_shop : Action::none;
    case static_cast<std::int32_t>(Phase::game_over):
    case static_cast<std::int32_t>(Phase::game_over_failed):
        return Action::stop;
    default:
        return Action::none;
    }
}

}  // namespace tycoon
