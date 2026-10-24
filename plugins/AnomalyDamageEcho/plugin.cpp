#include "anomaly/sdk/anomaly_sdk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kMaxTicksPerSecond = 1000;
constexpr std::size_t kMaxTargets = 64;
constexpr std::size_t kMaxEventsPerUpdate = 128;
constexpr std::size_t kMaxApplicationsPerUpdate = 64;

constexpr char kPluginId[] = "anomaly.local.damage-echo";
constexpr char kPluginName[] = "Damage Echo";
constexpr char kPluginAuthor[] = "Anomaly";
constexpr char kPluginVersion[] = "1.0.0";

struct EchoTarget {
    AnomalyGenerationHandleV1 world{};
    AnomalyGenerationHandleV1 victim{};
    std::int64_t damage{};
    std::uint32_t damage_type{};
    std::uint32_t display_type{};
    std::uint32_t reaction_type{};
    std::uint32_t reaction_display_type{};
    std::uint32_t flags{};
    double accumulator{};
    std::uint32_t failures{};
    bool native_started{};
};

struct SharedState {
    std::mutex mutex;
    bool enabled{true};
    std::uint32_t mode{};  // 0 = exact HP + damage text, 1 = native DoT GE.
    std::uint32_t ticks_per_second{4};
    std::uint32_t dot_index{};
    std::vector<std::string> dot_names;
    int pending_dot{-1};
    std::unordered_map<std::uint64_t, EchoTarget> targets;
    std::string status{"等待 Game 域伤害事件"};
    std::uint64_t cursor{};
    std::uint64_t last_world_generation{};
    std::uint64_t last_player_id{};
    std::uint64_t last_player_generation{};
};

struct PluginContext {
    const AnomalyHostApiV1* host{};
    const AnomalyUiServiceV1* ui{};
    const AnomalyNteCombatServiceV1* combat{};
    const AnomalyNteDamageEchoServiceV1* echo{};
    const AnomalyUe5BuildServiceV1* build{};
    SharedState shared;
    double service_retry{};
    int window_open{1};
};

[[nodiscard]] AnomalyStringViewV1 View(const char* text) noexcept {
    return {text, text != nullptr ? std::strlen(text) : 0};
}

template <typename Service>
const Service* QueryService(const AnomalyHostApiV1* host, const char* id,
                            std::uint32_t version = 1) noexcept {
    if (host == nullptr || host->query_service == nullptr) return nullptr;
    const void* table{};
    const AnomalyStringViewV1 service_id{
        id, std::strlen(id)};
    if (host->query_service(host->host_context, service_id, version, &table).code !=
            ANOMALY_STATUS_V1_OK ||
        table == nullptr) {
        return nullptr;
    }
    const auto* service = static_cast<const Service*>(table);
    if (service->struct_size < sizeof(Service) || service->service_version < version) return nullptr;
    return service;
}

void SetStatus(SharedState& shared, const char* text) {
    shared.status = text != nullptr ? text : "";
}

bool IsPlayer(const AnomalyNteCombatantSnapshotV1& player,
              const AnomalyGenerationHandleV1 participant) noexcept {
    return player.character.id != 0 && participant.id != 0 &&
        player.character.id == participant.id &&
        player.character.generation == participant.generation;
}

std::uint32_t RequestFlags(const AnomalyNteCombatEventV1& event) noexcept {
    std::uint32_t flags = ANOMALY_NTE_DAMAGE_ECHO_V1_SHOW_DAMAGE;
    if ((event.flags & ANOMALY_NTE_COMBAT_EVENT_V1_CRITICAL) != 0) {
        flags |= ANOMALY_NTE_DAMAGE_ECHO_V1_CRITICAL;
    }
    if ((event.flags & ANOMALY_NTE_COMBAT_EVENT_V1_HEAD_HIT) != 0) {
        flags |= ANOMALY_NTE_DAMAGE_ECHO_V1_HEAD_HIT;
    }
    if ((event.flags & ANOMALY_NTE_COMBAT_EVENT_V1_WEAK_UNBALANCE) != 0) {
        flags |= ANOMALY_NTE_DAMAGE_ECHO_V1_WEAK_UNBALANCE;
    }
    return flags;
}

void FillRequest(const EchoTarget& target, AnomalyNteDamageEchoRequestV1& request) noexcept {
    request = {};
    request.struct_size = sizeof(request);
    request.flags = target.flags;
    request.world = target.world;
    request.victim = target.victim;
    request.damage = target.damage;
    request.damage_type = target.damage_type;
    request.display_type = target.display_type;
    request.reaction_type = target.reaction_type;
    request.reaction_display_type = target.reaction_display_type;
}

