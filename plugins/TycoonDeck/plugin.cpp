#include <anomaly/sdk/cpp.hpp>
#include <anomaly/sdk/localization.hpp>

#include "deck_runtime.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace anomaly::sdk;
using tycoon::Action;
using tycoon::Clock;
using tycoon::DeckBridge;
using tycoon::DeckState;
using tycoon::ObjectDomain;
using tycoon::Reader;
using tycoon::Rules;

constexpr std::string_view kId = "yg.tycoons-deck";
constexpr std::string_view kTitle = "Tycoon's Deck Autopilot";
constexpr std::string_view kAuthor = "YG";
constexpr std::string_view kVersion = "0.1.0";
constexpr std::string_view kConfigId = "settings";
constexpr std::uint32_t kConfigVersion = 1;
constexpr std::string_view kConfigSchema =
    R"({"type":"object","required":["enabled","skip_blinds","skip_shop","confirm_packs","score_assist","interval_ms"],)"
    R"("additionalProperties":false,"properties":{)"
    R"("enabled":{"type":"boolean"},)"
    R"("skip_blinds":{"type":"boolean"},)"
    R"("skip_shop":{"type":"boolean"},)"
    R"("confirm_packs":{"type":"boolean"},)"
    R"("score_assist":{"type":"boolean"},)"
    R"("interval_ms":{"type":"integer","minimum":150,"maximum":2000}}})";
constexpr std::size_t kMaximumConfigBytes = 4096;
constexpr int kMinimumIntervalMs = 150;
constexpr int kMaximumIntervalMs = 2000;
constexpr int kDefaultIntervalMs = 400;
// A request that never changes the observed state is not retried forever: the run either
// advanced or this build's contract no longer matches what the plugin expects.
constexpr int kStallLimit = 12;

struct Settings final {
    bool enabled{};
    bool skip_blinds{true};
    bool skip_shop{true};
    bool confirm_packs{true};
    int interval_ms{kDefaultIntervalMs};
};

