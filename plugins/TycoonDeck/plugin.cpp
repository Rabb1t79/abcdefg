#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <anomaly/sdk/cpp.hpp>
#include <anomaly/sdk/localization.hpp>
#include <anomaly/sdk/services/platform.h>
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "flow.hpp"
#include "access.hpp"

namespace {
const AnomalyUe5NamesServiceV1* names{};
const AnomalyUe5ObjectsServiceV1* objects{};
const AnomalyUe5FrameworkServiceV1* framework{};
const AnomalyUe5BuildServiceV1* build_service{};
const AnomalyWindowServiceV1* window_service{};
const AnomalyConfigServiceV1* config_service{};
anomaly::sdk::Localizer localizer;
AnomalyGenerationHandleV1 window_handle{};
AnomalyGenerationHandleV1 schema_handle{};
int fallback_open{1};
Clock::time_point next_services{};
std::map<std::string, AnomalyGenerationHandleV1> functions;
std::map<std::string, Clock::time_point> missing_functions;
#include "ue.hpp"

constexpr std::string_view kSchemaId{"tycoondeck-settings-v1"};
constexpr std::string_view kSchemaDocument{
    R"({"type":"object","additionalProperties":false,"properties":{"enabled":{"type":"boolean"},"fastFinish":{"type":"boolean"}}})"};

std::atomic_bool enabled{};
std::atomic_bool fast_finish{true};
std::atomic_bool settings_dirty{};
std::atomic_uint64_t intent{};
uint64_t applied_intent{};
Clock::time_point next_poll{};
Clock::time_point next_bootstrap{};
int32_t last_action{-1};
uint64_t last_signature{};
int stall_rounds{};
struct View {
    std::string status;
    std::string detail;
    uint64_t settled{};
};
std::atomic<std::shared_ptr<const View>> published;

struct ObjectQuery { uintptr_t context{}, result{}; };
struct IntArray { int32_t* data{}; int32_t count{}, capacity{}; };
static_assert(sizeof(ObjectQuery) == 16 && sizeof(IntArray) == 16);
struct Parameter { const char* name; int32_t offset, size; const char* kind{}; };
struct State {
    uintptr_t player{}, component{};
    tycoon::Phase phase{};
    bool canFinish{}, canSkip{}, pendingPack{};
    int32_t pickCount{}, playsLeft{}, score{}, target{}, money{}, act{}, floor{};
    int32_t handCount{}, cellCount{}, cellX{}, cellY{};
};
struct Pending {
    tycoon::Action action{};
    Clock::time_point sent{};
    tycoon::Phase phase{};
    int32_t score{};
    int attempts{};
    bool active{};
};

void Publish(std::string status, std::string detail = {}) {
    const auto old = published.load();
    if (old && old->status == status && old->detail == detail) return;
    published.store(std::make_shared<const View>(View{std::move(status), std::move(detail), old ? old->settled : 0}));
}
std::string_view MissingRuntimeFeature(const AnomalyUe5BuildServiceV1* service) noexcept {
    if (!service || service->struct_size < offsetof(AnomalyUe5BuildServiceV1, feature_state) + sizeof(service->feature_state) ||
        !service->feature_state) return "anomaly.ue5.build.feature_state";
    for (const auto key : {"ue5.objects", "ue5.names", "ue5.functions", "ue5.process-event"})
        if (service->feature_state(service->user, StringView(key)) != ANOMALY_FEATURE_V1_AVAILABLE) return key;
    return {};
}
bool ParametersMatch(const Reader& r, uintptr_t fn, std::span<const Parameter> parameters) {
    uint32_t flags{};
    // 只调用游戏自己的命名包装函数；绝不手拼 ServerBalatroLikeRequestRPC 的载荷。
    return fn && r.Read(fn + tycoon::profile::snapshot.layout.FunctionFlags, flags) && (flags & 0x400) && !(flags & 0x40) &&
        std::all_of(parameters.begin(), parameters.end(),
            [&](const auto& p) { return p.kind && r.Property(fn, p.name, p.offset, p.size, p.kind); });
}
uintptr_t Checked(const Reader& r, const char* package, const char* owner, const char* name,
    int count, int size, std::initializer_list<Parameter> parameters) {
    const auto fn = Fn(r, package, owner, name, count, size);
    return ParametersMatch(r, fn, {parameters.begin(), parameters.size()}) ? fn : 0;
}
// 数组只读头部计数（TArray = 数据指针 + Num + Max），不搬运元素，避免返回数组的生命周期问题。
int ArrayCount(const Reader& r, uintptr_t object, const char* name) {
    const auto offset = Field(r, object, name, 16, "ArrayProperty");
    int32_t count{};
    if (offset < 0 || !r.Read(object + offset + 8, count) || count < 0 || count > 4096) return -1;
    return count;
}
int ReadCell(const Reader& r, uintptr_t object, int32_t& x, int32_t& y) {
    const auto offset = Field(r, object, "SyncedAvailableGridCells", 16, "ArrayProperty");
    uintptr_t data{};
    int32_t count{};
    if (offset < 0) return -1;
    if (!r.Read(object + offset, data) || !r.Read(object + offset + 8, count) || !data || count <= 0) return 0;
    int32_t cell[2]{};
    if (!r.Read(data, cell)) return -1;
    x = cell[0];
    y = cell[1];
    return count;
}int ReadBool(const Reader& r, uintptr_t object, uintptr_t fn, bool& value) {
    uint8_t raw{};
    if (!fn || !Invoke(r, object, fn, &raw) || raw > 1) return 0;
    value = raw != 0;
    return 1;
}
int ReadInt(const Reader& r, uintptr_t object, uintptr_t fn, int32_t& value) {
    return fn && Invoke(r, object, fn, &value) ? 1 : 0;
}
int ReadPhase(const Reader& r, uintptr_t object, uintptr_t fn, tycoon::Phase& value) {
    uint8_t raw{};
    if (!fn || !Invoke(r, object, fn, &raw) || raw > static_cast<uint8_t>(tycoon::Phase::SettlementAnimating)) return 0;
    value = static_cast<tycoon::Phase>(raw);
    return 1;
}

enum class Stage { Ok, NoWorld, NoPlayer, NoComponent, NoInterface };
bool ReadState(const Reader& r, State& value, Stage& stage) {
    value = {};
    stage = Stage::NoWorld;
    const auto world = World(r);
    const auto util = Find(r, "/Script/HTGame.Default__HTUtil");
    const auto player = Checked(r, "HTGame", "HTUtil", "GetMainPlayerState", 2, 16,
        {{"WorldContextObject", 0, 8, "ObjectProperty"}, {"ReturnValue", 8, 8, "ObjectProperty"}});
    ObjectQuery query{world};
    if (!world || !util || !player || !Invoke(r, util, player, &query)) return false;
    value.player = query.result;
    if (!value.player || !r.IsA(value.player, "HTPlayerState")) return false;
    stage = Stage::NoComponent;
    value.component = Child(r, value.player, "BalatroLikeComponentPtr");
    if (!value.component || !r.IsA(value.component, "HTBalatroLikeComponent")) return false;
    stage = Stage::NoInterface;
    const auto phase = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetCurrentPhase", 1, 1, {{"ReturnValue", 0, 1, "EnumProperty"}});
    const auto finish = Checked(r, "HTGame", "HTBalatroLikeComponent", "CanFinishCurrentBlind", 1, 1, {{"ReturnValue", 0, 1, "BoolProperty"}});
    const auto skip = Checked(r, "HTGame", "HTBalatroLikeComponent", "CanSkipCurrentBlind", 1, 1, {{"ReturnValue", 0, 1, "BoolProperty"}});
    const auto pack = Checked(r, "HTGame", "HTBalatroLikeComponent", "IsPendingPackSelection", 1, 1, {{"ReturnValue", 0, 1, "BoolProperty"}});
    if (!ReadPhase(r, value.component, phase, value.phase) || !ReadBool(r, value.component, finish, value.canFinish) ||
        !ReadBool(r, value.component, skip, value.canSkip) || !ReadBool(r, value.component, pack, value.pendingPack)) return false;
    const auto picks = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetPendingPackPickCount", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto plays = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetPlaysLeft", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto score = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetCurrentScore", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto target = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetBlindTargetScore", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto money = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetMoney", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto act = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetCurrentAct", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    const auto floor = Checked(r, "HTGame", "HTBalatroLikeComponent", "GetCurrentFloor", 1, 4, {{"ReturnValue", 0, 4, "IntProperty"}});
    if (!ReadInt(r, value.component, plays, value.playsLeft) || !ReadInt(r, value.component, picks, value.pickCount)) return false;
    static_cast<void>(ReadInt(r, value.component, score, value.score));
    static_cast<void>(ReadInt(r, value.component, target, value.target));
    static_cast<void>(ReadInt(r, value.component, money, value.money));
    static_cast<void>(ReadInt(r, value.component, act, value.act));
    static_cast<void>(ReadInt(r, value.component, floor, value.floor));
    const auto hand = ArrayCount(r, value.component, "HandCards");
    const auto cells = ReadCell(r, value.component, value.cellX, value.cellY);
    if (hand < 0 || cells < 0) return false;
    value.handCount = hand;
    value.cellCount = cells;
    stage = Stage::Ok;
    return true;
}
// 组件由服务端按请求复制到 PlayerState 上；没有它时先申请，等下一轮再读。
bool RequestComponent(const Reader& r, uintptr_t player) {
    const auto fn = Checked(r, "HTGame", "HTPlayerState", "RequestBalatroLikeComponent", 0, 0, {});
    return fn && player && Invoke(r, player, fn, nullptr);
}

enum class Dispatch { Sent, Waiting, Failed };
Dispatch Submit(const Reader& r, const State& value, tycoon::Action action) {
    uintptr_t fn{};
    IntArray picks{};
    std::array<int32_t, 16> indices{};
    switch (action) {
    case tycoon::Action::StartGame: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestStartGame", 0, 0, {}); break;
    case tycoon::Action::StartBlind: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestStartCurrentBlind", 0, 0, {}); break;
    case tycoon::Action::SkipBlind: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestSkipCurrentBlind", 0, 0, {}); break;
    case tycoon::Action::FinishBlind: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestFinishCurrentBlind", 0, 0, {}); break;
    case tycoon::Action::PlaceBuilding:
        fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestPlaceBuilding", 3, 12,
            {{"InHandIndex", 0, 4, "IntProperty"}, {"InGridX", 4, 4, "IntProperty"}, {"InGridY", 8, 4, "IntProperty"}});
        break;
    case tycoon::Action::SkipShop: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestSkipShop", 0, 0, {}); break;
    case tycoon::Action::Settle: fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestManualSettlement", 0, 0, {}); break;
    case tycoon::Action::ConfirmPack: {
        fn = Checked(r, "HTGame", "HTBalatroLikeComponent", "RequestConfirmPackSelection", 1, 16,
            {{"InSelectedIndices", 0, 16, "ArrayProperty"}});
        const auto count = std::clamp<int32_t>(value.pickCount, 0, static_cast<int32_t>(indices.size()));
        for (int32_t i = 0; i < count; ++i) indices[static_cast<size_t>(i)] = i;
        picks = {indices.data(), count, count};
        break;
    }
    default: return Dispatch::Waiting;
    }
    if (!fn) return Dispatch::Failed;
    // 防止在请求已被接受、状态尚未刷新时重复下发。
    State current;
    Stage current_stage{};
    if (!ReadState(r, current, current_stage) || current.component != value.component || current.phase != value.phase ||
        current.score != value.score) return Dispatch::Waiting;
    std::array<int32_t, 3> place{0, value.cellX, value.cellY};
    const auto parameters = action == tycoon::Action::ConfirmPack ? static_cast<void*>(&picks)
        : action == tycoon::Action::PlaceBuilding ? static_cast<void*>(place.data()) : nullptr;
    return Invoke(r, value.component, fn, parameters) ? Dispatch::Sent : Dispatch::Failed;
}

