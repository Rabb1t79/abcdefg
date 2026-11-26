#pragma once

// Binds the live UHTBalatroLikeComponent and talks to it through reflected functions
// only: getters read the component's own state, requests are the same entry points the
// minigame UI binds its buttons to. No field of the minigame is written directly.

#include "deck_rules.hpp"
#include "native_access.hpp"

#include <anomaly/sdk/cpp.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace tycoon {

struct FunctionSpec final {
    std::string_view name;
    std::uint8_t parameters;   // UFunction::NumParms
    std::uint16_t frame_size;  // UFunction::ParmsSize
};

enum class Request : std::size_t {
    start_blind = 0,
    skip_blind,
    play_hand,
    finish_blind,
    skip_shop,
    manual_settlement,
    confirm_pack,
    count,
};

enum class Query : std::size_t {
    phase = 0,
    plays_left,
    discards_left,
    money,
    current_score,
    target_score,
    act,
    floor,
    can_finish,
    can_skip_blind,
    pending_pack,
    pack_pick_count,
    count,
};

inline constexpr std::array<FunctionSpec, static_cast<std::size_t>(Request::count)> kRequests{{
    {"RequestStartCurrentBlind", 0, 0},
    {"RequestSkipCurrentBlind", 0, 0},
    {"RequestPlayAllHandCards", 0, 0},
    {"RequestFinishCurrentBlind", 0, 0},
    {"RequestSkipShop", 0, 0},
    {"RequestManualSettlement", 0, 0},
    // RequestConfirmPackSelection(const TArray<int32>& InSelectedIndices)
    {"RequestConfirmPackSelection", 1, 16},
}};

inline constexpr std::array<FunctionSpec, static_cast<std::size_t>(Query::count)> kQueries{{
    {"GetCurrentPhase", 1, 1},
    {"GetPlaysLeft", 1, 4},
    {"GetDiscardsLeft", 1, 4},
    {"GetMoney", 1, 4},
    {"GetCurrentScore", 1, 4},
    {"GetBlindTargetScore", 1, 4},
    {"GetCurrentAct", 1, 4},
    {"GetCurrentFloor", 1, 4},
    {"CanFinishCurrentBlind", 1, 1},
    {"CanSkipCurrentBlind", 1, 1},
    {"IsPendingPackSelection", 1, 1},
    {"GetPendingPackPickCount", 1, 4},
}};

inline constexpr std::size_t kMaximumPackPicks = 8;
inline constexpr std::string_view kComponentClass = "HTBalatroLikeComponent";
inline constexpr std::string_view kPlayerStateClass = "HTPlayerState";
inline constexpr std::string_view kComponentField = "BalatroLikeComponentPtr";
inline constexpr std::string_view kPlayerStateField = "PlayerState";
inline constexpr std::string_view kBlindScoreField = "CurrentBlindScore";
// The ids this run reports on completion. UHTBalatroLikeComponent fills them from the
// active OpActivity_BalatroLike configuration.
inline constexpr std::string_view kQuestIdField = "BalatroQuestIDs";
inline constexpr std::string_view kQuestComponentField = "QuestComponent";
inline constexpr std::string_view kQuestsInProgressField = "QuestsInProgress";
// Measured from the live row layout: ids sit at row offset 0 and rows are 0xE0 bytes apart.
inline constexpr std::uintptr_t kProgressStride = 0xE0;
inline constexpr std::size_t kMaximumReportQuests = 16;
// AHTPlayerState::ServerSubmitQuest —— the server RPC the minigame itself calls when a
// run is won, so submitting it is the report path rather than a simulation of play.
inline constexpr FunctionSpec kSubmitQuestFunction{"ServerSubmitQuest", 3, 20};
// UHTBalatroLikeComponent::GetHandCards —— returns TArray<FHTBalatroCardInstance>.
inline constexpr FunctionSpec kGetHandCardsFunction{"GetHandCards", 1, 16};
inline constexpr std::uintptr_t kCardInstanceStride = 0xA8;

struct TArrayHeader final {
    const void* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(TArrayHeader) == 16);

// The RPC layer only carries the id form of a play, so the hand's instance ids are what
// the server accepts.
inline constexpr FunctionSpec kPlayAllByIdFunction{"RequestPlayAllHandCardsByID", 1, 16};