AnomalyByteSpanV1 Bytes(const std::string_view text) noexcept {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

// The host hands the saved document back as compact JSON, and this plugin writes exactly
// the five keys below, so a quoted-key scan is enough and keeps the package dependency
// free. A key that cannot be parsed keeps its previous value.
[[nodiscard]] std::string_view JsonValue(const std::string_view document, const std::string_view key) noexcept {
    std::string quoted{};
    quoted.reserve(key.size() + 2);
    quoted.push_back('"');
    quoted.append(key);
    quoted.push_back('"');
    const auto position = document.find(quoted);
    if (position == std::string_view::npos) return {};
    const auto colon = document.find(':', position + quoted.size());
    if (colon == std::string_view::npos) return {};
    auto index = colon + 1;
    while (index < document.size() && (document[index] == ' ' || document[index] == '\t')) ++index;
    auto end = index;
    while (end < document.size() && document[end] != ',' && document[end] != '}') ++end;
    return document.substr(index, end - index);
}

[[nodiscard]] bool JsonBool(const std::string_view document, const std::string_view key, bool& value) noexcept {
    const auto field = JsonValue(document, key);
    if (field == "true") {
        value = true;
        return true;
    }
    if (field == "false") {
        value = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool JsonInt(const std::string_view document, const std::string_view key, int& value) noexcept {
    const auto field = JsonValue(document, key);
    if (field.empty() || field.size() > 9) return false;
    int parsed{};
    for (const auto character : field) {
        if (character < '0' || character > '9') return false;
        parsed = parsed * 10 + (character - '0');
    }
    value = parsed;
    return true;
}

class Context final {
public:
    explicit Context(const AnomalyHostApiV1* api) noexcept : api_(api), localizer_(api) {
        const Host query(api);
        core_ = query.Query<AnomalyCoreServiceV1>(ANOMALY_CORE_SERVICE_V1_ID).get();
        names_ = query.Query<AnomalyUe5NamesServiceV1>(ANOMALY_UE5_NAMES_SERVICE_V1_ID, 2).get();
        objects_ = query.Query<AnomalyUe5ObjectsServiceV1>(ANOMALY_UE5_OBJECTS_SERVICE_V1_ID).get();
        events_ = query.Query<AnomalyUe5ProcessEventServiceV1>(ANOMALY_UE5_PROCESS_EVENT_SERVICE_V1_ID).get();
        framework_ = query.Query<AnomalyUe5FrameworkServiceV1>(ANOMALY_UE5_FRAMEWORK_SERVICE_V1_ID).get();
        players_ = query.Query<AnomalyNtePlayerServiceV1>(ANOMALY_NTE_PLAYER_SERVICE_V1_ID).get();
        windows_ = query.Query<AnomalyWindowServiceV1>(ANOMALY_WINDOW_SERVICE_V1_ID).get();
        config_ = query.Query<AnomalyConfigServiceV1>(ANOMALY_CONFIG_SERVICE_V1_ID).get();
        scheduler_ = query.Query<AnomalySchedulerServiceV1>(ANOMALY_SCHEDULER_SERVICE_V1_ID).get();
        if (names_ != nullptr && core_ != nullptr) reader_.emplace(names_, core_);
        if (reader_) bridge_ = std::make_unique<DeckBridge>(*reader_, objects_, players_, events_, domain_);
    }

    [[nodiscard]] bool Ready() const noexcept { return bridge_ != nullptr && windows_ != nullptr && config_ != nullptr; }
    [[nodiscard]] const Localizer& Text() const noexcept { return localizer_; }
    [[nodiscard]] const AnomalyWindowServiceV1* Windows() const noexcept { return windows_; }
    [[nodiscard]] AnomalyGenerationHandleV1 Window() const noexcept { return window_; }
    void SetWindow(const AnomalyGenerationHandleV1 window) noexcept { window_ = window; }
    [[nodiscard]] bool Shown() const noexcept { return shown_.load(); }
    void Shown(const bool value) noexcept { shown_.store(value); }

    [[nodiscard]] bool Enabled() const noexcept { return enabled_.load(); }
    void SetEnabled(const bool value) noexcept {
        enabled_.store(value);
        if (!value) {
            reporting_.store(false);
            bridge_.reset();
        }
        if (value && reader_) bridge_ = std::make_unique<DeckBridge>(*reader_, objects_, players_, events_, domain_);
        start_sent_ = 0;
        stall_count_ = 0;
        signature_ = 0;
        assist_signature_ = 0;
        next_tick_ = {};
    }

    [[nodiscard]] bool Reporting() const noexcept { return reporting_.load(); }

    // Primary path: report the run's completion through the player state's own submit RPC.
    void StartReport() noexcept {
        reporting_.store(true);
        report_index_ = 0;
        report_sent_ = 0;
        report_failures_ = 0;
        SetEnabled(true);
    }

    // Fallback path: play the run out through the component's requests.
    void StartAutoplay() noexcept {
        reporting_.store(false);
        SetEnabled(true);
    }

    [[nodiscard]] Rules CurrentRules() const noexcept {        Rules rules;
        rules.skip_blinds = skip_blinds_.load();
        rules.skip_shop = skip_shop_.load();
        rules.confirm_packs = confirm_packs_.load();
        rules.score_assist = score_assist_.load();
        return rules;
    }

    [[nodiscard]] bool ScoreAssist() const noexcept { return score_assist_.load(); }
    void SetScoreAssist(const bool value) noexcept { score_assist_.store(value); }

    [[nodiscard]] int IntervalMilliseconds() const noexcept { return interval_ms_.load(); }
    void SetIntervalMilliseconds(const int value) noexcept {
        int clamped = value;
        if (clamped < kMinimumIntervalMs) clamped = kMinimumIntervalMs;
        if (clamped > kMaximumIntervalMs) clamped = kMaximumIntervalMs;
        interval_ms_.store(clamped);
    }

    [[nodiscard]] bool SkipBlinds() const noexcept { return skip_blinds_.load(); }
    void SetSkipBlinds(const bool value) noexcept { skip_blinds_.store(value); }
    [[nodiscard]] bool SkipShop() const noexcept { return skip_shop_.load(); }
    void SetSkipShop(const bool value) noexcept { skip_shop_.store(value); }
    [[nodiscard]] bool ConfirmPacks() const noexcept { return confirm_packs_.load(); }
    void SetConfirmPacks(const bool value) noexcept { confirm_packs_.store(value); }

    void Publish(std::string value) {
        status_.store(std::make_shared<const std::string>(std::move(value)));
    }
    [[nodiscard]] std::shared_ptr<const std::string> Status() const { return status_.load(); }
    [[nodiscard]] int Sent() const noexcept { return sent_.load(); }

    void ReadSettings() noexcept {
        try {
            if (config_ == nullptr || config_->read == nullptr) return;
            std::string document(kMaximumConfigBytes, '\0');
            std::size_t size = document.size();
            std::uint32_t version{};
            const auto status = config_->read(config_->user, StringView(kConfigId), &version,
                {reinterpret_cast<std::uint8_t*>(document.data()), document.size()}, &size);
            if (!Succeeded(status) || version != kConfigVersion || size == 0 || size > document.size()) return;
            document.resize(size);
            const std::string_view view{document};
            bool flag{};
            int number{};
            if (JsonBool(view, "enabled", flag)) enabled_.store(flag);
            if (JsonBool(view, "skip_blinds", flag)) skip_blinds_.store(flag);
            if (JsonBool(view, "skip_shop", flag)) skip_shop_.store(flag);
            if (JsonBool(view, "confirm_packs", flag)) confirm_packs_.store(flag);
            if (JsonBool(view, "score_assist", flag)) score_assist_.store(flag);
            if (JsonInt(view, "interval_ms", number)) SetIntervalMilliseconds(number);
        } catch (...) {
        }
    }

    void RequestSave() noexcept {
        pending_save_.store(true);
        if (scheduler_ == nullptr || scheduler_->schedule == nullptr) return;
        AnomalyGenerationHandleV1 task{};
        scheduler_->schedule(scheduler_->user, 0, &Context::SaveTask, this, &task);
    }

    void SaveSettings() noexcept {
        try {
            if (!pending_save_.exchange(false)) return;
            if (config_ == nullptr || config_->write_atomic == nullptr) return;
            char document[kMaximumConfigBytes]{};
            const auto written = std::snprintf(document, sizeof(document),
                "{\"enabled\":%s,\"skip_blinds\":%s,\"skip_shop\":%s,\"confirm_packs\":%s,"
                "\"score_assist\":%s,\"interval_ms\":%d}",
                enabled_.load() ? "true" : "false", skip_blinds_.load() ? "true" : "false",
                skip_shop_.load() ? "true" : "false", confirm_packs_.load() ? "true" : "false",
                score_assist_.load() ? "true" : "false", interval_ms_.load());
            if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(document)) return;
            config_->write_atomic(config_->user, StringView(kConfigId), kConfigVersion,
                Bytes(std::string_view(document, static_cast<std::size_t>(written))));
        } catch (...) {
        }
    }

    // One control step: observe the session, then send at most one native request.
    void Tick() noexcept {
        try {
            const auto now = Clock::now();
            if (now < next_tick_) return;
            next_tick_ = now + std::chrono::milliseconds(interval_ms_.load());
            if (framework_ == nullptr || framework_->is_game_thread == nullptr ||
                framework_->is_game_thread(framework_->user) == 0) {
                return;
            }
            if (!enabled_.load()) return;
            if (!reader_ || !bridge_) {
                Publish(localizer_.Text("status.waiting_interfaces", "Waiting for the game interfaces."));
                return;
            }
            if (!domain_.Initialize(*reader_, objects_)) {
                Publish(localizer_.Text("status.waiting_interfaces", "Waiting for the game interfaces."));
                return;
            }
            if (!bridge_->Refresh()) {
                Publish(localizer_.Text("status.waiting_component", "Waiting for the Tycoon's Deck session."));
                return;
            }
            if (reporting_.load()) {
                Report();
                return;
            }
            DeckState state;
            if (!bridge_->Read(state)) {
                Publish(localizer_.Text("status.waiting_state", "The session state is unavailable right now."));
                return;
            }
            std::int32_t score{};
            std::int32_t target{};
            std::int32_t money{};
            std::int32_t act{};
            std::int32_t floor{};
            const bool detailed = bridge_->ReadScore(score, target, money, act, floor);
            ShowState(state, score, target, money, act, floor, detailed);
            if (state.phase == static_cast<std::int32_t>(tycoon::Phase::game_over)) {
                Finish(localizer_.Text("status.run_finished", "The run is finished."));
                return;
            }
            if (state.phase == static_cast<std::int32_t>(tycoon::Phase::game_over_failed)) {
                Finish(localizer_.Text("status.run_failed", "The run failed."));
                return;
            }
            // Score assist runs once per observed blind state: it raises the local score the
            // cash-out request reads, then the next tick cashes out.
            if (CurrentRules().score_assist && detailed &&
                state.phase == static_cast<std::int32_t>(tycoon::Phase::playing) && score < target) {
                const auto signature = Signature(state);
                if (signature != assist_signature_ && bridge_->WriteBlindScore(target)) {
                    assist_signature_ = signature;
                    Publish(localizer_.Text("status.assist", "Blind score raised to the target."));
                    return;
                }
            }
            const auto action = tycoon::NextAction(state, CurrentRules());
            if (action == Action::none) return;
            const auto signature = Signature(state);
            if (signature == signature_) {
                if (++stall_count_ >= kStallLimit) {
                    const std::string text = std::to_string(stall_count_);
                    const std::array arguments{std::string_view(text)};
                    Finish(localizer_.Format("status.stalled",
                        "No progress after {0} requests, so the autopilot stopped.", arguments));
                    return;
                }
            } else {
                signature_ = signature;
                stall_count_ = 0;
            }
            if (bridge_->Send(action, state.pack_pick_count)) sent_.store(sent_.load() + 1);
        } catch (...) {
            Publish(localizer_.Text("status.waiting_state", "The session state is unavailable right now."));
        }
    }

private:
    // Submits one report id per control step so the server sees the same cadence the
    // minigame itself uses, then stops as soon as the list is exhausted.
    void Report() noexcept {
        try {
            std::vector<tycoon::FName> ids;
            if (!bridge_->ReadQuestIds(ids)) {
                Publish(localizer_.Text("status.report_waiting_ids", "Waiting for this run's report ids."));
                return;
            }
            if (report_index_ >= static_cast<int>(ids.size())) {
                const std::string count = std::to_string(report_sent_);
                const std::array arguments{std::string_view(count)};
                Finish(localizer_.Format("status.report_done", "Reported {0} objectives.", arguments));
                return;
            }
            const auto quest = ids[static_cast<std::size_t>(report_index_)];
            if (bridge_->SubmitQuest(quest)) {
                ++report_sent_;
                ++report_index_;
                report_failures_ = 0;
                const std::string name = bridge_->QuestName(quest);
                const std::array arguments{std::string_view(name)};
                Publish(localizer_.Format("status.report_sent", "Reported {0}.", arguments));
            } else if (++report_failures_ >= 8) {
                const std::string name = bridge_->QuestName(quest);
                const std::array arguments{std::string_view(name)};
                Finish(localizer_.Format("status.report_failed", "The server rejected {0}.", arguments));
            }
        } catch (...) {
        }
    }

    static void ANOMALY_CALL SaveTask(void* user, AnomalyGenerationHandleV1) noexcept {
        static_cast<Context*>(user)->SaveSettings();
    }

    [[nodiscard]] static std::uint64_t Signature(const DeckState& state) noexcept {
        std::uint64_t value = static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.phase));
        value = value * 1009U + static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.plays_left));
        value = value * 1009U + static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.discards_left));
        value = value * 1009U + (state.can_finish ? 1U : 0U);
        value = value * 1009U + (state.can_skip_blind ? 1U : 0U);
        value = value * 1009U + (state.pending_pack ? 1U : 0U);
        return value;
    }

    void Finish(const std::string& message) noexcept {
        Publish(message);
        enabled_.store(false);
        reporting_.store(false);
        pending_save_.store(true);
        RequestSave();
        bridge_.reset();
    }

    void ShowState(const DeckState& state, const std::int32_t score, const std::int32_t target,
        const std::int32_t money, const std::int32_t act, const std::int32_t floor, const bool detailed) {
        static constexpr std::array<std::string_view, 9> keys{{
            "phase.idle", "phase.blind_selection", "phase.playing", "phase.blind_cleared", "phase.shop",
            "phase.shop_cleared", "phase.game_over", "phase.game_over_failed", "phase.settling"}};
        static constexpr std::array<std::string_view, 9> english{{
            "Idle", "Blind selection", "Playing", "Blind cleared", "Shop",
            "Shop cleared", "Run finished", "Run failed", "Settling"}};
        const auto index = state.phase >= 0 && state.phase < static_cast<std::int32_t>(keys.size())
            ? static_cast<std::size_t>(state.phase)
            : keys.size() - 1;
        const std::string phase = localizer_.Text(keys[index], english[index]);
        if (!detailed) {
            const std::array arguments{std::string_view(phase)};
            Publish(localizer_.Format("text.phase", "Phase: {0}", arguments));
            return;
        }
        const auto score_text = std::to_string(score);
        const auto target_text = std::to_string(target);
        const std::array score_arguments{std::string_view(score_text), std::string_view(target_text)};
        const std::string line = localizer_.Format("text.score", "Score {0} / {1}", score_arguments);
        const std::array combined{std::string_view(phase), std::string_view(line)};
        state_score_.store(score);
        state_target_.store(target);
        state_money_.store(money);
        state_act_.store(act);
        state_floor_.store(floor);
        state_plays_.store(state.plays_left);
        state_discards_.store(state.discards_left);
        Publish(localizer_.Format("text.headline", "{0} 路 {1}", combined));
    }