uint64_t Signature(const State& value) {
    uint64_t h = 1469598103934665603ull;
    const uint32_t parts[6] = {static_cast<uint32_t>(value.phase), static_cast<uint32_t>(value.score), static_cast<uint32_t>(value.playsLeft),
        static_cast<uint32_t>(value.handCount), static_cast<uint32_t>(value.cellCount), static_cast<uint32_t>(value.money)};
    for (const auto part : parts) { h ^= part; h *= 1099511628211ull; }
    return h;
}
bool EnsureWindow() noexcept {
    if (window_handle.id) return true;
    if (!window_service || window_service->struct_size < offsetof(AnomalyWindowServiceV1, end) + sizeof(window_service->end) ||
        !window_service->register_window || !window_service->release_window || !window_service->state ||
        !window_service->begin || !window_service->end) return false;
    AnomalyWindowSpecV1 spec{};
    spec.struct_size = sizeof(spec);
    spec.id = StringView("b1ank-tycoondeck-main");
    const auto title = localizer.Text("window.title", "AutoTycoon");
    spec.title = StringView(title);
    spec.initial_width = 560;
    spec.initial_height = 320;
    spec.minimum_width = 400;
    spec.minimum_height = 220;
    spec.maximum_width = 900;
    spec.maximum_height = 600;
    spec.default_open = 1;
    return window_service->register_window(window_service->user, &spec, &window_handle).code == ANOMALY_STATUS_V1_OK &&
        window_handle.id;
}
void ReleaseWindow() noexcept {
    if (window_handle.id && window_service && window_service->release_window &&
        window_service->release_window(window_service->user, window_handle).code == ANOMALY_STATUS_V1_OK)
        window_handle = {};
}
void RefreshRuntimeServices() {
    if (!api) return;
    const Host services(api);
    names = services.Query<AnomalyUe5NamesServiceV1>(ANOMALY_UE5_NAMES_SERVICE_V1_ID).get();
    objects = services.Query<AnomalyUe5ObjectsServiceV1>(ANOMALY_UE5_OBJECTS_SERVICE_V1_ID).get();
    framework = services.Query<AnomalyUe5FrameworkServiceV1>(ANOMALY_UE5_FRAMEWORK_SERVICE_V1_ID).get();
    build_service = services.Query<AnomalyUe5BuildServiceV1>(ANOMALY_UE5_BUILD_SERVICE_V1_ID).get();
}
bool LoadSettings() {
    tycoon::Settings value{};
    if (config_service && config_service->read) {
        std::array<char, 512> buffer{};
        std::size_t size = buffer.size();
        uint32_t version{};
        if (config_service->read(config_service->user, StringView(kSchemaId), &version,
                {reinterpret_cast<uint8_t*>(buffer.data()), buffer.size()}, &size).code == ANOMALY_STATUS_V1_OK &&
            size > 0 && size <= buffer.size())
            value = tycoon::ParseSettings({buffer.data(), size});
    }
    enabled.store(value.enabled);
    fast_finish.store(value.fastFinish);
    return true;
}
bool SaveSettings() {
    if (!config_service || !config_service->write_atomic) return false;
    const auto document = tycoon::FormatSettings({enabled.load(), fast_finish.load()});
    return config_service->write_atomic(config_service->user, StringView(kSchemaId), 1,
        {reinterpret_cast<const uint8_t*>(document.data()), document.size()}).code == ANOMALY_STATUS_V1_OK;
}
void MarkDirty() { settings_dirty.store(true); }