// NPC challenge entry: the flow list is what the lobby's challenge button selects from, so
// starting one of these is the entry that binds the activity objectives.
inline constexpr FunctionSpec kGetAllNpcIdsFunction{"GetAllNPCIDs", 1, 16};
inline constexpr FunctionSpec kSelectNpcQuestFunction{"RequestSelectNPCChallengeQuest", 1, 12};
inline constexpr FunctionSpec kStartNpcChallengeFunction{"RequestStartNPCChallenge", 1, 8};

struct NpcSelectFrame final {
    FName flow{};
    std::uint8_t selected{};
    std::uint8_t reserved[3]{};
};
static_assert(sizeof(NpcSelectFrame) == 12);

struct GuidArray final {
    const Guid* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(GuidArray) == 16);
// UHTPlayerState::RequestBalatroLikeComponent 鈥斺€?the player state replicates the component
// on demand, so this is the native way to obtain it when it is not replicated yet.
inline constexpr FunctionSpec kRequestComponentFunction{"RequestBalatroLikeComponent", 0, 0};

struct TArrayInt32 final {
    const std::int32_t* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(TArrayInt32) == 16);

// TArray<FName> as the component stores its reportable quest ids.
struct QuestIdArray final {
    const FName* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};
static_assert(sizeof(QuestIdArray) == 16);

// UHTPlayerState::ServerSubmitQuest(const FName&, const FName&, int32)
struct SubmitQuestFrame final {
    FName quest{};
    FName group{};
    std::int32_t quality{};
};
static_assert(sizeof(SubmitQuestFrame) == 20);

class DeckBridge final {
public:
    DeckBridge(const Reader& reader, const AnomalyUe5ObjectsServiceV1* objects,
        const AnomalyNtePlayerServiceV1* players, const AnomalyUe5ProcessEventServiceV1* events,
        ObjectDomain& domain) noexcept
        : reader_(reader), objects_(objects), players_(players), events_(events), domain_(domain) {}

    void Reset() noexcept {
        component_ = {};
        player_state_ = {};
        submit_quest_ = {};
        get_hand_cards_ = {};
        play_all_by_id_ = {};
        get_all_npc_ids_ = {};
        select_npc_quest_ = {};
        start_npc_challenge_ = {};
        submit_quest_retry_ = {};
        quest_id_offset_ = -1;
        quest_component_offset_ = -1;
        progress_offset_ = -1;
        functions_ready_ = false;
        request_component_function_ = {};
        for (auto& handle : requests_) handle = {};
        for (auto& handle : queries_) handle = {};
        for (auto& deadline : request_retry_) deadline = {};
        for (auto& deadline : query_retry_) deadline = {};
        player_state_offset_ = -1;
        component_offset_ = -1;
        blind_score_offset_ = -1;
        component_requested_at_ = {};
    }

    // Functions first: they are class-level and survive the component being re-created,
    // while the component itself is re-read on every call because the player can change.
    [[nodiscard]] bool Refresh() {
        if (!functions_ready_ && !ResolveFunctions()) return false;
        return ResolveComponent();
    }

    [[nodiscard]] bool Read(DeckState& state) {
        if (component_.id == 0) return false;
        // GetCurrentPhase returns the EHTBalatroPhase enum, so its frame is one byte wide.
        std::uint8_t phase{};
        if (!QueryValue(Query::phase, phase)) return false;
        std::int32_t plays{};
        std::int32_t discards{};
        std::int32_t picks{};
        std::int32_t score{};
        std::int32_t target{};
        if (!QueryValue(Query::plays_left, plays) || !QueryValue(Query::discards_left, discards) ||
            !QueryValue(Query::can_finish, state.can_finish) ||
            !QueryValue(Query::can_skip_blind, state.can_skip_blind) ||
            !QueryValue(Query::pending_pack, state.pending_pack) ||
            !QueryValue(Query::pack_pick_count, picks)) {
            return false;
        }
        // Score and target decide whether playing more hands can still help, so a missing
        // value must not silently become zero.
        if (!QueryValue(Query::current_score, score) || !QueryValue(Query::target_score, target)) {
            return false;
        }
        state.phase = static_cast<std::int32_t>(phase);
        state.plays_left = plays;
        state.discards_left = discards;
        state.pack_pick_count = picks;
        state.score = score;
        state.target = target;
        return true;
    }

