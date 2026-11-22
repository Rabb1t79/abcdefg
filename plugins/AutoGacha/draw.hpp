#pragma once
#include <cstddef>
#include <cstdint>

namespace gacha {
// Reflected game primitives. Only the fields this plugin reads are declared,
// and every size is asserted against the layout the game ships.
struct Name {
    std::uint32_t id{}, number{};
    friend bool operator==(Name, Name) = default;
};
inline std::uint64_t Pack(Name name) {
    return std::uint64_t{name.id} | (std::uint64_t{name.number} << 32);
}
struct Array {
    std::uintptr_t data{};
    std::int32_t count{}, capacity{};
};
// FParameterWrapperArray and the two-parameter RPC frame the game's
// HTPlayerState exposes for the gashapon system.
struct WrapperArray {
    Array items{};
    std::int32_t cursor{}, padding{};
};
struct Rpc {
    std::int32_t message{}, padding{};
    WrapperArray parameters{};
};
static_assert(sizeof(Name) == 8 && sizeof(Array) == 16 && sizeof(WrapperArray) == 24 && sizeof(Rpc) == 32);
static_assert(offsetof(WrapperArray, cursor) == 16 && offsetof(Rpc, parameters) == 8);

// EGashaponMsgCToS / EGashaponMsgSToC values this plugin distinguishes.
inline constexpr std::int32_t kMsgRaffle = 0;
inline constexpr std::int32_t kMsgSynRaffleResult = 0;
inline constexpr std::int32_t kMsgSynRaffleFailed = 3;

// World service epoch plus the local player object. Handles from
// anomaly.ue5.world are opaque tokens, so the epoch is kept separately.
struct Identity {
    std::uint64_t world{}, world_generation{}, player{}, player_generation{}, epoch{}, epoch_generation{};
    friend bool operator==(const Identity&, const Identity&) = default;
    bool Valid() const {
        return world && world_generation && player && player_generation && epoch && epoch_generation;
    }
};

enum class State { Idle, Armed, Opening, Ready, Submitted, Success, Failed, Unconfirmed, Interrupted };

// One draw per arm. Arming is the only way in, so a finished round cannot be
// spent a second time and a command captured by an older UI frame is rejected
// by the revision/deadline checks the caller performs.
struct Round {
    Identity identity{};
    State state{State::Idle};
    std::uint64_t sequence{}, deadline{};
    std::uint64_t actor{}, actor_generation{};
    Name pool{};
    bool ten{};

    void Reset() { *this = {}; }
    bool Pending() const {
        return state == State::Armed || state == State::Opening || state == State::Ready ||
            state == State::Submitted;
    }
    bool Arm(const Identity& id, std::uint64_t selected, std::uint64_t selected_generation,
             Name pool_name, bool ten_draw, std::uint64_t now) {
        if (!id.Valid() || !selected || !selected_generation || !pool_name.id) return false;
        if (Pending()) return false;
        identity = id;
        actor = selected;
        actor_generation = selected_generation;
        pool = pool_name;
        ten = ten_draw;
        state = State::Armed;
        deadline = now + 5000;
        return true;
    }
    void Opening(std::uint64_t now) {
        if (state == State::Armed) {
            state = State::Opening;
            deadline = now + 8000;
        }
    }
    // The machine panel appeared. The caller only reaches this after proving the
    // panel belongs to the armed machine: through the wall's own selection when
    // the player opened it, or through the click this round issued itself.
    void Present(const Identity& id, std::uint64_t now) {
        if (id != identity || !id.Valid()) return;
        if (state != State::Armed && state != State::Opening) return;
        state = State::Ready;
        deadline = now + 5000;
    }
    bool CanSubmit(const Identity& id) const { return state == State::Ready && id == identity; }
    bool Submit(const Identity& id, std::uint64_t now) {
        if (!CanSubmit(id)) return false;
        state = State::Submitted;
        deadline = now + 15000;
        ++sequence;
        return true;
    }
    void Result(bool ok) {
        if (state == State::Submitted) state = ok ? State::Success : State::Failed;
    }
    // The game requested a draw on its own before this round got there.
    void Interrupt() {
        if (state == State::Armed || state == State::Opening || state == State::Ready)
            state = State::Interrupted;
    }
    void Tick(std::uint64_t now) {
        if (Pending() && now >= deadline) state = State::Unconfirmed;
    }
};
}