AnomalyStatusV1 ANOMALY_CALL Load(const AnomalyHostApiV1* host, void** context) noexcept {
    if (!host || !context) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    *context = nullptr;
    registry = world_slot = 0;
    try {
        api = host;
        localizer = anomaly::sdk::Localizer(host);
        if (!tycoon::profile::Load(host)) {
            static thread_local std::string message;
            message = localizer.Text("status.signatures", "Tycoon signatures could not be resolved.");
            return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, StringView(message)};
        }
        const Host services(host);
        window_service = services.Query<AnomalyWindowServiceV1>(ANOMALY_WINDOW_SERVICE_V1_ID).get();
        core = services.Query<AnomalyCoreServiceV1>(ANOMALY_CORE_SERVICE_V1_ID).get();
        config_service = services.Query<AnomalyConfigServiceV1>(ANOMALY_CONFIG_SERVICE_V1_ID).get();
        if (config_service && config_service->register_schema)
            static_cast<void>(config_service->register_schema(config_service->user, StringView(kSchemaId), 1,
                {reinterpret_cast<const uint8_t*>(kSchemaDocument.data()), kSchemaDocument.size()}, &schema_handle));
        RefreshRuntimeServices();
        LoadSettings();
        return core ? Ok() : AnomalyStatusV1{ANOMALY_STATUS_V1_NOT_FOUND, 0, {}};
    } catch (...) { return {ANOMALY_STATUS_V1_FAILED, 0, {}}; }
}
AnomalyStatusV1 ANOMALY_CALL Start(void*) noexcept {
    try {
        enabled.store(false);
        settings_dirty.store(false);
        applied_intent = ++intent;
        registry = world_slot = 0;
        next_poll = {};
        next_bootstrap = {};
        next_services = {};
        ResetBindings();
        published.store({});
        fallback_open = 1;
        registry = tycoon::profile::snapshot.registry;
        world_slot = tycoon::profile::snapshot.world;
        LoadSettings();
        Publish(localizer.Text("status.waiting", "Waiting for a Tycoon's Deck run."));
        static_cast<void>(EnsureWindow());
        return Ok();
    } catch (...) { return {ANOMALY_STATUS_V1_FAILED, 0, {}}; }
}
AnomalyStatusV1 ANOMALY_CALL Stop(void*, uint32_t) noexcept {
    enabled.store(false);
    ++intent;
    if (settings_dirty.exchange(false)) static_cast<void>(SaveSettings());
    registry = world_slot = 0;
    ReleaseWindow();
    return Ok();
}
void ANOMALY_CALL Unload(void*) noexcept {
    registry = world_slot = 0;
    tycoon::profile::snapshot = {};
    ReleaseWindow();
    if (config_service && config_service->unregister_schema && schema_handle.id)
        static_cast<void>(config_service->unregister_schema(config_service->user, schema_handle));
    schema_handle = {};
}