public:
    [[nodiscard]] std::int32_t Score() const noexcept { return state_score_.load(); }
    [[nodiscard]] std::int32_t Target() const noexcept { return state_target_.load(); }
    [[nodiscard]] std::int32_t Money() const noexcept { return state_money_.load(); }
    [[nodiscard]] std::int32_t Act() const noexcept { return state_act_.load(); }
    [[nodiscard]] std::int32_t Floor() const noexcept { return state_floor_.load(); }
    [[nodiscard]] std::int32_t Plays() const noexcept { return state_plays_.load(); }
    [[nodiscard]] std::int32_t Discards() const noexcept { return state_discards_.load(); }
    [[nodiscard]] bool Configured() const noexcept { return !pending_save_.load(); }

    // Replaces the reader/bridge after the tick gate stops the previous instance.
    void Rebind() noexcept {
        bridge_.reset();
        if (reader_) bridge_ = std::make_unique<DeckBridge>(*reader_, objects_, players_, events_, domain_);
    }

private:
    const AnomalyHostApiV1* api_{};
    Localizer localizer_;
    const AnomalyCoreServiceV1* core_{};
    const AnomalyUe5NamesServiceV1* names_{};
    const AnomalyUe5ObjectsServiceV1* objects_{};
    const AnomalyUe5ProcessEventServiceV1* events_{};
    const AnomalyUe5FrameworkServiceV1* framework_{};
    const AnomalyNtePlayerServiceV1* players_{};
    const AnomalyWindowServiceV1* windows_{};
    const AnomalyConfigServiceV1* config_{};
    const AnomalySchedulerServiceV1* scheduler_{};
    std::optional<Reader> reader_;
    ObjectDomain domain_;
    std::unique_ptr<DeckBridge> bridge_;
    AnomalyGenerationHandleV1 window_{};
    std::atomic<bool> enabled_{};
    std::atomic<bool> skip_blinds_{true};
    std::atomic<bool> skip_shop_{true};
    std::atomic<bool> confirm_packs_{true};
    std::atomic<bool> score_assist_{true};
    std::atomic<int> interval_ms_{kDefaultIntervalMs};
    std::atomic<bool> pending_save_{};
    std::atomic<bool> shown_{};
    std::atomic<bool> reporting_{};
    int report_index_{};
    int report_sent_{};
    int report_failures_{};
    std::atomic<int> sent_{};
    std::atomic<std::int32_t> state_score_{};
    std::atomic<std::int32_t> state_target_{};
    std::atomic<std::int32_t> state_money_{};
    std::atomic<std::int32_t> state_act_{};
    std::atomic<std::int32_t> state_floor_{};
    std::atomic<std::int32_t> state_plays_{};
    std::atomic<std::int32_t> state_discards_{};
    std::atomic<std::shared_ptr<const std::string>> status_;
    Clock::time_point next_tick_{};
    std::uint64_t signature_{};
    std::uint64_t assist_signature_{};
    int stall_count_{};
    int start_sent_{};
};

