#include "profile_test_setup.hpp"
#include "plugin.cpp"
#include <cstdlib>
#include <iostream>

namespace fixture {
void Check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
AnomalyStatusV1 Missing() { return {ANOMALY_STATUS_V1_NOT_FOUND, 0, {}}; }
AnomalyStatusV1 ReadMemory(void*, std::uintptr_t address, AnomalyMutableByteSpanV1 out) {
    __try { std::memcpy(out.data, reinterpret_cast<const void*>(address), out.size); return Ok(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Missing(); }
}
struct Game {
    std::vector<std::vector<std::uint8_t>> storage;
    std::vector<std::uintptr_t> objects;
    std::map<std::uint32_t, std::string> strings;
    std::map<std::string, std::uint32_t> ids;
    std::map<std::string, Handle> paths;
    std::map<std::wstring, std::uintptr_t> widget_names;
    anomaly::testing::ProfileHost profile_host{"AutoGacha"};
    AnomalyHostApiV1 host{};
    AnomalyCoreServiceV1 core{sizeof(core), 1, this};
    AnomalyUe5NamesServiceV1 names{sizeof(names), 1, this};
    AnomalyUe5ObjectsServiceV1 object_service{sizeof(object_service), 1, this};
    AnomalyUe5WorldServiceV1 world_service{sizeof(world_service), 1, this};
    AnomalyUe5FrameworkServiceV1 framework{sizeof(framework), 1, this};
    AnomalyUe5BuildServiceV1 build{sizeof(build), 1, this};
    AnomalyUe5ProcessEventServiceV1 events{sizeof(events), 1, this};
    AnomalySignatureServiceV1 signatures{sizeof(signatures), 1, this};
    AnomalySchedulerServiceV1 scheduler{sizeof(scheduler), 1, this};
    AnomalyNteSessionServiceV1 session{sizeof(session), 1, this};
    AnomalyNtePlayerServiceV1 player_service{sizeof(player_service), 1, this};
    struct Task { Handle handle{}; AnomalyTaskCallbackV1 callback{}; void* user{}; bool cancelled{}, ran{}; };
    std::vector<Task> tasks;
    std::uintptr_t registry{}, chunk{}, world{}, level{}, player{}, controller{}, hud{}, wall{}, wall_class{};
    std::uintptr_t panel{}, panel_class{}, actor_a{}, actor_b{}, doll_array{};
    std::uintptr_t get_ps{}, get_pc{}, get_ui{}, get_owner{}, server{}, client{}, open{}, draw{}, get_data{};
    std::uintptr_t current_world{};
    std::uint64_t world_epoch{77};
    std::uint64_t player_epoch{88}, snapshot_sequence{100};
    std::uint32_t session_state{ANOMALY_NTE_SESSION_V1_WORLD_READY}, player_flags{ANOMALY_NTE_SNAPSHOT_V1_VALID};
    bool worker{}, reject_invoke{};
    std::uintptr_t text_address{};
    gacha::Name pool_a{}, pool_b{};
    Context* observed{};
    unsigned opens{}, draws{}, translations{}, signature_calls{}, reads{}, object_counts{}, snapshots{}, finds{}, native_calls{}, player_queries{}, subscriptions{}, unsubscribed{}, scheduled{}, cancelled{}, game_queries{}, semantic_calls{}, thread_queries{};
    std::uint8_t last_toggle{0xff};
    std::uintptr_t last_open{};

    std::uintptr_t Buffer(std::size_t size) { storage.emplace_back(size); return reinterpret_cast<std::uintptr_t>(storage.back().data()); }
    void Put(std::uintptr_t at, const auto& value) { std::memcpy(reinterpret_cast<void*>(at), &value, sizeof(value)); }
    gacha::Name Name(const std::string& text) {
        if (const auto it = ids.find(text); it != ids.end()) return {it->second, 0};
        const auto id = static_cast<std::uint32_t>(strings.size() + 1); strings[id] = text; ids[text] = id; return {id, 0};
    }
    Handle H(std::uint32_t index) const { return {(std::uint64_t{7} << 32) | (std::uint64_t{index} + 1), 11}; }
    Handle H(std::uintptr_t object) const { int index{}; std::memcpy(&index, reinterpret_cast<void*>(object + 12), 4); return H(static_cast<std::uint32_t>(index)); }
    void Register(std::uintptr_t object) {
        const auto index = static_cast<std::uint32_t>(objects.size()); objects.push_back(object);
        Put(object + 12, static_cast<std::int32_t>(index)); Put(chunk + 24 * index, object); Put(chunk + 24 * index + 16, std::uint32_t{7}); Put(registry + 36, static_cast<std::uint32_t>(objects.size()));
    }
    std::pair<std::uintptr_t, std::uintptr_t> Object(const char* type, std::size_t size, const std::string& path = {}) {
        const auto cls = Buffer(128); Register(cls); Put(cls + 24, Name(type));
        const auto object = Buffer(size); Register(object); Put(object + 16, cls);
        if (!path.empty()) paths[path] = H(object);
        return {object, cls};
    }
    std::uintptr_t Property(std::uintptr_t cls, const char* name, int offset, int size, const char* kind = "ObjectProperty") {
        const auto p = Buffer(80); std::uintptr_t previous{}; std::memcpy(&previous, reinterpret_cast<void*>(cls + 112), 8);
        const auto field_class = Buffer(40); Put(field_class, Name(kind)); Put(p + 8, field_class);
        Put(p + 32, Name(name)); Put(p + 52, size); Put(p + 68, offset); Put(p + 72, previous); Put(cls + 112, p); return p;
    }
    std::uintptr_t Function(const char* package, const char* owner, const char* name, int count, int size, std::initializer_list<ue::Parameter> parameters, std::uint32_t flags = 0x400) {
        const auto [fn, cls] = Object("Function", 224, std::string("/Script/") + package + "." + owner + "." + name);
        static_cast<void>(cls); Put(fn + 24, Name(name)); Put(fn + 176, flags); Put(fn + 180, static_cast<std::uint8_t>(count)); Put(fn + 182, static_cast<std::uint16_t>(size));
        for (const auto& p : parameters) Property(fn, p.name, p.offset, p.size, p.kind); return fn;
    }
    void SetWallActive(bool active) { Put(wall + 64, static_cast<std::uint8_t>(active ? 1 : 0)); }
    void SetPanelActive(bool active) { Put(panel + 64, static_cast<std::uint8_t>(active ? 1 : 0)); }
    void SetWallSelection(std::uintptr_t actor) { Put(wall + 112, actor); }
    Game() {
        PrepareGachaProfile(profile_host); registry = Buffer(512);
        const auto recorded = RecordedGachaCode(); Put(registry, recorded);
        chunk = Buffer(24 * 2048); const auto chunks = Buffer(8); Put(registry + 16, chunks); Put(chunks, chunk);
        core.read_memory = [](void* user, std::uintptr_t at, AnomalyMutableByteSpanV1 out) { auto& f = *static_cast<Game*>(user); ++f.reads; return ReadMemory(user, at, out); };
        names.resolve_utf8 = [](void* user, std::uint32_t id, char* out, std::size_t* size) {
            auto& f = *static_cast<Game*>(user); const auto it = f.strings.find(id); if (it == f.strings.end()) return Missing();
            const auto bytes = it->second.size() + 1; if (!out || *size < bytes) { *size = bytes; return AnomalyStatusV1{ANOMALY_STATUS_V1_BUFFER_TOO_SMALL, 0, {}}; }
            std::memcpy(out, it->second.c_str(), bytes); *size = bytes; return Ok();
        };
        object_service.count = [](void* user) { auto& f = *static_cast<Game*>(user); ++f.object_counts; return static_cast<std::uint32_t>(f.objects.size()); };
        object_service.snapshot_at = [](void* user, std::uint32_t index, AnomalyUe5ObjectSnapshotV1* out) { auto& f = *static_cast<Game*>(user); ++f.snapshots; if (index >= f.objects.size()) return Missing(); out->handle = f.H(index); return Ok(); };
        object_service.snapshot_by_handle = [](void* user, Handle h, AnomalyUe5ObjectSnapshotV1* out) {
            auto& f = *static_cast<Game*>(user); ++f.snapshots; const auto index = ANOMALY_UE5_OBJECT_HANDLE_INDEX(h);
            if (index >= f.objects.size() || !ue::Same(h, f.H(index))) return Missing(); out->handle = h; return Ok();
        };
        object_service.find_exact = [](void* user, AnomalyStringViewV1 name, Handle* out) {
            auto& f = *static_cast<Game*>(user); ++f.finds; const auto it = f.paths.find(std::string(name.data, name.size)); if (it == f.paths.end()) return Missing(); *out = it->second; return Ok();
        };
        world_service.current = [](void* user, Handle* out) { auto& f = *static_cast<Game*>(user); if (!f.current_world) return Missing(); *out = {1, f.world_epoch}; return Ok(); };
        world_service.snapshot = [](void* user, Handle h, AnomalyUe5WorldSnapshotV1* out) {
            auto& f = *static_cast<Game*>(user); if (!f.current_world || h.id != 1 || h.generation != f.world_epoch) return Missing();
            out->handle = h; out->change_sequence = f.world_epoch; std::uint32_t id{}; std::memcpy(&id, reinterpret_cast<void*>(f.current_world + 24), 4); out->name_id = id; return Ok();
        };
        framework.is_game_thread = [](void* user) { auto& f = *static_cast<Game*>(user); ++f.thread_queries; return f.worker ? 0 : 1; };
        build.feature_state = [](void*, AnomalyStringViewV1) { return static_cast<std::uint32_t>(ANOMALY_FEATURE_V1_AVAILABLE); };
        events.invoke = [](void* user, Handle receiver, Handle function, void* parameters, std::size_t size) {
            auto& f = *static_cast<Game*>(user);
            Check(!f.worker, "native invocation ran on the Worker");
            const auto receiver_index = ANOMALY_UE5_OBJECT_HANDLE_INDEX(receiver), function_index = ANOMALY_UE5_OBJECT_HANDLE_INDEX(function);
            if (f.reject_invoke || receiver_index >= f.objects.size() || function_index >= f.objects.size() ||
                !ue::Same(receiver, f.H(receiver_index)) || !ue::Same(function, f.H(function_index))) return Missing();
            std::uint16_t expected{}; std::memcpy(&expected, reinterpret_cast<void*>(f.objects[function_index] + 182), sizeof(expected));
            Check(size == expected && (!size || parameters), "public ProcessEvent invocation received an invalid parameter capacity");
            f.Call(f.objects[receiver_index], f.objects[function_index], parameters); return Ok();
        };
        events.subscribe = [](void* user, AnomalyUe5ProcessEventCallbackV1, void* observer, Handle* h) { auto& f = *static_cast<Game*>(user); ++f.subscriptions; f.observed = static_cast<Context*>(observer); *h = {9, 1}; return Ok(); };
        events.unsubscribe = [](void* user, Handle h) { auto& f = *static_cast<Game*>(user); Check(h.id == 9, "unsubscribe used a different service's handle"); ++f.unsubscribed; f.observed = nullptr; return Ok(); };
        scheduler.schedule = [](void* user, std::uint32_t, AnomalyTaskCallbackV1 callback, void* data, Handle* h) { auto& f = *static_cast<Game*>(user); *h = {++f.scheduled, 1}; f.tasks.push_back({*h, callback, data}); return Ok(); };
        scheduler.cancel = [](void* user, Handle h) {
            auto& f = *static_cast<Game*>(user);
            for (auto& task : f.tasks) if (ue::Same(task.handle, h) && !task.ran && !task.cancelled) { task.cancelled = true; ++f.cancelled; return AnomalyStatusV1{ANOMALY_STATUS_V1_CANCELLED, 0, {}}; }
            return Missing();
        };
        session.snapshot = [](void* user, AnomalyNteSessionSnapshotV1* out) { auto& f = *static_cast<Game*>(user); ++f.semantic_calls; out->state = f.session_state; out->sequence = f.world_epoch; out->world = {1, f.world_epoch}; return Ok(); };
        player_service.snapshot = [](void* user, AnomalyNtePlayerSnapshotV1* out) { auto& f = *static_cast<Game*>(user); ++f.semantic_calls; out->flags = f.player_flags; out->sequence = f.snapshot_sequence; out->handle = {2, f.player_epoch}; return Ok(); };
        signatures.resolve = [](void* user, AnomalyStringViewV1 module, AnomalyStringViewV1 section, AnomalyStringViewV1 pattern, std::uintptr_t* out) {
            auto& f = *static_cast<Game*>(user); ++f.signature_calls;
            Check(f.worker, "signature scanning ran outside the host Worker");
            Check(std::string_view(module.data, module.size) == "HTGame.exe" && std::string_view(section.data, section.size) == ".text", "wrong startup signature domain");
            try {
                const auto hits = ue5mem::Pattern::Parse({pattern.data, pattern.size}).FindAll(
                    std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(f.registry), 512), 2);
                if (hits.size() != 1) return Missing(); *out = f.registry + hits[0]; return Ok();
            } catch (...) { return Missing(); }
        };
        host.struct_size = sizeof(host); host.api_major = ANOMALY_PLUGIN_API_V1_MAJOR; host.api_minor = ANOMALY_PLUGIN_API_V1_MINOR; host.host_context = this;
        host.query_service = [](void* user, AnomalyStringViewV1 key, std::uint32_t, const void** out) {
            auto& f = *static_cast<Game*>(user); const std::string_view id(key.data, key.size); *out = nullptr;
            if (id.starts_with("anomaly.ue5.") || id.starts_with("anomaly.nte.") || id == ANOMALY_SIGNATURE_SERVICE_V1_ID) ++f.game_queries;
            if (id == ANOMALY_CORE_SERVICE_V1_ID) *out = &f.core;
            else if (id == ANOMALY_UE5_NAMES_SERVICE_V1_ID) *out = &f.names;
            else if (id == ANOMALY_UE5_OBJECTS_SERVICE_V1_ID) *out = &f.object_service;
            else if (id == ANOMALY_UE5_WORLD_SERVICE_V1_ID) *out = &f.world_service;
            else if (id == ANOMALY_UE5_FRAMEWORK_SERVICE_V1_ID) *out = &f.framework;
            else if (id == ANOMALY_UE5_BUILD_SERVICE_V1_ID) *out = &f.build;
            else if (id == ANOMALY_UE5_PROCESS_EVENT_SERVICE_V1_ID) *out = &f.events;
            else if (id == ANOMALY_SIGNATURE_SERVICE_V1_ID) *out = &f.signatures;
            else if (id == ANOMALY_SCHEDULER_SERVICE_V1_ID) *out = &f.scheduler;
            else if (id == ANOMALY_NTE_SESSION_SERVICE_V1_ID) *out = &f.session;
            else if (id == ANOMALY_NTE_PLAYER_SERVICE_V1_ID) *out = &f.player_service;
            else if (id == ANOMALY_LOCALIZATION_SERVICE_V1_ID) ++f.translations;
            else if (id == ANOMALY_PROFILE_SERVICE_V1_ID || id == ANOMALY_JSON_SERVICE_V1_ID)
                return anomaly::testing::ProfileHost::Query(&f.profile_host, key, 1, out);
            return *out ? Ok() : Missing();
        };
        const auto [w, wc] = Object("World", 128); world = current_world = w; static_cast<void>(wc); Put(w + 24, Name("FixtureWorld")); Put(registry + 64, w);
        const auto [l, lc] = Object("Level", 192); level = l; Property(lc, "OwningWorld", 64, 8); Put(l + 64, w);
        const auto [ps, psc] = Object("HTPlayerState", 128); player = ps; static_cast<void>(psc); Put(ps + 32, l);
        const auto [pc, pcc] = Object("HTPlayerController", 128); controller = pc; Put(pc + 32, l); Property(pcc, "PlayerState", 64, 8); Put(pc + 64, ps);
        const auto [h, hc] = Object("HTHUD", 192); hud = h; Property(hc, "PlayerOwner", 64, 8); Put(h + 64, pc); Property(hc, "ActivedWidgetMap", 80, 80, "MapProperty");
        Property(pcc, "MyHUD", 72, 8); Put(pc + 72, h);
        const auto [wl, wlc] = Object("HTUI_GashaponMain", 192); wall = wl; wall_class = wlc;
        Property(wlc, "bIsActive", 64, 1, "BoolProperty"); Property(wlc, "Visibility", 65, 1, "EnumProperty"); Put(wl + 64, std::uint8_t{1});
        Property(wlc, "ActorArray", 96, 16, "ArrayProperty"); Property(wlc, "m_GashaponActor", 112, 8);
        const auto [pn, pnc] = Object("HTUI_GashaponMachine", 160); panel = pn; panel_class = pnc;
        Property(pnc, "bIsActive", 64, 1, "BoolProperty"); Property(pnc, "Visibility", 65, 1, "EnumProperty"); Put(pn + 64, std::uint8_t{0});
        widget_names[L"GashaponWall"] = wall; widget_names[L"GashaponPanel"] = panel;
        const auto slots = Buffer(48); Put(slots, wall_class); Put(slots + 8, Name("GashaponWall")); Put(slots + 24, panel_class); Put(slots + 32, Name("GashaponPanel"));
        Put(h + 80, gacha::Array{slots, 2, 2}); Put(h + 96, std::uint32_t{3}); Put(h + 120, std::int32_t{2}); Put(h + 124, std::int32_t{128});
        const auto [aa, aac] = Object("HTGashaponActor", 128); actor_a = aa; Put(aa + 32, l); Property(aac, "PoolID", 64, 8, "NameProperty"); pool_a = Name("Pool_Alpha"); Put(aa + 64, pool_a);
        const auto [ab, abc] = Object("HTGashaponActor", 128); actor_b = ab; Put(ab + 32, l); Property(abc, "PoolID", 64, 8, "NameProperty"); pool_b = Name("Pool_Beta"); Put(ab + 64, pool_b);
        doll_array = Buffer(16); Put(doll_array, aa); Put(doll_array + 8, ab); Put(wall + 96, gacha::Array{doll_array, 2, 2});
        Object("HTUtil", 64, "/Script/HTGame.Default__HTUtil");
        get_ps = Function("HTGame", "HTUtil", "GetMainPlayerState", 2, 16, {{"WorldContextObject", 0, 8, "ObjectProperty"}, {"ReturnValue", 8, 8, "ObjectProperty"}});
        get_pc = Function("HTGame", "HTUtil", "GetMainPlayerController", 2, 16, {{"WorldContextObject", 0, 8, "ObjectProperty"}, {"ReturnValue", 8, 8, "ObjectProperty"}});
        get_ui = Function("HTGame", "HTHUD", "BP_GetUIByClass", 2, 24, {{"UIClass", 0, 16, "StrProperty"}, {"ReturnValue", 16, 8, "ObjectProperty"}});
        get_owner = Function("UMG", "Widget", "GetOwningPlayer", 1, 8, {{"ReturnValue", 0, 8, "ObjectProperty"}});
        open = Function("HTGame", "HTUI_GashaponMain", "OnActorClickedDelegate", 1, 8, {{"BindActor", 0, 8, "ObjectProperty"}}, 0);
        draw = Function("HTGame", "HTUI_GashaponMachine", "OnStartRaffle", 1, 1, {{"InbIsSingle", 0, 1, "BoolProperty"}}, 0);
        server = Function("HTGame", "HTPlayerState", "ServerGashaponRPC", 2, 32, {{"MsgType", 0, 4, "IntProperty"}, {"WrapperArray", 8, 24, "StructProperty"}}, 0x00200440);
        client = Function("HTGame", "HTPlayerState", "ClientGashaponRPC", 2, 32, {{"MsgType", 0, 4, "IntProperty"}, {"WrapperArray", 8, 24, "StructProperty"}}, 0x01000440);
        const auto [wa, wac] = Object("ScriptStruct", 128, "/Script/HottaFramework.ParameterWrapperArray"); static_cast<void>(wac);
        Property(wa, "ParameterWrappers", 0, 16, "ArrayProperty"); Property(wa, "ReadParameterIndex", 16, 4, "IntProperty");
        Object("HTGameData", 128, "/Script/HTGame.Default__HTGameData");
        get_data = Function("HTGame", "HTGameData", "GetGameData", 1, 8, {{"ReturnValue", 0, 8, "ObjectProperty"}});
    }
    void Bind(Context& c) {
        c.engine.api = &host; c.engine.profile = gacha::compat::Load(&host); Check(static_cast<bool>(c.engine.profile), "test Profile did not load");
        c.engine.Refresh(); c.engine.registry = registry; c.engine.world_slot = registry + 64;
        c.localizer = anomaly::sdk::Localizer(&host); c.running = c.detection = true; c.prepared = true; c.init_serial = 1;
        c.callback_framework = &framework; c.scheduler = &scheduler; c.applied_revision = c.revision.load();
        c.gate.identity = {1, world_epoch, 2, player_epoch}; c.gate.observed = true; c.gate.sequence = snapshot_sequence;
        c.gate.progress = ue::Now(); c.gate.since = c.gate.progress - 2000;
        Check(ResolveFunctions(c), "reflected gashapon contract did not match");
        Check(BaseScope(c.engine, c.scope) && c.scope.identity.Valid(), "local player, HUD or wall was not bound");
        observed = &c;
    }
    void Call(std::uintptr_t object, std::uintptr_t fn, void* parameters) {
        ++native_calls;
        if (observed) Observe(observed, object, fn, parameters);
        if (fn == get_ps) { ++player_queries; Put(reinterpret_cast<std::uintptr_t>(parameters) + 8, player); }
        else if (fn == get_pc) Put(reinterpret_cast<std::uintptr_t>(parameters) + 8, controller);
        else if (fn == get_ui) {
            const auto& key = *static_cast<const gacha::Array*>(parameters);
            const std::wstring label(reinterpret_cast<const wchar_t*>(key.data), static_cast<std::size_t>(key.count - 1));
            const auto found = widget_names.find(label);
            Put(reinterpret_cast<std::uintptr_t>(parameters) + 16, found == widget_names.end() ? std::uintptr_t{} : found->second);
        }
        else if (fn == get_owner) Put(reinterpret_cast<std::uintptr_t>(parameters), controller);
        else if (fn == open) {
            Check(object == wall, "wall click ran on another widget");
            last_open = *static_cast<std::uintptr_t*>(parameters);
            Check(last_open == actor_a || last_open == actor_b, "wall click named an unknown machine");
            ++opens;
        }
        else if (fn == draw) {
            Check(object == panel, "draw ran on another widget");
            last_toggle = *static_cast<std::uint8_t*>(parameters);
            ++draws;
        }
    }
};
}