    [[nodiscard]] bool ReadScore(std::int32_t& score, std::int32_t& target, std::int32_t& money,
        std::int32_t& act, std::int32_t& floor) const {
        return QueryValue(Query::current_score, score) && QueryValue(Query::target_score, target) &&
            QueryValue(Query::money, money) && QueryValue(Query::act, act) && QueryValue(Query::floor, floor);
    }

    [[nodiscard]] bool Send(const Action action, const std::int32_t pack_picks) {        switch (action) {
        case Action::start_blind: return SendRequest(Request::start_blind);
        case Action::skip_blind: return SendRequest(Request::skip_blind);
        case Action::play_hand: {
            // Prefer the id form: the zero-argument variant only replays whatever selection
            // the UI staged, which is empty when the plugin drives the run.
            if (play_all_by_id_.id == 0) {
                Clock::time_point deadline{};
                std::string path{"/Script/HTGame."};
                path.append(kComponentClass);
                path.push_back('.');
                path.append(kPlayAllByIdFunction.name);
                const auto handle = Find(path, deadline);
                if (handle.id != 0 && Validate(handle, kPlayAllByIdFunction)) play_all_by_id_ = handle;
            }
            std::vector<Guid> ids;
            std::int32_t count{};
            if (play_all_by_id_.id != 0 && ReadHandCardIds(ids, count) && !ids.empty()) {
                GuidArray selection{ids.data(), static_cast<std::int32_t>(ids.size()),
                    static_cast<std::int32_t>(ids.size())};
                return Invoke(play_all_by_id_, &selection, sizeof(selection));
            }
            return SendRequest(Request::play_hand);
        }
        case Action::finish_blind: return SendRequest(Request::finish_blind);
        case Action::skip_shop: return SendRequest(Request::skip_shop);
        case Action::manual_settlement: return SendRequest(Request::manual_settlement);
        case Action::confirm_pack: {
            std::array<std::int32_t, kMaximumPackPicks> indices{};
            std::int32_t count = pack_picks;
            if (count < 1) count = 1;
            if (count > static_cast<std::int32_t>(kMaximumPackPicks)) count = static_cast<std::int32_t>(kMaximumPackPicks);
            for (std::int32_t index = 0; index < count; ++index) indices[static_cast<std::size_t>(index)] = index;
            TArrayInt32 selection{indices.data(), count, count};
            return Invoke(requests_[static_cast<std::size_t>(Request::confirm_pack)], &selection, sizeof(selection));
        }
        default: return false;
        }
    }

    // Score assist: the cash-out request reads the local blind score, so raising it to the
    // target lets the native request finish the blind immediately instead of waiting for
    // another hand. Returns false when the field no longer matches its reflected name/size.
    [[nodiscard]] bool WriteBlindScore(const std::int32_t value) {
        if (component_.id == 0) return false;
        const auto component = domain_.Pointer(reader_, component_);
        if (component == 0) return false;
        if (blind_score_offset_ < 0) {
            blind_score_offset_ = reader_.PropertyOffset(component, kBlindScoreField, sizeof(std::int32_t));
        }
        if (blind_score_offset_ < 0) return false;
        return reader_.Write(component + static_cast<std::uintptr_t>(blind_score_offset_), &value, sizeof(value));
    }