void Content(Context& context, const AnomalyUiServiceV1* ui) {
    const auto& text = context.Text();
    // The primary action is the win report: it submits the run's own quest ids through
    // AHTPlayerState::ServerSubmitQuest instead of playing the hand out.
    if (context.Enabled()) {
        if (ui->button(ui->user, StringView(text.Text("action.stop", "Stop")), 0.0F, 0.0F) != 0) {
            context.SetEnabled(false);
            context.RequestSave();
        }
    } else if (ui->button(ui->user,
                   StringView(text.Text("action.report_win", "Submit the win report")), 0.0F, 0.0F) != 0) {
        context.StartReport();
        context.RequestSave();
    }
    if (!context.Enabled() && ui->button(ui->user,
            StringView(text.Text("action.autoplay", "Play the run through instead")), 0.0F, 0.0F) != 0) {
        context.StartAutoplay();
        context.RequestSave();
    }
    int score_assist = context.ScoreAssist() ? 1 : 0;
    if (ui->checkbox(ui->user, StringView(text.Text("option.score_assist", "Raise the blind score to its target")),
            &score_assist) != 0) {
        context.SetScoreAssist(score_assist != 0);
        context.RequestSave();
    }
    int skip_shop = context.SkipShop() ? 1 : 0;
    if (ui->checkbox(ui->user, StringView(text.Text("option.skip_shop", "Leave the shop immediately")),
            &skip_shop) != 0) {
        context.SetSkipShop(skip_shop != 0);
        context.RequestSave();
    }
    if (ui->slider_float != nullptr) {
        float interval = static_cast<float>(context.IntervalMilliseconds());
        if (ui->slider_float(ui->user, StringView(text.Text("option.interval", "Action interval (ms)")), &interval,
                static_cast<float>(kMinimumIntervalMs), static_cast<float>(kMaximumIntervalMs)) != 0) {
            context.SetIntervalMilliseconds(static_cast<int>(interval));
            context.RequestSave();
        }
    }
    if (ui->separator != nullptr) ui->separator(ui->user);
    if (const auto status = context.Status()) ui->text(ui->user, StringView(*status));
    const auto score = std::to_string(context.Score());
    const auto target = std::to_string(context.Target());
    const std::array score_arguments{std::string_view(score), std::string_view(target)};
    const auto score_text = text.Format("text.score", "Score {0} / {1}", score_arguments);
    ui->text(ui->user, StringView(score_text));
    const auto plays = std::to_string(context.Plays());
    const auto discards = std::to_string(context.Discards());
    const std::array plays_arguments{std::string_view(plays), std::string_view(discards)};
    const auto plays_text = text.Format("text.plays", "Plays left {0} 路 Discards left {1}", plays_arguments);
    ui->text(ui->user, StringView(plays_text));
    const auto money = std::to_string(context.Money());
    const std::array money_arguments{std::string_view(money)};
    ui->text(ui->user, StringView(text.Format("text.money", "Money {0}", money_arguments)));
    const auto act = std::to_string(context.Act());
    const auto floor = std::to_string(context.Floor());
    const std::array act_arguments{std::string_view(act), std::string_view(floor)};
    ui->text(ui->user, StringView(text.Format("text.act", "Act {0} 路 Floor {1}", act_arguments)));
    const auto sent = std::to_string(context.Sent());
    const std::array sent_arguments{std::string_view(sent)};
    ui->text(ui->user, StringView(text.Format("text.sent", "Requests sent: {0}", sent_arguments)));
    if (ui->separator != nullptr) ui->separator(ui->user);
    ui->text(ui->user, StringView(text.Text("hint.enter_table",
        "Enter a Tycoon's Deck run, then enable this.")));
}