namespace {
using namespace fixture;

void RoundMachine() {
    gacha::Round round;
    const gacha::Identity id{1, 2, 3, 4, 5, 6};
    Check(!round.Arm({}, 10, 11, gacha::Name{12, 0}, false, 0), "invalid identity armed a round");
    Check(!round.Arm(id, 0, 11, gacha::Name{12, 0}, false, 0), "missing machine armed a round");
    Check(round.Arm(id, 10, 11, gacha::Name{12, 0}, false, 0) && round.state == gacha::State::Armed, "selection did not arm");
    Check(!round.Arm(id, 10, 11, gacha::Name{12, 0}, false, 1), "pending round armed twice");
    round.Present(id, 2);
    Check(round.state == gacha::State::Ready, "machine panel did not advance the round");
    Check(!round.Submit({9, 9, 9, 9, 9, 9}, 3), "foreign identity submitted");
    Check(round.Submit(id, 3) && round.state == gacha::State::Submitted, "ready round refused to submit");
    Check(!round.Submit(id, 4), "round submitted twice");
    round.Result(false);
    Check(round.state == gacha::State::Failed, "failed result was dropped");
    round.Result(true);
    Check(round.state == gacha::State::Failed, "a finished round changed result");
    gacha::Round late;
    late.Arm(id, 10, 11, gacha::Name{12, 0}, false, 0);
    late.Opening(1);
    late.Tick(9000);
    Check(late.state == gacha::State::Unconfirmed, "opening never timed out");
    late.Present(id, 9001);
    Check(late.state == gacha::State::Unconfirmed, "late panel revived a timed-out round");
    gacha::Round stolen;
    stolen.Arm(id, 10, 11, gacha::Name{12, 0}, false, 0);
    stolen.Interrupt();
    Check(stolen.state == gacha::State::Interrupted && !stolen.Pending(), "in-game draw did not free the round");
}

void ReflectedContract() {
    Game f; Context c; f.Bind(c);
    f.Property(f.wall_class, "bIsActive", 64, 1, "ByteProperty"); c.engine.fields.clear();
    Check(!Active(c.engine, f.wall, "HTUI_GashaponMain"), "same-sized non-boolean active field was accepted");
    f.Property(f.wall_class, "bIsActive", 64, 1, "BoolProperty"); c.engine.fields.clear();
    Check(Active(c.engine, f.wall, "HTUI_GashaponMain"), "restored active property could not be resolved");
    f.Property(f.server, "MsgType", 0, 4, "FloatProperty");
    Check(!ResolveFunctions(c) && !f.draws, "same-sized wrong RPC parameter type was accepted");
    f.Property(f.server, "MsgType", 0, 4, "IntProperty"); Check(ResolveFunctions(c), "valid RPC type did not recover");
    f.Property(f.draw, "InbIsSingle", 0, 1, "IntProperty");
    Check(!ResolveFunctions(c), "draw entry accepted a non-boolean toggle");
    f.Property(f.draw, "InbIsSingle", 0, 1, "BoolProperty"); Check(ResolveFunctions(c), "restored draw entry did not resolve");
}

void WallListing() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    const auto view = c.view.load();
    Check(view && view->items.size() == 2, "wall did not list both machines");
    Check(view->items[0].id == "Pool_Alpha" && view->items[1].id == "Pool_Beta", "pool identifiers were reordered or lost");
    Check(view->ready && view->revision == c.applied_revision, "a fully readable wall did not enable actions");
    Check(f.subscriptions == 1 && c.subscription.id, "wall did not subscribe to ProcessEvent");
    // A sparse array hole and an actor from another world are never machines.
    f.Put(f.wall + 96, gacha::Array{f.doll_array, 2, 2});
    f.Put(f.actor_b + 32, std::uintptr_t{0});
    c.labels.clear();
    Poll(c, now + 600);
    const auto reduced = c.view.load();
    Check(reduced && reduced->items.size() == 1 && reduced->items[0].id == "Pool_Alpha", "unowned machine stayed selectable");
    f.Put(f.actor_b + 32, f.level);
    // An inconsistent array header is rejected instead of trusted.
    f.Put(f.wall + 96, gacha::Array{f.doll_array, 4, 2});
    Poll(c, now + 1200);
    Check(c.view.load()->items.empty(), "inconsistent array header produced machines");
}
void SceneChange() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    Check(f.subscriptions == 1, "wall never subscribed");
    // The world object disappears: the plugin must stop instead of reading a
    // stale world or leaving its ProcessEvent observer installed.
    f.current_world = 0;
    Poll(c, now + 1);
    Check(!c.detection.load(), "world loss left detection on");
    Check(!c.subscription.id && f.unsubscribed == 1, "world loss kept the ProcessEvent subscription");
    Check(
        c.view.load()->status.find("scene") != std::string::npos ||
            c.view.load()->status.find("Player or scene") != std::string::npos,
        "world loss did not report a scene change");
}