    // Reads the report ids the minigame keeps for this run: the same ids the component
    // submits when the run is won.
    [[nodiscard]] bool ReadQuestIds(std::vector<FName>& out) {
        out.clear();
        if (component_.id == 0) return false;
        const auto component = domain_.Pointer(reader_, component_);
        if (component == 0) return false;
        if (quest_id_offset_ < 0) {
            quest_id_offset_ = reader_.PropertyOffset(component, kQuestIdField,
                static_cast<std::int32_t>(sizeof(QuestIdArray)));
        }
        if (quest_id_offset_ < 0) return false;
        QuestIdArray ids{};
        if (!reader_.Read(component + static_cast<std::uintptr_t>(quest_id_offset_), ids)) return false;
        if (ids.count <= 0 || ids.data == nullptr ||
            ids.count > static_cast<std::int32_t>(kMaximumReportQuests)) {
            return false;
        }
        for (std::int32_t index = 0; index < ids.count; ++index) {
            FName id{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(ids.data) +
                    sizeof(FName) * static_cast<std::uintptr_t>(index), id)) {
                return false;
            }
            if (id.id != 0) out.push_back(id);
        }
        return !out.empty();
    }

    // The win report itself: AHTPlayerState::ServerSubmitQuest for one report id.
    [[nodiscard]] bool SubmitQuest(const FName quest) {
        if (player_state_.id == 0 || quest.id == 0) return false;
        if (submit_quest_.id == 0) {
            const auto now = Clock::now();
            if (now < submit_quest_retry_) return false;
            submit_quest_retry_ = now + std::chrono::seconds(5);
            std::string path{"/Script/HTGame."};
            path.append(kPlayerStateClass);
            path.push_back('.');
            path.append(kSubmitQuestFunction.name);
            Clock::time_point deadline{};
            const auto handle = Find(path, deadline);
            if (handle.id == 0 || !Validate(handle, kSubmitQuestFunction)) return false;
            submit_quest_ = handle;
        }
        SubmitQuestFrame frame{};
        frame.quest = quest;
        return InvokeOn(player_state_, submit_quest_, &frame, sizeof(frame));
    }

    [[nodiscard]] std::string QuestName(const FName quest) const { return reader_.Name(quest); }

    // Diagnostic view of the report-id array, so an empty list can be told apart from an
    // unresolved field without another round trip.
    [[nodiscard]] std::string DescribeQuestIds() {
        std::string text{"reportIds"};
        if (component_.id == 0) return text + "=no-component";
        const auto component = domain_.Pointer(reader_, component_);
        if (component == 0) return text + "=no-pointer";
        if (quest_id_offset_ < 0) {
            quest_id_offset_ = reader_.PropertyOffset(component, kQuestIdField,
                static_cast<std::int32_t>(sizeof(QuestIdArray)));
        }
        text += " offset=";
        text += std::to_string(quest_id_offset_);
        if (quest_id_offset_ < 0) return text;
        QuestIdArray ids{};
        if (!reader_.Read(component + static_cast<std::uintptr_t>(quest_id_offset_), ids)) {
            return text + " unreadable";
        }
        text += " count=";
        text += std::to_string(ids.count);
        text += " capacity=";
        text += std::to_string(ids.capacity);
        return text;
    }

    // Reads this hand's card instance ids. GetHandCards returns a TArray by value, so its
    // 16-byte return slot carries the array header inline; each element is an
    // FHTBalatroCardInstance whose first member is the instance id.
    [[nodiscard]] bool ReadHandCardIds(std::vector<Guid>& ids, std::int32_t& count) {
        ids.clear();
        count = -1;
        if (component_.id == 0) return false;
        if (get_hand_cards_.id == 0) {
            Clock::time_point deadline{};
            std::string path{"/Script/HTGame."};
            path.append(kComponentClass);
            path.push_back('.');
            path.append(kGetHandCardsFunction.name);
            const auto handle = Find(path, deadline);
            if (handle.id == 0 || !Validate(handle, kGetHandCardsFunction)) return false;
            get_hand_cards_ = handle;
        }
        TArrayHeader header{};
        if (!Invoke(get_hand_cards_, &header, sizeof(header))) return false;
        count = header.count;
        if (header.count <= 0 || header.count > 32 || header.data == nullptr) return false;
        for (std::int32_t index = 0; index < header.count; ++index) {
            Guid id{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(header.data) +
                    static_cast<std::uintptr_t>(index) * kCardInstanceStride, id)) {
                return false;
            }
            ids.push_back(id);
        }
        return true;
    }

    // The in-progress quest rows whose id names the minigame (measured stride 0xE0, id at
    // row offset 0). Submitting these with ServerSubmitQuest completes the activity without
    // touching the board.
    [[nodiscard]] bool ReadActivityQuestIds(std::vector<FName>& out, std::vector<std::string>& names) {
        out.clear();
        names.clear();
        if (player_state_.id == 0) return false;
        const auto state = domain_.Pointer(reader_, player_state_);
        if (state == 0) return false;
        const auto component = reader_.PointerField(state, kQuestComponentField, quest_component_offset_);
        if (quest_component_offset_ < 0) {
            quest_component_offset_ = reader_.PropertyOffset(state, kQuestComponentField,
                static_cast<std::int32_t>(sizeof(std::uintptr_t)));
        }
        if (component == 0) return false;
        if (progress_offset_ < 0) {
            progress_offset_ = reader_.PropertyOffset(component, kQuestsInProgressField,
                static_cast<std::int32_t>(sizeof(TArrayHeader)));
        }
        if (progress_offset_ < 0) return false;
        TArrayHeader array{};
        if (!reader_.Read(component + static_cast<std::uintptr_t>(progress_offset_), array)) return false;
        if (array.count <= 0 || array.count > 256 || array.data == nullptr) return false;
        for (std::int32_t index = 0; index < array.count; ++index) {
            FName id{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(array.data) +
                    kProgressStride * static_cast<std::uintptr_t>(index), id)) {
                continue;
            }
            auto name = reader_.Name(id);
            if (name.empty() || name.size() > 96) continue;
            std::string lower{name};
            for (auto& character : lower) {
                if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
            }
            if (lower.find("alatro") == std::string::npos && lower.find("ycoon") == std::string::npos) continue;
            out.push_back(id);
            names.push_back(std::move(name));
        }
        return !out.empty();
    }

    // NPC challenge entry: read the lobby's flow list, then select and start one flow. Both
    // requests are the lobby buttons' own entry points.
    [[nodiscard]] bool ReadNpcFlowIds(std::vector<FName>& ids, std::vector<std::string>& names) {
        ids.clear();
        names.clear();
        if (component_.id == 0) return false;
        if (get_all_npc_ids_.id == 0) {
            Clock::time_point deadline{};
            std::string path{"/Script/HTGame."};
            path.append(kComponentClass);
            path.push_back('.');
            path.append(kGetAllNpcIdsFunction.name);
            const auto handle = Find(path, deadline);
            if (handle.id == 0 || !Validate(handle, kGetAllNpcIdsFunction)) return false;
            get_all_npc_ids_ = handle;
        }
        TArrayHeader header{};
        if (!Invoke(get_all_npc_ids_, &header, sizeof(header))) return false;
        if (header.count <= 0 || header.count > 64 || header.data == nullptr) return false;
        for (std::int32_t index = 0; index < header.count; ++index) {
            FName id{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(header.data) +
                    sizeof(FName) * static_cast<std::uintptr_t>(index), id)) {
                continue;
            }
            auto name = reader_.Name(id);
            if (name.empty() || name.size() > 96) continue;
            ids.push_back(id);
            names.push_back(std::move(name));
        }
        return !ids.empty();
    }

    [[nodiscard]] bool SelectNpcQuest(const FName flow, bool& selected) {
        if (select_npc_quest_.id == 0) {
            Clock::time_point deadline{};
            std::string path{"/Script/HTGame."};
            path.append(kComponentClass);
            path.push_back('.');
            path.append(kSelectNpcQuestFunction.name);
            const auto handle = Find(path, deadline);
            if (handle.id == 0 || !Validate(handle, kSelectNpcQuestFunction)) return false;
            select_npc_quest_ = handle;
        }
        NpcSelectFrame frame{};
        frame.flow = flow;
        if (!Invoke(select_npc_quest_, &frame, sizeof(frame))) return false;
        selected = frame.selected != 0;
        return true;
    }

    [[nodiscard]] bool StartNpcChallenge(const FName flow) {
        if (start_npc_challenge_.id == 0) {
            Clock::time_point deadline{};
            std::string path{"/Script/HTGame."};
            path.append(kComponentClass);
            path.push_back('.');
            path.append(kStartNpcChallengeFunction.name);
            const auto handle = Find(path, deadline);
            if (handle.id == 0 || !Validate(handle, kStartNpcChallengeFunction)) return false;
            start_npc_challenge_ = handle;
        }
        FName frame{};
        frame = flow;
        return Invoke(start_npc_challenge_, &frame, sizeof(frame));
    }

    // Direct-completion discovery: the player's quest component lists what is still in
    // progress, which is where an activity objective id can be read from without the
    // component's transient report list.
    [[nodiscard]] std::string DescribeQuests() {
        std::string text{"quests"};
        if (player_state_.id == 0) return text + "=no-state";
        const auto state = domain_.Pointer(reader_, player_state_);
        if (state == 0) return text + "=no-pointer";
        const auto component = reader_.PointerField(state, kQuestComponentField, quest_component_offset_);
        if (quest_component_offset_ < 0) {
            quest_component_offset_ = reader_.PropertyOffset(state, kQuestComponentField,
                static_cast<std::int32_t>(sizeof(std::uintptr_t)));
        }
        text += " comp=";
        text += component != 0 ? '1' : '0';
        if (component == 0) return text;
        if (progress_offset_ < 0) {
            progress_offset_ = reader_.PropertyOffset(component, kQuestsInProgressField,
                static_cast<std::int32_t>(sizeof(TArrayHeader)));
        }
        text += " offset=";
        text += std::to_string(progress_offset_);
        if (progress_offset_ < 0) return text;
        TArrayHeader array{};
        if (!reader_.Read(component + static_cast<std::uintptr_t>(progress_offset_), array)) {
            return text + " unreadable";
        }
        text += " count=";
        text += std::to_string(array.count);
        if (array.count <= 0 || array.count > 256 || array.data == nullptr) return text;
        // One element's first bytes: the quest id is expected to lead the row, and this is
        // what the caller needs to size the stride.
        std::array<std::uint8_t, 32> bytes{};
        if (!reader_.Read(reinterpret_cast<std::uintptr_t>(array.data), bytes)) return text + " elem-unreadable";
        text += " first=";
        char byte[4]{};
        for (const auto value : bytes) {
            std::snprintf(byte, sizeof(byte), "%02X", value);
            text += byte;
        }
        // Element layout probe: an FName anywhere in the first 0x80 bytes of row 0 is
        // reported by its offset, which tells where the quest id lives; the stride follows
        // from resolving the same offset in row 1.
        char field[24]{};
        for (std::uintptr_t offset = 0; offset < 0x80; offset += 8) {
            FName id{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(array.data) + offset, id)) continue;
            const auto name = reader_.Name(id);
            if (name.empty() || name.size() > 96) continue;
            std::snprintf(field, sizeof(field), " f0x%02X=", static_cast<unsigned>(offset));
            text += field;
            text += name;
        }
        // Row 0 starts with a quest id, so the stride is the offset whose row 1 **and**
        // row 2 both resolve to a different printable id.
        text += " strides:";
        constexpr std::array<std::uintptr_t, 14> candidates{
            0x78, 0x80, 0x88, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0x100, 0x120, 0x140, 0x180, 0x200};
        for (const auto stride : candidates) {
            FName first{};
            FName second{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(array.data) + stride, first) ||
                !reader_.Read(reinterpret_cast<std::uintptr_t>(array.data) + stride * 2, second)) {
                continue;
            }
            const auto one = reader_.Name(first);
            const auto two = reader_.Name(second);
            if (one.empty() || two.empty() || one == two) continue;
            std::snprintf(field, sizeof(field), " 0x%02X=%s/%s", static_cast<unsigned>(stride),
                one.c_str(), two.c_str());
            text += field;
        }
        // Every id, so the minigame's rows can be named instead of guessed.
        text += " ids:";
        for (std::int32_t index = 0; index < array.count; ++index) {
            FName row{};
            if (!reader_.Read(reinterpret_cast<std::uintptr_t>(array.data) +
                    kProgressStride * static_cast<std::uintptr_t>(index), row)) {
                break;
            }
            const auto name = reader_.Name(row);
            if (name.empty()) continue;
            text += " ";
            text += name;
        }
        return text;
    }

    // Raw view of the hand query: the resolved UFunction's marshal fields and the bytes it
    // writes back, so a layout mismatch is visible instead of just "unreadable".
    [[nodiscard]] std::string DescribeHand() {
        std::string text{"hand"};
        Clock::time_point deadline{};
        std::string path{"/Script/HTGame."};
        path.append(kComponentClass);
        path.push_back('.');
        path.append(kGetHandCardsFunction.name);
        const auto handle = Find(path, deadline);
        text += " found=";
        text += handle.id != 0 ? '1' : '0';
        if (handle.id == 0) return text;
        const auto function = domain_.Pointer(reader_, handle);
        text += " fn=";
        text += function != 0 ? '1' : '0';
        if (function == 0) return text;
        std::uint8_t parameters{};
        std::uint16_t frame_size{};
        if (reader_.Read(function + profile::native_layout.FunctionCount, parameters)) {
            text += " n=";
            text += std::to_string(parameters);
        }
        if (reader_.Read(function + profile::native_layout.FunctionSize, frame_size)) {
            text += " s=";
            text += std::to_string(frame_size);
        }
        std::array<std::uint8_t, 16> frame{};
        if (!InvokeOn(component_, handle, frame.data(), frame.size())) return text + " invoke=fail";
        text += " bytes=";
        char byte[4]{};
        for (const auto value : frame) {
            std::snprintf(byte, sizeof(byte), "%02X", value);
            text += byte;
        }
        return text;
    }