void RememberFirstHit(
    SharedState& shared,
    const AnomalyNteCombatEventV1& event,
    const AnomalyNteCombatantSnapshotV1& player) {
    if (event.kind != ANOMALY_NTE_COMBAT_EVENT_V1_DAMAGE ||
        !IsPlayer(player, event.source) || event.target.id == 0) {
        return;
    }
    const std::int64_t damage = event.final_value != 0 ? event.final_value : event.value;
    if (damage <= 0) return;
    if (shared.targets.size() >= kMaxTargets &&
        !shared.targets.contains(event.target.id)) {
        SetStatus(shared, "目标数量达到上限，忽略新的回响");
        return;
    }
    const auto [iterator, inserted] = shared.targets.try_emplace(event.target.id);
    if (!inserted) return;
    EchoTarget& target = iterator->second;
    target.world = event.world;
    target.victim = event.target;
    target.damage = damage;
    target.damage_type = event.damage_type;
    target.display_type = event.display_type;
    target.reaction_type = event.reaction_type;
    target.reaction_display_type = event.reaction_display_type;
    target.flags = RequestFlags(event);
    target.accumulator = 0.0;
}

void ApplyTargets(PluginContext& context, double delta_seconds) {
    SharedState& shared = context.shared;
    const std::uint32_t rate = std::clamp<std::uint32_t>(
        shared.ticks_per_second, 1U, kMaxTicksPerSecond);
    const double interval = 1.0 / static_cast<double>(rate);
    std::size_t applications{};
    std::vector<std::uint64_t> remove;
    remove.reserve(shared.targets.size());
    for (auto& [key, target] : shared.targets) {
        (void)key;
        target.accumulator += (std::max)(delta_seconds, 0.0);
        while (target.accumulator >= interval &&
               applications < kMaxApplicationsPerUpdate) {
            target.accumulator -= interval;
            ++applications;
            if (shared.mode == 1 && target.native_started) {
                target.accumulator = 0.0;
                break;
            }
            AnomalyNteDamageEchoRequestV1 request{};
            FillRequest(target, request);
            request.ticks_per_second = shared.ticks_per_second;
            AnomalyNteDamageEchoResultV1 result{};
            result.struct_size = sizeof(result);
            const auto status = shared.mode == 0
                ? context.echo->apply_manual(context.echo->user, &request, &result)
                : shared.mode == 1
                    ? context.echo->apply_native_dot(context.echo->user, &request, &result)
                    : context.echo->apply_session(context.echo->user, &request, &result);
            if (status.code == ANOMALY_STATUS_V1_OK) {
                target.failures = 0;
                if (shared.mode == 1) target.native_started = true;
                if ((result.flags & ANOMALY_NTE_DAMAGE_ECHO_V1_DEAD) != 0 ||
                    result.dead != 0) {
                    remove.push_back(target.victim.id);
                    break;
                }
                continue;
            }
            if (status.code == ANOMALY_STATUS_V1_NOT_FOUND ||
                status.code == ANOMALY_STATUS_V1_INVALID_ARGUMENT) {
                remove.push_back(target.victim.id);
                break;
            }
            if (++target.failures >= 3) {
                remove.push_back(target.victim.id);
                if (status.message.size != 0 && status.message.data != nullptr) {
                    shared.status.assign(status.message.data, status.message.size);
                } else if (shared.mode == 0 && status.code == ANOMALY_STATUS_V1_FAILED) {
                    SetStatus(shared, "A 模式写入被游戏拒绝；可切换到 B 原生 DoT");
                } else if (status.code == ANOMALY_STATUS_V1_PERMISSION_DENIED) {
                    SetStatus(shared, "宿主拒绝了伤害回响服务权限");
                } else {
                    SetStatus(shared, "伤害回响应用失败，已停止该目标");
                }
                break;
            }
        }
    }
    for (const auto id : remove) shared.targets.erase(id);
}

void RefreshServices(PluginContext& context) {
    if (context.combat == nullptr) {
        context.combat = QueryService<AnomalyNteCombatServiceV1>(
            context.host, ANOMALY_NTE_COMBAT_SERVICE_V1_ID);
    }
    if (context.echo == nullptr) {
        context.echo = QueryService<AnomalyNteDamageEchoServiceV1>(
            context.host, ANOMALY_NTE_DAMAGE_ECHO_SERVICE_V1_ID,
            ANOMALY_NTE_DAMAGE_ECHO_SERVICE_V1_VERSION);
    }
    if (context.build == nullptr) {
        context.build = QueryService<AnomalyUe5BuildServiceV1>(
            context.host, ANOMALY_UE5_BUILD_SERVICE_V1_ID);
    }
}