void ANOMALY_CALL Update(void*, double) noexcept {
    try {
        if (!names || !objects || !framework || !build_service) {
            const auto now = Clock::now();
            if (now >= next_services) {
                next_services = now + std::chrono::seconds(2);
                RefreshRuntimeServices();
            }
            if (!names || !objects || !framework || !build_service) {
                Publish(localizer.Text("status.services", "Waiting for the game activity interfaces."));
                return;
            }
        }
        if (!framework->is_game_thread || !framework->is_game_thread(framework->user)) return;
        const auto token = intent.load();
        if (applied_intent != token) { applied_intent = token; next_poll = {}; }
        if (!enabled.load()) return;
        const auto now = Clock::now();
        if (now < next_poll) return;
        next_poll = now + std::chrono::milliseconds(400);
        const auto missing = MissingRuntimeFeature(build_service);
        if (!missing.empty()) {
            Publish(localizer.Format("status.interface_missing", "Missing game interface: {0}",
                std::array<std::string_view, 1>{missing}));
            next_poll = now + std::chrono::seconds(2);
            return;
        }
        if (!registry || !world_slot) {
            Publish(localizer.Text("status.signatures", "Tycoon signatures could not be resolved."));
            next_poll = now + std::chrono::seconds(2);
            return;
        }
        const Reader r{names};
        State value;
        Stage stage{};
        const auto read = ReadState(r, value, stage);
        if (!read) {
            if (stage == Stage::NoComponent && now >= next_bootstrap) {
                next_bootstrap = now + std::chrono::seconds(3);
                // 组件是服务端请求后才复制到 PlayerState 上的；先申请，下一轮再读。
                if (RequestComponent(r, value.player)) {
                    Publish(localizer.Text("status.bootstrap", "Requesting the activity from the server."));
                    return;
                }
            }
            const auto message = stage == Stage::NoWorld ? "status.no_world"
                : stage == Stage::NoPlayer ? "status.no_player"
                : stage == Stage::NoInterface ? "status.interface_missing_activity"
                : "status.no_component";
            const auto fallback = stage == Stage::NoWorld ? "Waiting for the game world."
                : stage == Stage::NoPlayer ? "Waiting for the player state."
                : stage == Stage::NoInterface ? "The activity component does not expose the expected functions."
                : "Press play on the activity page, or enable the plugin again.";
            Publish(localizer.Text(message, fallback));
            next_poll = now + std::chrono::seconds(2);
            return;
        }
        const auto key = PhaseKey(value.phase);
        const auto state_text = localizer.Format("label.state", "Phase {0} act {1} floor {2}",
            std::array<std::string_view, 3>{localizer.Text(key, PhaseFallback(value.phase)),
                std::to_string(value.act), std::to_string(value.floor)});
        const auto score_text = localizer.Format("label.score", "Score {0} / {1} money {2} plays {3}",
            std::array<std::string_view, 4>{std::to_string(value.score), std::to_string(value.target),
                std::to_string(value.money), std::to_string(value.playsLeft)});
        auto decision = tycoon::Decide({value.phase, value.canFinish, value.canSkip, value.pendingPack,
            value.pickCount, value.handCount, value.cellCount});
        if (!fast_finish.load() && decision.action == tycoon::Action::FinishBlind &&
            value.phase == tycoon::Phase::Playing && value.handCount > 0 && value.cellCount > 0)
            decision = {tycoon::Action::PlaceBuilding, "status.place_building", "Placing a building card."};
        if (decision.action == tycoon::Action::None) {
            Publish(localizer.Text("status.settled", "This run has ended."), state_text);
            return;
        }
        const auto signature = Signature(value);
        if (last_action == static_cast<int32_t>(decision.action) && signature == last_signature) ++stall_rounds;
        else { stall_rounds = 0; last_signature = signature; last_action = static_cast<int32_t>(decision.action); }
        if (stall_rounds >= 4) {
            Publish(localizer.Text("status.stalled", "The game stopped accepting the action; automation stopped."), state_text);
            enabled.store(false);
            MarkDirty();
            return;
        }
        const auto outcome = Submit(r, value, decision.action);
        if (outcome == Dispatch::Waiting) {
            Publish(localizer.Text(decision.key, decision.fallback), state_text);
            return;
        }
        if (outcome == Dispatch::Failed) {
            Publish(localizer.Text("status.rejected", "The game rejected the request; automation stopped."), state_text);
            enabled.store(false);
            MarkDirty();
            return;
        }
        Publish(localizer.Text(decision.key, decision.fallback), state_text + " | " + score_text);
        next_poll = now + std::chrono::milliseconds(700);
    } catch (...) {
        enabled.store(false);
        ++intent;
        Publish(localizer.Text("status.data_changed", "Run data changed; automation stopped."));
    }
}