void DrawFlow() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    const auto view = c.view.load();
    Check(view && view->items.size() == 2 && view->ready, "wall was not ready for a draw");
    // The player picks the first machine and presses Draw once.
    c.command.store(std::make_shared<Command>(Command{c.scope.identity, view->items[0].actor, view->items[0].pool, false, c.applied_revision, now + 5000}));
    Poll(c, now + 1);
    Check(f.opens == 1 && f.last_open == f.actor_a, "the chosen machine was not opened through the wall");
    Check(c.round.state == gacha::State::Opening && f.draws == 0, "draw ran before the machine panel existed");
    // The game opens the machine and reports it as the wall selection.
    f.SetWallSelection(f.actor_a); f.SetPanelActive(true);
    Poll(c, now + 2);
    Check(f.draws == 1 && f.last_toggle == 1, "single draw did not press the game's single button");
    Check(c.round.state == gacha::State::Submitted, "draw was not tracked as pending");
    // The game answers with its own result message.
    gacha::Rpc reply{}; reply.message = gacha::kMsgSynRaffleResult;
    Observe(&c, f.player, f.client, &reply);
    Check(c.round.state == gacha::State::Success, "server confirmation was dropped");
    Poll(c, now + 3);
    Check(f.draws == 1 && f.opens == 1, "a finished round drew again");
    // A command whose revision predates the applied one cannot spend a round.
    c.command.store(std::make_shared<Command>(Command{c.scope.identity, view->items[1].actor, view->items[1].pool, false, c.applied_revision - 1, now + 6000}));
    Poll(c, now + 4);
    Check(f.draws == 1 && f.opens == 1, "a stale UI command spent a round");
}