private:
    static constexpr std::size_t Index(const Request value) noexcept { return static_cast<std::size_t>(value); }    static constexpr std::size_t Index(const Query value) noexcept { return static_cast<std::size_t>(value); }

    [[nodiscard]] bool InvokeOn(const AnomalyGenerationHandleV1 receiver, const AnomalyGenerationHandleV1 function,
        void* const frame, const std::size_t size) const {
        if (receiver.id == 0 || function.id == 0 || events_ == nullptr || events_->invoke == nullptr) return false;
        return events_->invoke(events_->user, receiver, function, frame, size).code == ANOMALY_STATUS_V1_OK;
    }

    [[nodiscard]] bool Invoke(const AnomalyGenerationHandleV1 function, void* const frame,
        const std::size_t size) const {
        return InvokeOn(component_, function, frame, size);
    }

    template <class Value>
    [[nodiscard]] bool QueryValue(const Query query, Value& out) const {
        const auto index = Index(query);
        const auto spec = kQueries[index];
        std::array<std::uint8_t, 16> frame{};
        if (queries_[index].id == 0 || sizeof(Value) != spec.frame_size ||
            !Invoke(queries_[index], frame.data(), spec.frame_size)) {
            return false;
        }
        std::memcpy(&out, frame.data(), sizeof(Value));
        return true;
    }

    [[nodiscard]] bool SendRequest(const Request request) const {
        const auto index = Index(request);
        return requests_[index].id != 0 && Invoke(requests_[index], nullptr, 0);
    }

    [[nodiscard]] AnomalyGenerationHandleV1 Find(const std::string_view path, Clock::time_point& deadline) const {
        if (objects_ == nullptr || objects_->find_exact == nullptr) return {};
        const auto now = Clock::now();
        if (now < deadline) return {};
        AnomalyGenerationHandleV1 handle{};
        if (objects_->find_exact(objects_->user, anomaly::sdk::StringView(path), &handle).code != ANOMALY_STATUS_V1_OK) {
            handle = {};
        }
        if (handle.id == 0) deadline = now + std::chrono::seconds(5);
        return handle;
    }

    [[nodiscard]] bool ResolveFunctions() {
        std::string path{"/Script/HTGame."};
        path.append(kComponentClass);
        path.push_back('.');
        const auto prefix = path.size();
        for (std::size_t index = 0; index < kRequests.size(); ++index) {
            if (requests_[index].id != 0) continue;
            path.resize(prefix);
            path.append(kRequests[index].name);
            auto handle = Find(path, request_retry_[index]);
            if (handle.id != 0 && !Validate(handle, kRequests[index])) {
                request_retry_[index] = Clock::now() + std::chrono::seconds(5);
                handle = {};
            }
            requests_[index] = handle;
            if (handle.id == 0) return false;
        }
        for (std::size_t index = 0; index < kQueries.size(); ++index) {
            if (queries_[index].id != 0) continue;
            path.resize(prefix);
            path.append(kQueries[index].name);
            auto handle = Find(path, query_retry_[index]);
            if (handle.id != 0 && !Validate(handle, kQueries[index])) {
                query_retry_[index] = Clock::now() + std::chrono::seconds(5);
                handle = {};
            }
            queries_[index] = handle;
            if (handle.id == 0) return false;
        }
        functions_ready_ = true;
        return true;
    }

    [[nodiscard]] bool Validate(const AnomalyGenerationHandleV1 handle, const FunctionSpec spec) const {
        const auto function = domain_.Pointer(reader_, handle);
        return function != 0 && reader_.MatchesFunction(function, spec.name, spec.parameters, spec.frame_size);
    }

    [[nodiscard]] bool ResolveComponent() {
        if (players_ == nullptr || players_->get_object_handles == nullptr) return false;
        AnomalyGenerationHandleV1 controller{};
        if (players_->get_object_handles(players_->user, nullptr, &controller).code != ANOMALY_STATUS_V1_OK ||
            controller.id == 0) {
            component_ = {};
            return false;
        }
        const auto controller_pointer = domain_.Pointer(reader_, controller);
        const auto player_state = reader_.PointerField(controller_pointer, kPlayerStateField, player_state_offset_);
        if (player_state == 0 || !reader_.IsA(player_state, kPlayerStateClass)) {
            component_ = {};
            return false;
        }
        if (player_state_offset_ < 0) {
            player_state_offset_ = reader_.PropertyOffset(controller_pointer, kPlayerStateField,
                static_cast<std::int32_t>(sizeof(std::uintptr_t)));
        }
        auto component = reader_.PointerField(player_state, kComponentField, component_offset_);
        if (component_offset_ < 0) {
            component_offset_ = reader_.PropertyOffset(player_state, kComponentField,
                static_cast<std::int32_t>(sizeof(std::uintptr_t)));
        }
        if (component == 0 || !reader_.IsA(component, kComponentClass)) {
            component_ = {};
            RequestComponent(player_state);
            return false;
        }
        component_ = domain_.Handle(reader_, objects_, component);
        player_state_ = domain_.Handle(reader_, objects_, player_state);
        return component_.id != 0;
    }

    // The component only replicates once the player state is asked for it. The request is
    // rate limited because the answer arrives with the next replication batch.
    void RequestComponent(const std::uintptr_t player_state) {
        if (request_component_function_.id == 0) {
            std::string path{"/Script/HTGame."};
            path.append(kPlayerStateClass);
            path.push_back('.');
            path.append(kRequestComponentFunction.name);
            Clock::time_point deadline{};
            request_component_function_ = Find(path, deadline);
        }
        const auto now = Clock::now();
        if (request_component_function_.id == 0 || now < component_requested_at_) return;
        component_requested_at_ = now + std::chrono::seconds(5);
        const auto handle = domain_.Handle(reader_, objects_, player_state);
        if (handle.id == 0 || events_ == nullptr || events_->invoke == nullptr) return;
        events_->invoke(events_->user, handle, request_component_function_, nullptr, 0);
    }

    const Reader& reader_;
    const AnomalyUe5ObjectsServiceV1* objects_;
    const AnomalyNtePlayerServiceV1* players_;
    const AnomalyUe5ProcessEventServiceV1* events_;
    ObjectDomain& domain_;
    AnomalyGenerationHandleV1 component_{};
    AnomalyGenerationHandleV1 player_state_{};
    AnomalyGenerationHandleV1 submit_quest_{};
    AnomalyGenerationHandleV1 get_hand_cards_{};
    AnomalyGenerationHandleV1 play_all_by_id_{};
    AnomalyGenerationHandleV1 get_all_npc_ids_{};
    AnomalyGenerationHandleV1 select_npc_quest_{};
    AnomalyGenerationHandleV1 start_npc_challenge_{};
    Clock::time_point submit_quest_retry_{};
    AnomalyGenerationHandleV1 request_component_function_{};
    std::array<AnomalyGenerationHandleV1, static_cast<std::size_t>(Request::count)> requests_{};
    std::array<AnomalyGenerationHandleV1, static_cast<std::size_t>(Query::count)> queries_{};
    std::array<Clock::time_point, static_cast<std::size_t>(Request::count)> request_retry_{};
    std::array<Clock::time_point, static_cast<std::size_t>(Query::count)> query_retry_{};
    int player_state_offset_{-1};
    int component_offset_{-1};
    int blind_score_offset_{-1};
    int quest_id_offset_{-1};
    int quest_component_offset_{-1};
    int progress_offset_{-1};
    Clock::time_point component_requested_at_{};
    bool functions_ready_{};
};

}  // namespace tycoon