void DrawControls(const AnomalyUiServiceV1* ui) noexcept {
    try {
        if (!ui || !ui->text) return;
        int checked = enabled.load() ? 1 : 0;
        if (ui->checkbox && ui->checkbox(ui->user,
                StringView(localizer.Label("option.enabled", "Enable automatic completion", "tycoondeck-enabled")), &checked)) {
            enabled.store(checked != 0);
            ++intent;
            MarkDirty();
        }
        int fast = fast_finish.load() ? 1 : 0;
        if (ui->checkbox && ui->checkbox(ui->user,
                StringView(localizer.Label("option.fast", "End blinds directly when allowed", "tycoondeck-fast")), &fast)) {
            fast_finish.store(fast != 0);
            MarkDirty();
        }
        ui->text(ui->user, StringView(localizer.Text("instructions.start",
            "Open the activity page, press play, then enable automatic completion.")));
        ui->text(ui->user, StringView(localizer.Text("instructions.rules",
            "Every step uses the game's own request interface and follows its settlement rules.")));
        if (const auto view = published.load()) {
            ui->text(ui->user, StringView(enabled.load() ? view->status
                : localizer.Text("status.off", "Automatic completion is off.")));
            if (!view->detail.empty()) ui->text(ui->user, StringView(view->detail));
        }
    } catch (...) {}
}
void ANOMALY_CALL Draw(void*, const AnomalyUiServiceV1* ui) noexcept {
    try {
        if (!ui || !ui->text) return;
        if (EnsureWindow()) {
            AnomalyWindowStateV1 state{sizeof(state)};
            if (window_service->state(window_service->user, window_handle, &state).code != ANOMALY_STATUS_V1_OK || !state.open) return;
            int32_t visible{};
            if (window_service->begin(window_service->user, window_handle, 0, &visible).code != ANOMALY_STATUS_V1_OK) return;
            if (visible) DrawControls(ui);
            static_cast<void>(window_service->end(window_service->user, window_handle));
            return;
        }
        if (!fallback_open || !ui->begin_window || !ui->end_window) return;
        anomaly::sdk::UiWindow window(ui, localizer.Text("window.title", "AutoTycoon"), &fallback_open);
        if (window) DrawControls(ui);
    } catch (...) {}
}
}

ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(AnomalyPluginDescriptorV1* descriptor) {
    if (!descriptor || descriptor->struct_size < sizeof(*descriptor)) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    *descriptor = {sizeof(*descriptor), ANOMALY_PLUGIN_API_V1_MAJOR, ANOMALY_PLUGIN_API_V1_MINOR,
        StringView("b1ank.tycoondeck"), StringView("AutoTycoon"), StringView("b1ank"), StringView("0.1.0"),
        Load, Start, Stop, Unload, Update, Draw};
    return Ok();
}