void DrainEvents(PluginContext& context, const AnomalyNteCombatantSnapshotV1& player) {
    SharedState& shared = context.shared;
    for (std::size_t count{}; count < kMaxEventsPerUpdate; ++count) {
        AnomalyNteCombatEventV1 event{};
        event.struct_size = sizeof(event);
        const auto status = context.combat->next_event(
            context.combat->user, shared.cursor, &event);
        if (status.code != ANOMALY_STATUS_V1_OK) break;
        if (event.sequence <= shared.cursor) break;
        shared.cursor = event.sequence;
        if (event.world.generation != 0 &&
            shared.last_world_generation != 0 &&
            event.world.generation != shared.last_world_generation) {
            shared.targets.clear();
        }
        if (event.world.generation != 0) {
            shared.last_world_generation = event.world.generation;
        }
        RememberFirstHit(shared, event, player);
    }
}

void RefreshDotChoices(PluginContext& context) {
    if (context.echo == nullptr || context.echo->dot_count == nullptr ||
        context.echo->dot_name_at == nullptr) return;
    {
        std::scoped_lock lock(context.shared.mutex);
        if (!context.shared.dot_names.empty()) return;
    }
    const auto count = context.echo->dot_count(context.echo->user);
    std::vector<std::string> names;
    names.reserve((std::min)(count, 64U));
    for (std::uint32_t index = 0; index < count && index < 64U; ++index) {
        std::size_t size{};
        if (context.echo->dot_name_at(context.echo->user, index, nullptr, &size).code !=
                ANOMALY_STATUS_V1_BUFFER_TOO_SMALL || size == 0 || size > 256) {
            continue;
        }
        std::string name(size, '\0');
        if (context.echo->dot_name_at(context.echo->user, index, name.data(), &size).code ==
            ANOMALY_STATUS_V1_OK) {
            if (const auto end = name.find('\0'); end != std::string::npos) name.resize(end);
            if (!name.empty()) names.push_back(std::move(name));
        }
    }
    std::scoped_lock lock(context.shared.mutex);
    context.shared.dot_names = std::move(names);
    if (context.shared.dot_index >= context.shared.dot_names.size()) {
        context.shared.dot_index = 0;
    }
}

void Update(PluginContext& context, double delta_seconds) {
    if (context.host == nullptr) return;
    context.service_retry += (std::max)(delta_seconds, 0.0);
    if (context.service_retry >= 0.5) {
        context.service_retry = 0.0;
        RefreshServices(context);
    RefreshDotChoices(context);
    {
        int pending = -1;
        {
            std::scoped_lock lock(context.shared.mutex);
            pending = context.shared.pending_dot;
            context.shared.pending_dot = -1;
        }
        if (pending >= 0 && context.echo != nullptr &&
            context.echo->select_dot != nullptr) {
            if (context.echo->select_dot(context.echo->user,
                    static_cast<std::uint32_t>(pending)).code == ANOMALY_STATUS_V1_OK) {
                std::scoped_lock lock(context.shared.mutex);
                context.shared.dot_index = static_cast<std::uint32_t>(pending);
            }
        }
    }
    }
    if (context.combat == nullptr || context.echo == nullptr) return;

    AnomalyNteCombatantSnapshotV1 player{};
    player.struct_size = sizeof(player);
    if (context.combat->current_combatant(context.combat->user, &player).code !=
            ANOMALY_STATUS_V1_OK ||
        (player.flags & ANOMALY_NTE_COMBATANT_V1_VALID) == 0) {
        return;
    }

    std::scoped_lock lock(context.shared.mutex);
    if (!context.shared.enabled) return;
    if (context.shared.cursor == 0) {
        const auto latest = context.combat->latest_event_sequence(context.combat->user);
        // Consume the most recent event on the first service-visible tick. The
        // initial attack can otherwise arrive on the same tick this cursor was
        // initialized and be skipped forever.
        context.shared.cursor = latest > 0 ? latest - 1 : 0;
    }
    if (context.shared.last_player_id != player.character.id ||
        context.shared.last_player_generation != player.character.generation) {
        context.shared.last_player_id = player.character.id;
        context.shared.last_player_generation = player.character.generation;
        context.shared.targets.clear();
    }
    DrainEvents(context, player);
    if (context.shared.targets.empty()) {
        SetStatus(context.shared, "等待玩家对怪物的有效伤害");
        return;
    }
    ApplyTargets(context, delta_seconds);
    char status[160]{};
    std::snprintf(status, sizeof(status), "模式 %s / 目标 %zu / 速率 %u 次每秒",
        context.shared.mode == 0 ? "A 精确回响" : "B 原生 DoT",
        context.shared.targets.size(),
        static_cast<unsigned>(context.shared.ticks_per_second));
    context.shared.status = status;
}