AnomalyStatusV1 ANOMALY_CALL Load(const AnomalyHostApiV1* host, void** out) noexcept {
    if (out == nullptr) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    *out = nullptr;
    if (host == nullptr) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    if (!tycoon::profile::LoadCompatibility(host)) {
        return {ANOMALY_STATUS_V1_UNAVAILABLE, 0,
            StringView("Tycoon's Deck compatibility profile is unavailable for this build")};
    }
    auto* context = new (std::nothrow) Context(host);
    if (context == nullptr) return {ANOMALY_STATUS_V1_FAILED, 0, {}};
    if (!context->Ready()) {
        delete context;
        return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, StringView("Tycoon's Deck interfaces are unavailable")};
    }
    const auto config = Host(host).Query<AnomalyConfigServiceV1>(ANOMALY_CONFIG_SERVICE_V1_ID);
    if (!config || config->register_schema == nullptr) {
        delete context;
        return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, StringView("configuration service is unavailable")};
    }
    AnomalyGenerationHandleV1 schema{};
    const auto registered = config->register_schema(config->user, StringView(kConfigId), kConfigVersion,
        Bytes(kConfigSchema), &schema);
    if (!Succeeded(registered)) {
        delete context;
        return registered;
    }
    context->ReadSettings();
    *out = context;
    return Ok();
}