void TenDraw() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    const auto view = c.view.load();
    c.command.store(std::make_shared<Command>(Command{c.scope.identity, view->items[1].actor, view->items[1].pool, true, c.applied_revision, now + 5000}));
    Poll(c, now + 1);
    Check(f.last_open == f.actor_b, "ten draw opened the wrong machine");
    f.SetWallSelection(f.actor_b); f.SetPanelActive(true);
    Poll(c, now + 2);
    Check(f.draws == 1 && f.last_toggle == 0, "ten draw did not press the game's ten button");
}

void ManualDraw() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    const auto view = c.view.load();
    c.command.store(std::make_shared<Command>(Command{c.scope.identity, view->items[0].actor, view->items[0].pool, false, c.applied_revision, now + 5000}));
    Poll(c, now + 1);
    Check(c.round.state == gacha::State::Opening, "round was not opening");
    // The player draws by himself before the panel is reachable.
    gacha::Rpc request{}; request.message = gacha::kMsgRaffle;
    Observe(&c, f.player, f.server, &request);
    Check(c.round.state == gacha::State::Interrupted, "in-game draw did not interrupt the round");
    f.SetWallSelection(f.actor_a); f.SetPanelActive(true);
    Poll(c, now + 2);
    Check(f.draws == 0, "the plugin drew after the player already drew");
}

void FailedDraw() {
    Game f; Context c; f.Bind(c);
    const auto now = ue::Now();
    Poll(c, now);
    const auto view = c.view.load();
    c.command.store(std::make_shared<Command>(Command{c.scope.identity, view->items[0].actor, view->items[0].pool, false, c.applied_revision, now + 5000}));
    Poll(c, now + 1);
    f.SetWallSelection(f.actor_a); f.SetPanelActive(true);
    Poll(c, now + 2);
    Check(f.draws == 1, "draw did not run");
    gacha::Rpc reply{}; reply.message = gacha::kMsgSynRaffleFailed;
    Observe(&c, f.player, f.client, &reply);
    Check(c.round.state == gacha::State::Failed, "failed result was dropped");
    // No automatic retry: the plugin must stay out of the way until asked again.
    Poll(c, now + 3); Poll(c, now + 4);
    Check(f.draws == 1 && f.opens == 1, "a failed draw was retried automatically");
}
}

int main() {
    RoundMachine();
    ReflectedContract();
    WallListing();
    SceneChange();
    DrawFlow();
    TenDraw();
    ManualDraw();
    FailedDraw();
    std::cout << "autogacha: round machine, reflected contract, wall listing, draw flow and failure gates passed\n";
    return 0;
}