void Draw(PluginContext& context, const AnomalyUiServiceV1* ui) {
    if (ui == nullptr || ui->begin_window == nullptr) return;
    bool enabled{};
    std::uint32_t mode{};
    std::uint32_t rate{};
    std::size_t targets{};
    std::string status;
    std::vector<std::string> dot_names;
    std::uint32_t dot_index{};
    {
        std::scoped_lock lock(context.shared.mutex);
        enabled = context.shared.enabled;
        mode = context.shared.mode;
        rate = context.shared.ticks_per_second;
        targets = context.shared.targets.size();
        status = context.shared.status;
        dot_names = context.shared.dot_names;
        dot_index = context.shared.dot_index;
    }
    const bool visible = ui->begin_window(
        ui->user, View("伤害回响 / Damage Echo"), &context.window_open, 0) != 0;
    if (visible) {
        const auto text = [ui](const char* value) {
            if (value != nullptr) ui->text(ui->user, View(value));
        };
        text("对玩家首次命中的每个怪物，按固定速率重复该次伤害，直到目标死亡。");
        int enabled_int = enabled ? 1 : 0;
        if (ui->checkbox(ui->user, View("启用回响"), &enabled_int) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.enabled = enabled_int != 0;
        }
        ui->separator(ui->user);
        text("模式");
        if (ui->button(ui->user, View("A 精确回响（HP + 官方伤害数字）"), 0.0F, 0.0F) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.mode = 0;
            context.shared.targets.clear();
            SetStatus(context.shared, "已切换 A 模式；等待新的有效伤害");
        }
        if (ui->button(ui->user, View("B 原生 DoT（游戏 GE 公式）"), 0.0F, 0.0F) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.mode = 1;
            context.shared.targets.clear();
            SetStatus(context.shared, "已切换 B 模式；伤害量由游戏 DoT GE 决定");
        }
        if (ui->button(ui->user, View("C Session 重放（捕获攻击）"), 0.0F, 0.0F) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.mode = 2;
            context.shared.targets.clear();
            SetStatus(context.shared, "已切换 C 模式；只重放捕获到的玩家攻击 session");
        }
        if (mode == 1) {
            char heading[96]{};
            std::snprintf(heading, sizeof(heading), "通用 DoT（已加载 %zu 项）",
                dot_names.size());
            text(heading);
            for (std::size_t index = 0; index < dot_names.size() && index < 32U; ++index) {
                const std::string label = (index == dot_index ? "[当前] " : "选择 ") +
                    dot_names[index];
                if (ui->button(ui->user, View(label.c_str()), 0.0F, 0.0F) != 0) {
                    std::scoped_lock lock(context.shared.mutex);
                    context.shared.pending_dot = static_cast<int>(index);
                }
            }
            if (dot_names.empty()) text("尚未读取到通用 DoT；请进入世界后重试。");
        }
        ui->separator(ui->user);
        float rate_value = static_cast<float>(rate);
        if (ui->slider_float(ui->user, View("每秒次数"), &rate_value, 1.0F,
                static_cast<float>(kMaxTicksPerSecond)) != 0) {
            const auto next = static_cast<std::uint32_t>(
                std::clamp(std::lround(rate_value), 1L,
                    static_cast<long>(kMaxTicksPerSecond)));
            std::scoped_lock lock(context.shared.mutex);
            context.shared.ticks_per_second = next;
        }
        char line[160]{};
        std::snprintf(line, sizeof(line), "当前模式：%s",
            mode == 0 ? "A 精确回响" : mode == 1 ? "B 原生 DoT" : "C Session 重放");
        text(line);
        std::snprintf(line, sizeof(line), "活跃目标：%zu", targets);
        text(line);
        std::snprintf(line, sizeof(line), "服务：combat=%s / damage-echo=%s",
            context.combat != nullptr ? "可用" : "不可用",
            context.echo != nullptr ? "可用" : "不可用");
        text(line);
        if (context.build != nullptr) {
            const auto combat_feature = context.build->feature_state(
                context.build->user, View("nte.combat"));
            const auto echo_feature = context.build->feature_state(
                context.build->user, View("nte.damage-echo"));
            std::snprintf(line, sizeof(line), "Feature：combat=%s / damage-echo=%s",
                combat_feature == ANOMALY_FEATURE_V1_AVAILABLE ? "可用" : "不可用",
                echo_feature == ANOMALY_FEATURE_V1_AVAILABLE ? "可用" : "不可用");
            text(line);
        }
        text(status.c_str());
        if (mode == 1) {
            text("B 模式的跳伤由游戏原生 DoT GE 决定，速率只控制维护/重挂频率。");
        }
        if (ui->button(ui->user, View("清空目标"), 0.0F, 0.0F) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.targets.clear();
            SetStatus(context.shared, "已清空目标");
        }
    }
    ui->end_window(ui->user);
}