AnomalyStatusV1 ANOMALY_CALL Start(void* raw) noexcept {
    try {
        auto& context = *static_cast<Context*>(raw);
        const auto* windows = context.Windows();
        if (windows == nullptr || windows->register_window == nullptr) {
            return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, {}};
        }
        AnomalyWindowSpecV1 spec{};
        spec.struct_size = sizeof(spec);
        spec.id = StringView("tycoons-deck-main");
        const auto title = context.Text().Text("window.title", "Tycoon's Deck Autopilot");
        spec.title = StringView(title);
        spec.initial_width = 430.0F;
        spec.initial_height = 400.0F;
        spec.minimum_width = 300.0F;
        spec.minimum_height = 200.0F;
        spec.maximum_width = 720.0F;
        spec.maximum_height = 640.0F;
        spec.default_open = 1;
        AnomalyGenerationHandleV1 window{};
        const auto registered = windows->register_window(windows->user, &spec, &window);
        if (!Succeeded(registered)) return registered;
        context.SetWindow(window);
        return Ok();
    } catch (...) {
        return {ANOMALY_STATUS_V1_FAILED, 0, {}};
    }
}

AnomalyStatusV1 ANOMALY_CALL Stop(void* raw, std::uint32_t) noexcept {
    auto& context = *static_cast<Context*>(raw);
    context.SetEnabled(false);
    context.SaveSettings();
    const auto* windows = context.Windows();
    const auto window = context.Window();
    if (windows == nullptr || window.id == 0) return Ok();
    const auto released = windows->release_window(windows->user, window);
    context.SetWindow({});
    return Succeeded(released) || released.code == ANOMALY_STATUS_V1_NOT_FOUND ? Ok() : released;
}

