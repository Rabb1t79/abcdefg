#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <anomaly/sdk/cpp.hpp>
#include <anomaly/sdk/localization.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <new>
#include <vector>
#include "draw.hpp"
#include "engine.hpp"
#include "readiness.hpp"

namespace {
using namespace anomaly::sdk;
using Handle = AnomalyGenerationHandleV1;
struct Item { Handle actor{}; gacha::Name pool{}; std::string id, label; };
struct Scope {
    std::uintptr_t world{}, player{}, controller{}, hud{}, wall{}, panel{};
    Handle world_epoch{};
    gacha::Identity identity{};
};
struct View {
    gacha::Identity identity{};
    std::uint64_t round{}, revision{};
    std::vector<Item> items;
    std::string machine, status;
    bool ready{};
};
struct Command {
    gacha::Identity identity{};
    Handle actor{};
    gacha::Name pool{};
    bool ten{};
    std::uint64_t revision{}, expires{};
};
struct Context;
struct InitResult { std::uint64_t serial{}; std::uintptr_t registry{}, world_slot{}; };
struct InitWork {
    Context* context{}; const AnomalyHostApiV1* api{}; const AnomalyCoreServiceV1* core{};
    std::shared_ptr<const gacha::compat::Snapshot> profile;
    std::uint64_t serial{}; std::atomic_bool cancelled{};
};
struct Context {
    ue::Engine engine;
    anomaly::sdk::Localizer localizer;
    const AnomalyWindowServiceV1* windows{};
    const AnomalyUe5ProcessEventServiceV1* event_owner{};
    const AnomalySchedulerServiceV1* scheduler{};
    Handle window{}, subscription{}, game_data{};
    Handle init_task{};
    std::atomic_bool running{};
    std::atomic_bool detection{};
    std::atomic_bool ten{};
    std::atomic<const AnomalyUe5FrameworkServiceV1*> callback_framework{};
    std::atomic_uint64_t revision{}, init_serial{}, init_failed{};
    std::uint64_t applied_revision{}, next_poll{}, next_scan{};
    std::atomic<std::shared_ptr<const InitResult>> initialized;
    std::vector<std::unique_ptr<InitWork>> init_work;
    gacha::ReadyGate gate;
    bool prepared{}, init_requested{};
    std::atomic<std::shared_ptr<const View>> view;
    std::atomic<std::shared_ptr<const Command>> command;
    Scope scope;
    Scope scan_scope;
    gacha::Round round;
    std::atomic_uintptr_t server_fn{}, client_fn{}, open_fn{}, draw_fn{};
    std::uint64_t next_services{}, next_view{}, last_world{};
    std::uint64_t next_data{};
    std::map<std::uint64_t, std::string> labels;
    std::vector<Item> items;
    int fallback_open{1};
    std::uint64_t selected{};
    gacha::Identity selected_scope{};
};
bool Active(ue::Engine& e, std::uintptr_t widget, const char* type) {
    if (!e.Live(widget, type)) return false;
    const auto at = e.Field(widget, "bIsActive", 1, "BoolProperty"), visible = e.Field(widget, "Visibility", 1, "EnumProperty");
    std::uint8_t active{}, visibility{};
    return at >= 0 && visible >= 0 && e.Read(widget + at, active) && active == 1 && e.Read(widget + visible, visibility) &&
        (visibility == 0 || visibility == 3 || visibility == 4);
}
std::uintptr_t ActiveWidget(ue::Engine& e, const Scope& scope, const char* type) {
    struct Query { gacha::Array name{}; std::uintptr_t result{}; };
    static_assert(sizeof(Query) == 24 && offsetof(Query, result) == 16);
    if (!e.Live(scope.hud, "HTHUD") || e.Child(scope.hud, "PlayerOwner") != scope.controller) return 0;
    const auto at = e.Field(scope.hud, "ActivedWidgetMap", e.layout().MapSize, "MapProperty");
    const auto get = e.Function("HTGame", "HTHUD", "BP_GetUIByClass", 2, 24, {{"UIClass", 0, 16, "StrProperty"}, {"ReturnValue", 16, 8, "ObjectProperty"}});
    const auto owner = e.Function("UMG", "Widget", "GetOwningPlayer", 1, 8, {{"ReturnValue", 0, 8, "ObjectProperty"}});
    ue::SparseMap map{};
    if (at < 0 || !get || !owner || !ue::ReadMap(e, scope.hud + at, map, 512, 4096)) return 0;
    for (int i = 0; i < map.slots.count; ++i) {
        auto bits = map.bits[(i / 32) % 4];
        if (map.extra && !e.Read(map.extra + 4 * (i / 32), bits)) return 0;
        if (!(bits & (1u << (i % 32)))) continue;
        std::uintptr_t cls{}; gacha::Name key{};
        if (!e.Read(map.slots.data + e.layout().MapEntryStride * i, cls) || !e.Read(map.slots.data + e.layout().MapEntryStride * i + e.layout().MapValue, key)) continue;
        // The map key can be a blueprint-derived widget class.
        bool matches = false;
        for (int depth = 0; cls && depth < 32; ++depth) { if (e.NameAt(cls + e.layout().ObjectName) == type) { matches = true; break; } if (!e.Read(cls + e.layout().Super, cls)) break; }
        if (!matches) continue;
        const auto label = e.Name(key); if (label.empty() || label.size() > 255) continue;
        const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(), static_cast<int>(label.size()), nullptr, 0);
        if (length <= 0) continue;
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, label.data(), static_cast<int>(label.size()), wide.data(), length);
        Query query{{reinterpret_cast<std::uintptr_t>(wide.c_str()), length + 1, length + 1}}; std::uintptr_t pc{};
        if (ue::Invoke(e, scope.hud, get, &query) && Active(e, query.result, type) && ue::Invoke(e, query.result, owner, &pc) && pc == scope.controller) return query.result;
    }
    return 0;
}
bool BaseScope(ue::Engine& e, Scope& scope) {
    struct Query { std::uintptr_t context{}, value{}; };
    scope.world = e.World(&scope.world_epoch); if (!scope.world) return false;
    const auto utility = e.Find("/Script/HTGame.Default__HTUtil");
    const auto get_ps = e.Function("HTGame", "HTUtil", "GetMainPlayerState", 2, 16, {{"WorldContextObject", 0, 8, "ObjectProperty"}, {"ReturnValue", 8, 8, "ObjectProperty"}});
    const auto get_pc = e.Function("HTGame", "HTUtil", "GetMainPlayerController", 2, 16, {{"WorldContextObject", 0, 8, "ObjectProperty"}, {"ReturnValue", 8, 8, "ObjectProperty"}});
    Query ps{scope.world}, pc{scope.world};
    if (!utility || !get_ps || !get_pc || !ue::Invoke(e, utility, get_ps, &ps) || !ue::Invoke(e, utility, get_pc, &pc)) return false;
    scope.player = ps.value; scope.controller = pc.value;
    if (!e.Live(scope.player, "HTPlayerState") || !e.Live(scope.controller, "HTPlayerController") || e.Child(scope.controller, "PlayerState") != scope.player ||
        e.OwningWorld(scope.player) != scope.world || e.OwningWorld(scope.controller) != scope.world) return false;
    scope.hud = e.Child(scope.controller, "MyHUD");
    scope.wall = ActiveWidget(e, scope, "HTUI_GashaponMain");
    scope.panel = ActiveWidget(e, scope, "HTUI_GashaponMachine");
    const auto world = e.Handle(scope.world), player = e.Handle(scope.player);
    scope.identity = {world.id, world.generation, player.id, player.generation, scope.world_epoch.id, scope.world_epoch.generation};
    return true;
}
std::uintptr_t Row(ue::Engine& e, std::uintptr_t table, const char* type, gacha::Name id) {
    // Read-only DataTable sparse map. The row type and fields are validated
    // before interpreting its contents; sparse holes are never treated as rows.
    const auto row = e.Child(table, "RowStruct");
    if (!e.IsA(table, "DataTable") || !row || e.NameAt(row + e.layout().ObjectName) != type) return 0;
    ue::SparseMap map{};
    if (!ue::ReadMap(e, table + e.layout().DataTableRows, map, 65536, 1048576) || !map.slots.count) return 0;
    std::vector<std::uint8_t> slots(static_cast<std::size_t>(map.slots.count) * e.layout().MapEntryStride);
    std::vector<std::uint32_t> bits(static_cast<std::size_t>((map.count + 31) / 32));
    if (!e.ReadBytes(map.slots.data, slots.data(), slots.size())) return 0;
    if (map.extra) { if (!e.ReadBytes(map.extra, bits.data(), bits.size() * 4)) return 0; }
    else std::copy_n(map.bits.begin(), bits.size(), bits.begin());
    for (int i = 0; i < map.slots.count; ++i) {
        if (!(bits[static_cast<std::size_t>(i / 32)] & (1u << (i % 32)))) continue;
        gacha::Name key{}; std::uintptr_t value{};
        std::memcpy(&key, slots.data() + e.layout().MapEntryStride * i, sizeof(key));
        if (key == id) { std::memcpy(&value, slots.data() + e.layout().MapEntryStride * i + e.layout().MapValue, sizeof(value)); return value; }
    }
    return 0;
}
std::string PoolLabel(Context& c, gacha::Name pool) {
    auto& e = c.engine;
    const auto key = gacha::Pack(pool);
    if (const auto it = c.labels.find(key); it != c.labels.end()) return it->second;
    const auto data = e.Resolve(c.game_data);
    if (!data) return {};
    const auto table = e.Child(data, "GashaponPoolDataTable"), type = e.Child(table, "RowStruct");
    const auto at = e.Offset(type, "PoolName", 16, "TextProperty");
    if (at < 0) return {};
    const auto row = Row(e, table, "GashaponPoolDataConfig", pool);
    std::array<char, 512> text{}; std::size_t size = text.size();
    if (!row || !e.names || !e.names->resolve_ftext_utf8 || e.names->resolve_ftext_utf8(e.names->user, row + at, text.data(), &size).code != 0 ||
        !size || size > text.size()) return {};
    std::string label(text.data(), std::find(text.begin(), text.end(), '\0') - text.begin());
    if (label.empty() || label.size() > 255) return {};
    c.labels[key] = label;
    return label;
}
std::vector<Item> Items(Context& c, const Scope& scope) {
    auto& e = c.engine;
    const auto at = e.Field(scope.wall, "ActorArray", sizeof(gacha::Array), "ArrayProperty");
    gacha::Array array{}; std::vector<Item> result;
    if (at < 0 || !e.Read(scope.wall + at, array) || !ue::ValidArray(array, 64)) return result;
    for (int i = 0; i < array.count; ++i) {
        std::uintptr_t actor{};
        if (!e.Read(array.data + sizeof(std::uintptr_t) * i, actor) || !e.Live(actor, "HTGashaponActor") || e.OwningWorld(actor) != scope.world) continue;
        const auto offset = e.Field(actor, "PoolID", 8, "NameProperty"); gacha::Name pool{};
        if (offset < 0 || !e.Read(actor + offset, pool) || !pool.id) continue;
        const auto id = e.Name(pool); if (id.empty() || id == "None") continue;
        // The same pool can be placed on several identical machines; the draw
        // targets the pool, so one row per pool is enough.
        if (std::any_of(result.begin(), result.end(), [&](const Item& v) { return v.pool == pool; })) continue;
        Item item{e.Handle(actor), pool, id, {}}; item.label = PoolLabel(c, pool); result.push_back(std::move(item));
    }
    return result;
}
bool Selected(ue::Engine& e, std::uintptr_t wall, const Handle& actor) {
    std::uintptr_t current{};
    const auto at = e.Field(wall, "m_GashaponActor", sizeof(std::uintptr_t), "ObjectProperty");
    return at >= 0 && e.Read(wall + at, current) && current && ue::Same(e.Handle(current), actor);
}
void ANOMALY_CALL Observe(void* user, std::uintptr_t object, std::uintptr_t function, void* parameters) noexcept {
    auto& c = *static_cast<Context*>(user);
    // The shared dispatcher sees all ProcessEvents, including loading traffic on
    // other threads. Ignore unrelated functions before any host call.
    const auto server = c.server_fn.load(), client = c.client_fn.load();
    if (!function || (function != server && function != client) || !c.running.load() || !c.detection.load()) return;
    try {
        const auto* framework = c.callback_framework.load();
        if (!framework || !framework->is_game_thread || !framework->is_game_thread(framework->user) || c.applied_revision != c.revision.load()) return;
        if (!parameters || !c.scope.player || object != c.scope.player) return;
        gacha::Rpc rpc{};
        if (!c.engine.Read(reinterpret_cast<std::uintptr_t>(parameters), rpc) || !ue::ValidArray(rpc.parameters.items, 16) || rpc.parameters.cursor != 0) return;
        if (function == server && rpc.message == gacha::kMsgRaffle) c.round.Interrupt();
        else if (function == client && rpc.message == gacha::kMsgSynRaffleResult) c.round.Result(true);
        else if (function == client && rpc.message == gacha::kMsgSynRaffleFailed) c.round.Result(false);
        else return;
        c.next_view = 0;
    } catch (...) {}
}
bool ResolveFunctions(Context& c) {
    auto& e = c.engine;
    c.server_fn = e.Function("HTGame", "HTPlayerState", "ServerGashaponRPC", 2, 32, {{"MsgType", 0, 4, "IntProperty"}, {"WrapperArray", 8, 24, "StructProperty"}}, 0x00200440);
    c.client_fn = e.Function("HTGame", "HTPlayerState", "ClientGashaponRPC", 2, 32, {{"MsgType", 0, 4, "IntProperty"}, {"WrapperArray", 8, 24, "StructProperty"}}, 0x01000440);
    // Widget entry points are implemented in Blueprint, so no native flag is
    // required; the parameter shape is the contract that matters.
    c.open_fn = e.Function("HTGame", "HTUI_GashaponMain", "OnActorClickedDelegate", 1, 8, {{"BindActor", 0, 8, "ObjectProperty"}}, 0);
    c.draw_fn = e.Function("HTGame", "HTUI_GashaponMachine", "OnStartRaffle", 1, 1, {{"InbIsSingle", 0, 1, "BoolProperty"}}, 0);
    const auto array = e.Find("/Script/HottaFramework.ParameterWrapperArray");
    return c.server_fn && c.client_fn && c.open_fn && c.draw_fn &&
        e.Property(array, "ParameterWrappers", 0, 16, "ArrayProperty") && e.Property(array, "ReadParameterIndex", 16, 4, "IntProperty");
}
std::string RoundStatus(Context& c) {
    switch (c.round.state) {
    case gacha::State::Armed: return c.localizer.Text("status.opening", "Opening the selected gashapon machine.");
    case gacha::State::Opening: return c.localizer.Text("status.opening", "Opening the selected gashapon machine.");
    case gacha::State::Ready: return c.localizer.Text("status.ready", "Machine list ready. Select a machine, then press Draw once.");
    case gacha::State::Submitted: return c.localizer.Text("status.pending", "Draw requested. Waiting for the server result.");
    case gacha::State::Success: return c.localizer.Text("status.success", "The server confirmed this draw.");
    case gacha::State::Failed: return c.localizer.Text("status.failed", "The server returned a failed draw. No automatic retry.");
    case gacha::State::Unconfirmed: return c.localizer.Text("status.unconfirmed", "The draw was not confirmed. No automatic retry.");
    case gacha::State::Interrupted: return c.localizer.Text("status.interrupted", "The round was drawn in-game, so the plugin stepped back.");
    default: return {};
    }
}
void Publish(Context& c, const std::string& status, std::vector<Item> items = {}, bool actions = false) {
    auto v = std::make_shared<View>();
    v->identity = c.scope.identity; v->round = c.round.sequence; v->revision = c.applied_revision;
    v->status = status; v->items = std::move(items);
    v->ready = actions && c.running.load() && c.detection.load() && c.subscription.id && c.scope.wall && !c.round.Pending();
    if (c.round.pool.id) v->machine = PoolLabel(c, c.round.pool);
    c.view.store(std::move(v));
}
gacha::ReadinessSample Readiness(ue::Engine& e) {
    AnomalyNteSessionSnapshotV1 session{sizeof(session)};
    AnomalyNtePlayerSnapshotV1 player{sizeof(player)};
    if (!e.session || !e.session->snapshot || !e.player || !e.player->snapshot ||
        e.session->snapshot(e.session->user, &session).code != 0 || session.state != ANOMALY_NTE_SESSION_V1_WORLD_READY ||
        e.player->snapshot(e.player->user, &player).code != 0 || !(player.flags & ANOMALY_NTE_SNAPSHOT_V1_VALID) ||
        (player.flags & ANOMALY_NTE_SNAPSHOT_V1_PARTIAL)) return {};
    return {{session.world.id, session.world.generation, player.handle.id, player.handle.generation}, player.sequence, true};
}
void SetDetection(Context& c, bool enabled) {
    if (c.detection.exchange(enabled) != enabled) c.revision.fetch_add(1);
    c.command.store({});
}
AnomalyStatusV1 Unsubscribe(Context& c) {
    if (!c.subscription.id) return Ok();
    if (!c.event_owner || !c.event_owner->unsubscribe) return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, {}};
    const auto status = c.event_owner->unsubscribe(c.event_owner->user, c.subscription);
    if (status.code != 0 && status.code != ANOMALY_STATUS_V1_NOT_FOUND) return status;
    c.subscription = {}; c.event_owner = nullptr; return Ok();
}
AnomalyStatusV1 ResetActivation(Context& c) {
    c.init_serial.fetch_add(1);
    for (const auto& work : c.init_work) work->cancelled = true;
    c.initialized.store({}); c.init_failed = 0;
    AnomalyStatusV1 cancelled = Ok();
    if (c.init_task.id) {
        cancelled = c.scheduler && c.scheduler->cancel ? c.scheduler->cancel(c.scheduler->user, c.init_task) : AnomalyStatusV1{ANOMALY_STATUS_V1_UNAVAILABLE, 0, {}};
        if (cancelled.code == 0 || cancelled.code == ANOMALY_STATUS_V1_CANCELLED || cancelled.code == ANOMALY_STATUS_V1_NOT_FOUND) { c.init_task = {}; cancelled = Ok(); }
    }
    const auto unsubscribed = Unsubscribe(c);
    c.command.store({});
    c.gate.Reset(); c.prepared = c.init_requested = false;
    c.round.Reset(); c.scope = {}; c.scan_scope = {}; c.game_data = {}; c.labels.clear(); c.items.clear();
    c.next_data = 0; c.last_world = 0;
    c.next_poll = c.next_scan = c.next_services = c.next_view = 0;
    c.server_fn = c.client_fn = c.open_fn = c.draw_fn = 0;
    c.engine.registry = c.engine.world_slot = 0;
    c.engine.functions.clear(); c.engine.fields.clear();
    return cancelled.code != 0 ? cancelled : unsubscribed;
}
void Deactivate(Context& c, const std::string& status) {
    SetDetection(c, false); c.applied_revision = c.revision.load();
    static_cast<void>(ResetActivation(c)); Publish(c, status);
}
void ANOMALY_CALL Initialize(void* user, Handle) noexcept {
    auto& work = *static_cast<InitWork*>(user); auto& c = *work.context;
    try {
        if (work.cancelled.load() || !c.running.load() || !c.detection.load() || work.serial != c.init_serial.load()) return;
        // Only the host Worker touches signature learning/scanning. The Game
        // Engine and its caches are not shared with this callback.
        ue::Engine engine; engine.api = work.api; engine.core = work.core; engine.profile = work.profile;
        const bool valid = engine.Signatures();
        if (work.cancelled.load() || !c.running.load() || !c.detection.load() || work.serial != c.init_serial.load()) return;
        c.initialized.store(std::make_shared<InitResult>(InitResult{work.serial, valid ? engine.registry : 0, valid ? engine.world_slot : 0}));
    } catch (...) {
        c.init_failed = work.serial;
    }
}
bool ScheduleInitialization(Context& c) {
    if (c.init_requested) return true;
    if (!c.scheduler || !c.scheduler->schedule || !c.scheduler->cancel) return false;
    auto work = std::make_unique<InitWork>(); work->context = &c; work->api = c.engine.api; work->core = c.engine.core; work->profile = c.engine.profile; work->serial = c.init_serial.load();
    auto* user = work.get(); c.init_work.push_back(std::move(work)); c.init_requested = true;
    // callback_user remains alive until Unload, after the host drains scoped
    // Worker callbacks. Cancellation also rejects any late completion by serial.
    if (c.scheduler->schedule(c.scheduler->user, 0, Initialize, user, &c.init_task).code != 0) {
        user->cancelled = true; c.init_task = {}; return false;
    }
    return true;
}
bool Member(const std::vector<Item>& items, const Handle& actor, gacha::Name pool) {
    return std::any_of(items.begin(), items.end(), [&](const Item& i) { return i.pool == pool && ue::Same(i.actor, actor); });
}
void Advance(Context& c, Scope& scope, std::uint64_t now) {
    auto& e = c.engine;
    const Handle actor{c.round.actor, c.round.actor_generation};
    if (c.round.state == gacha::State::Armed) {
        // The player may already have opened the armed machine himself. The wall
        // selection is what proves the open panel belongs to this round.
        if (scope.panel && scope.wall && Selected(e, scope.wall, actor)) {
            c.round.Present(scope.identity, now);
            c.next_view = 0;
        } else if (scope.wall) {
            struct Click { std::uintptr_t actor; };
            Click click{e.Resolve(actor)};
            if (!click.actor || !ue::Invoke(e, scope.wall, c.open_fn, &click)) {
                c.round.Reset(); Publish(c, c.localizer.Text("status.layout", "The gashapon interface or its parameter layout does not match; actions are unavailable."));
                return;
            }
            c.round.Opening(now); c.next_view = 0;
        }
    } else if (c.round.state == gacha::State::Opening && scope.panel) {
        c.round.Present(scope.identity, now);
        c.next_view = 0;
    }
    if (c.round.state == gacha::State::Ready && scope.panel && c.round.CanSubmit(scope.identity)) {
        if (!c.round.Submit(scope.identity, now)) return;
        struct Toggle { std::uint8_t value; };
        Toggle toggle{static_cast<std::uint8_t>(c.round.ten ? 0 : 1)};
        if (!ue::Invoke(e, scope.panel, c.draw_fn, &toggle)) c.round.state = gacha::State::Unconfirmed;
        c.next_view = 0;
    }
}
void ReleaseWindow(Context& c) {
    if (c.window.id && c.windows && c.windows->release_window && c.windows->release_window(c.windows->user, c.window).code == 0) c.window = {};
}
bool EnsureWindow(Context& c) {
    if (c.window.id) return true;
    if (!c.windows || !c.windows->register_window || !c.windows->begin || !c.windows->end || !c.windows->state || !c.windows->release_window) return false;
    AnomalyWindowSpecV1 spec{}; spec.struct_size = sizeof(spec); spec.id = StringView("autogacha-main");
    const auto title = c.localizer.Text("window.title", "AutoGacha"); spec.title = StringView(title);
    spec.initial_width = 560; spec.initial_height = 480; spec.minimum_width = 380; spec.minimum_height = 220; spec.maximum_width = 1100; spec.maximum_height = 1000; spec.default_open = 1;
    return c.windows->register_window(c.windows->user, &spec, &c.window).code == 0 && c.window.id;
}
AnomalyStatusV1 ANOMALY_CALL Load(const AnomalyHostApiV1* host, void** out) noexcept {
    if (!host || !out) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    *out = nullptr;
    try {
        auto c = std::make_unique<Context>(); c->engine.api = host; c->localizer = anomaly::sdk::Localizer(host);
        c->engine.profile = gacha::compat::Load(host);
        if (!c->engine.profile) {
            static thread_local std::string message;
            message = c->localizer.Text("error.profile", "The gashapon compatibility profile is unavailable or incompatible. Detection was not started.");
            return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, StringView(message)};
        }
        c->engine.core = c->engine.Query<AnomalyCoreServiceV1>(ANOMALY_CORE_SERVICE_V1_ID);
        c->windows = c->engine.Query<AnomalyWindowServiceV1>(ANOMALY_WINDOW_SERVICE_V1_ID);
        c->scheduler = c->engine.Query<AnomalySchedulerServiceV1>(ANOMALY_SCHEDULER_SERVICE_V1_ID);
        if (!c->engine.core) return {ANOMALY_STATUS_V1_NOT_FOUND, 0, {}};
        *out = c.release(); return Ok();
    } catch (...) { return {ANOMALY_STATUS_V1_FAILED, 0, {}}; }
}
AnomalyStatusV1 ANOMALY_CALL Start(void* user) noexcept {
    if (!user) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    auto& c = *static_cast<Context*>(user);
    try {
        SetDetection(c, false); c.applied_revision = c.revision.load();
        const auto reset = ResetActivation(c); if (reset.code != 0) return reset;
        c.view.store({}); c.fallback_open = 1; c.selected = 0; c.selected_scope = {}; c.ten = false;
        c.running = true; static_cast<void>(EnsureWindow(c));
        Publish(c, c.localizer.Text("status.idle", "Detection is off. Enter the game, then enable gashapon detection here."));
        return Ok();
    } catch (...) { c.running = false; return {ANOMALY_STATUS_V1_FAILED, 0, {}}; }
}
AnomalyStatusV1 ANOMALY_CALL Stop(void* user, std::uint32_t) noexcept {
    if (!user) return Ok(); auto& c = *static_cast<Context*>(user); c.running = false; SetDetection(c, false);
    const auto reset = ResetActivation(c); ReleaseWindow(c); return reset;
}
void ANOMALY_CALL Unload(void* user) noexcept {
    if (!user) return; auto& c = *static_cast<Context*>(user);
    if (Stop(user, 0).code != 0) return;
    delete &c;
}
void Poll(Context& c, std::uint64_t now) {
    auto& e = c.engine;
    if (!e.Resolve(c.game_data) && now >= c.next_data) {
        c.next_data = now + 2000;
        const auto utility = e.Find("/Script/HTGame.Default__HTGameData");
        const auto getter = e.Function("HTGame", "HTGameData", "GetGameData", 1, 8, {{"ReturnValue", 0, 8, "ObjectProperty"}});
        std::uintptr_t data{};
        if (utility && getter && ue::Invoke(e, utility, getter, &data) && e.IsA(data, "HTGameData")) c.game_data = e.Handle(data);
    }
    Scope scope;
    if (!BaseScope(e, scope)) {
        Deactivate(c, c.localizer.Text("status.scene", "Player or scene changed. Detection was stopped; enable it again after loading finishes."));
        return;
    }
    const auto world = e.Handle(scope.world);
    if (c.last_world && c.last_world != world.id) {
        Deactivate(c, c.localizer.Text("status.scene", "Player or scene changed. Detection was stopped; enable it again after loading finishes."));
        return;
    }
    c.last_world = world.id; c.scan_scope = scope; c.scope = scope;
    if (!scope.identity.Valid()) {
        if (now >= c.next_view) { c.next_view = now + 2000; Publish(c, c.localizer.Text("status.player", "Waiting for stable local player and world snapshots.")); }
        return;
    }
    if (scope.wall || scope.panel) {
        if (!c.subscription.id) {
            if (!ResolveFunctions(c) || e.events->subscribe(e.events->user, Observe, &c, &c.subscription).code != 0 || !c.subscription.id) {
                Deactivate(c, c.localizer.Text("status.layout", "The gashapon interface or its parameter layout does not match; actions are unavailable."));
                return;
            }
            c.event_owner = e.events;
        }
    } else {
        c.scope.wall = c.scope.panel = 0;
        if (!c.round.Pending()) static_cast<void>(Unsubscribe(c));
    }
    if (scope.wall) c.items = Items(c, scope);
    else c.items.clear();
    if (const auto command = c.command.load()) {
        c.command.store({});
        const bool current = command->identity == c.scope.identity && command->revision == c.applied_revision && command->revision == c.revision.load();
        if (!current || (command->expires && now >= command->expires)) { c.next_view = 0; }
        else if (!scope.wall) Publish(c, c.localizer.Text("status.open", "Open a gashapon wall in the game and wait for its machine list."));
        else if (!Member(c.items, command->actor, command->pool)) Publish(c, c.localizer.Text("status.layout", "The gashapon interface or its parameter layout does not match; actions are unavailable."));
        else if (c.round.Arm(c.scope.identity, command->actor.id, command->actor.generation, command->pool, command->ten, now)) c.next_view = 0;
    }
    Advance(c, scope, now);
    c.round.Tick(now);
    if (now < c.next_view) return;
    c.next_view = now + 500;
    if (const auto status = RoundStatus(c); !status.empty()) { Publish(c, status, c.items, true); return; }
    if (scope.wall) {
        Publish(c, c.items.empty() ? c.localizer.Text("status.machine", "Reading the machines on this gashapon wall. Please wait.") :
            c.localizer.Text("status.ready", "Machine list ready. Select a machine, then press Draw once."), c.items, true);
    } else {
        Publish(c, c.localizer.Text("status.open", "Open a gashapon wall in the game and wait for its machine list."));
    }
}
void Tick(Context& c, std::uint64_t now) {
    auto& e = c.engine;
    if (!c.running.load()) return;
    if (c.applied_revision != c.revision.load()) {
        c.applied_revision = c.revision.load();
        if (ResetActivation(c).code != 0) {
            Deactivate(c, c.localizer.Text("status.cleanup", "A previous callback could not be released. Disable and reload the plugin.")); return;
        }
        Publish(c, c.detection.load() ? c.localizer.Text("status.player", "Waiting for stable local player and world snapshots.") :
            c.localizer.Text("status.idle", "Detection is off. Enter the game, then enable gashapon detection here."));
    }
    // A restored plugin never touches UE objects, hooks or signatures until the
    // user explicitly enables detection in this process.
    if (!c.detection.load()) return;
    if (now >= c.next_services) {
        c.next_services = now + 2000; e.Refresh(); c.callback_framework = e.framework;
        if (!c.init_task.id) c.scheduler = e.Query<AnomalySchedulerServiceV1>(ANOMALY_SCHEDULER_SERVICE_V1_ID);
    }
    if (!e.framework || !e.framework->is_game_thread || !e.framework->is_game_thread(e.framework->user)) return;
    if (now < c.next_scan) return; c.next_scan = now + 33;
    const auto sample = Readiness(e);
    const bool same = c.gate.Same(sample), ready = c.gate.Observe(sample, now);
    if ((c.prepared || c.init_requested) && (!same || !ready)) {
        Deactivate(c, c.localizer.Text("status.scene", "Player or scene changed. Detection was stopped; enable it again after loading finishes.")); return;
    }
    if (!ready) {
        if (now >= c.next_view) { c.next_view = now + 2000; Publish(c, c.localizer.Text("status.player", "Waiting for stable local player and world snapshots.")); } return;
    }
    if (const auto result = c.initialized.exchange({}); result && result->serial == c.init_serial.load()) {
        c.init_task = {};
        if (!result->registry || !result->world_slot) {
            Deactivate(c, c.localizer.Text("status.signature", "Object or world signature unavailable. Enable detection again after the framework is ready.")); return;
        }
        e.registry = result->registry; e.world_slot = result->world_slot; c.prepared = true; c.next_poll = 0;
    }
    if (c.init_failed.load() == c.init_serial.load()) {
        Deactivate(c, c.localizer.Text("status.signature", "Object or world signature unavailable. Enable detection again after the framework is ready.")); return;
    }
    if (!c.prepared) {
        if (!ScheduleInitialization(c)) {
            Deactivate(c, c.localizer.Text("status.scheduler", "The initialization worker is unavailable. Reload the plugin after the framework is ready.")); return;
        }
        if (now >= c.next_view) { c.next_view = now + 2000; Publish(c, c.localizer.Text("status.initializing", "Preparing gashapon detection in the background.")); } return;
    }
    if (!e.names || !e.objects || !e.worlds || !e.events || !e.events->subscribe || !e.events->unsubscribe || !e.events->invoke || !e.Features()) {
        if (now >= c.next_view) { c.next_view = now + 2000; Publish(c, c.localizer.Text("status.services", "Waiting for game reflection and ProcessEvent services.")); } return;
    }
    if (now >= c.next_poll) { c.next_poll = now + 250; Poll(c, now); }
}
void ANOMALY_CALL Update(void* user, double) noexcept {
    if (!user) return; auto& c = *static_cast<Context*>(user);
    try { Tick(c, ue::Now()); }
    catch (...) {
        try { Deactivate(c, c.localizer.Text("status.scene", "Player or scene changed. Detection was stopped; enable it again after loading finishes.")); }
        catch (...) { SetDetection(c, false); }
    }
}
void Controls(Context& c, const AnomalyUiServiceV1* ui) {
    int enabled = c.detection.load() ? 1 : 0;
    if (ui->checkbox && ui->checkbox(ui->user, StringView(c.localizer.Label("option.detection", "Enable gashapon detection", "autogacha-detection")), &enabled)) SetDetection(c, enabled != 0);
    int ten = c.ten.load() ? 1 : 0;
    if (ui->checkbox && ui->checkbox(ui->user, StringView(c.localizer.Label("option.ten", "Ten draw", "autogacha-ten")), &ten)) c.ten = ten != 0;
    ui->text(ui->user, StringView(c.localizer.Text("instructions.flow", "Enable detection, open a gashapon wall, pick a machine, then press Draw once. A draw spends in-game currency and is never retried.")));
    ui->text(ui->user, StringView(c.localizer.Text("instructions.result", "Draw results and rewards follow the game settlement.")));
    const auto view = c.view.load(); if (!view) return;
    ui->text(ui->user, StringView(view->status));
    if (!c.detection.load() || view->revision != c.revision.load()) return;
    if (!view->machine.empty()) ui->text(ui->user, StringView(c.localizer.Format("label.pool", "Current machine: {0}", std::array<std::string_view, 1>{view->machine})));
    if (c.selected_scope != view->identity) { c.selected_scope = view->identity; c.selected = 0; }
    if (ui->separator) ui->separator(ui->user);
    if (view->items.empty()) ui->text(ui->user, StringView(c.localizer.Text("label.no_pools", "No machines found on the active gashapon wall yet.")));
    const auto count = std::to_string(view->items.size());
    ui->text(ui->user, StringView(c.localizer.Format("label.count", "Machines on this wall: {0}", std::array<std::string_view, 1>{count})));
    if (ui->checkbox) for (const auto& item : view->items) {
        int selected = c.selected == gacha::Pack(item.pool) ? 1 : 0;
        const auto text = c.localizer.Format("item.choice", "{0}", std::array<std::string_view, 1>{item.label.empty() ? item.id : item.label});
        const auto id = text + "###autogacha-pool-" + std::to_string(gacha::Pack(item.pool));
        if (ui->checkbox(ui->user, StringView(id), &selected) && selected) c.selected = gacha::Pack(item.pool);
    }
    const auto chosen = std::find_if(view->items.begin(), view->items.end(), [&](const Item& i) { return gacha::Pack(i.pool) == c.selected; });
    if (chosen == view->items.end()) return;
    if (ui->button_enabled && ui->button_enabled(ui->user, StringView(c.localizer.Label("action.draw", "Draw once", "autogacha-draw")), 0, 0, view->ready ? 1 : 0)) {
        c.command.store(std::make_shared<Command>(Command{view->identity, chosen->actor, chosen->pool, c.ten.load(), view->revision, ue::Now() + 5000}));
    }
}
void ANOMALY_CALL Draw(void* user, const AnomalyUiServiceV1* ui) noexcept {
    if (!user || !ui || !ui->text) return; auto& c = *static_cast<Context*>(user);
    try {
        if (EnsureWindow(c)) {
            AnomalyWindowStateV1 state{sizeof(state)};
            if (c.windows->state(c.windows->user, c.window, &state).code != 0 || !state.open) return;
            std::int32_t visible{}; if (c.windows->begin(c.windows->user, c.window, 0, &visible).code != 0) return;
            try { if (visible) Controls(c, ui); } catch (...) {}
            static_cast<void>(c.windows->end(c.windows->user, c.window)); return;
        }
        if (!c.fallback_open || !ui->begin_window || !ui->end_window) return;
        UiWindow window(ui, c.localizer.Text("window.title", "AutoGacha"), &c.fallback_open);
        if (window) Controls(c, ui);
    } catch (...) {}
}
}
ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(AnomalyPluginDescriptorV1* descriptor) {
    if (!descriptor || descriptor->struct_size < sizeof(*descriptor)) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    *descriptor = {sizeof(*descriptor), ANOMALY_PLUGIN_API_V1_MAJOR, ANOMALY_PLUGIN_API_V1_MINOR,
        StringView("b1ank.autogacha"), StringView("AutoGacha"), StringView("b1ank"), StringView("0.1.0"), Load, Start, Stop, Unload, Update, Draw};
    return Ok();
}