AnomalyStatusV1 ANOMALY_CALL Load(const AnomalyHostApiV1* host, void** plugin_context) {
    if (host == nullptr || plugin_context == nullptr ||
        host->struct_size < sizeof(AnomalyHostApiV1)) {
        return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    }
    auto* context = new (std::nothrow) PluginContext();
    if (context == nullptr) return {ANOMALY_STATUS_V1_FAILED, 0, {}};
    context->host = host;
    context->ui = QueryService<AnomalyUiServiceV1>(host, ANOMALY_UI_SERVICE_V1_ID);
    if (context->ui == nullptr) {
        delete context;
        return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, {}};
    }
    context->combat = QueryService<AnomalyNteCombatServiceV1>(
        host, ANOMALY_NTE_COMBAT_SERVICE_V1_ID);
    context->echo = QueryService<AnomalyNteDamageEchoServiceV1>(
        host, ANOMALY_NTE_DAMAGE_ECHO_SERVICE_V1_ID);
    *plugin_context = context;
    return {ANOMALY_STATUS_V1_OK, 0, {}};
}

AnomalyStatusV1 ANOMALY_CALL Start(void* plugin_context) {
    auto* context = static_cast<PluginContext*>(plugin_context);
    if (context == nullptr) return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    {
        std::scoped_lock lock(context->shared.mutex);
        context->shared.targets.clear();
        context->shared.cursor = 0;
        context->shared.last_world_generation = 0;
        context->shared.last_player_id = 0;
        context->shared.last_player_generation = 0;
        context->shared.enabled = true;
        SetStatus(context->shared, "等待玩家对怪物的有效伤害");
    }
    RefreshServices(*context);
    return {ANOMALY_STATUS_V1_OK, 0, {}};
}

AnomalyStatusV1 ANOMALY_CALL Stop(void* plugin_context, std::uint32_t) {
    auto* context = static_cast<PluginContext*>(plugin_context);
    if (context == nullptr) return {ANOMALY_STATUS_V1_OK, 0, {}};
    std::scoped_lock lock(context->shared.mutex);
    context->shared.targets.clear();
    return {ANOMALY_STATUS_V1_OK, 0, {}};
}

void ANOMALY_CALL Unload(void* plugin_context) {
    auto* context = static_cast<PluginContext*>(plugin_context);
    delete context;
}

void ANOMALY_CALL UpdateCallback(void* plugin_context, double delta_seconds) {
    auto* context = static_cast<PluginContext*>(plugin_context);
    if (context != nullptr) Update(*context, delta_seconds);
}

void ANOMALY_CALL DrawCallback(void* plugin_context, const AnomalyUiServiceV1* ui) {
    auto* context = static_cast<PluginContext*>(plugin_context);
    if (context != nullptr) Draw(*context, ui);
}

}  // namespace

ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(
    AnomalyPluginDescriptorV1* descriptor) {
    if (descriptor == nullptr || descriptor->struct_size < sizeof(*descriptor)) {
        return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    }
    *descriptor = {
        sizeof(*descriptor),
        ANOMALY_PLUGIN_API_V1_MAJOR,
        ANOMALY_PLUGIN_API_V1_MINOR,
        {kPluginId, std::strlen(kPluginId)},
        {kPluginName, std::strlen(kPluginName)},
        {kPluginAuthor, std::strlen(kPluginAuthor)},
        {kPluginVersion, std::strlen(kPluginVersion)},
        Load,
        Start,
        Stop,
        Unload,
        UpdateCallback,
        DrawCallback};
    return {ANOMALY_STATUS_V1_OK, 0, {}};
}