void ANOMALY_CALL Unload(void* raw) noexcept { delete static_cast<Context*>(raw); }

void ANOMALY_CALL Update(void* raw, double) noexcept {
    try {
        static_cast<Context*>(raw)->Tick();
    } catch (...) {
    }
}

void ANOMALY_CALL Draw(void* raw, const AnomalyUiServiceV1* ui) noexcept {
    try {
        auto& context = *static_cast<Context*>(raw);
        context.Shown(false);
        if (ui == nullptr || ui->text == nullptr || ui->checkbox == nullptr || ui->button == nullptr) return;
        const auto* windows = context.Windows();
        const auto window = context.Window();
        if (windows == nullptr || window.id == 0) return;
        AnomalyWindowStateV1 state{};
        state.struct_size = sizeof(state);
        if (!Succeeded(windows->state(windows->user, window, &state)) || state.open == 0) return;
        int32_t visible{};
        if (!Succeeded(windows->begin(windows->user, window, 0, &visible))) return;
        if (visible != 0) {
            context.Shown(true);
            Content(context, ui);
        }
        windows->end(windows->user, window);
    } catch (...) {
    }
}

}  // namespace

ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(AnomalyPluginDescriptorV1* descriptor) {
    if (descriptor == nullptr || descriptor->struct_size < sizeof(*descriptor)) {
        return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    }
    *descriptor = {sizeof(*descriptor), ANOMALY_PLUGIN_API_V1_MAJOR, ANOMALY_PLUGIN_API_V1_MINOR,
        StringView(kId), StringView(kTitle), StringView(kAuthor), StringView(kVersion),
        Load, Start, Stop, Unload, Update, Draw};
    return Ok();
}
