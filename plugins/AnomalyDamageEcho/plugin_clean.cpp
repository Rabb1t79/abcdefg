#include "anomaly/sdk/anomaly_sdk.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct CharacterName {
    const char* token;
    const char* display;
};

constexpr CharacterName kCharacterNames[] = {
    {"Female051", "异能者·零"},
    {"Shinku", "真红"},
    {"Nanally", "娜娜莉"},
    {"Lacrimosa", "安魂曲"},
    {"Hathor", "哈索尔"},
    {"Zankou", "残虹"},
    {"Akane", "明音凛"},
    {"Cang", "白藏"},
    {"Kuhara", "九原"},
    {"Sagiri", "早雾"},
    {"Rouge", "胭脂"},
    {"Oneiroi", "伊洛伊"},
    {"Fadia", "法帝娅"},
};

std::vector<std::string> AllCharacterTokens() {
    std::vector<std::string> result;
    result.reserve(std::size(kCharacterNames));
    for (const auto& entry : kCharacterNames) result.emplace_back(entry.token);
    return result;
}

const char* CharacterDisplay(const std::string& token) {
    for (const auto& entry : kCharacterNames) {
        if (token == entry.token) return entry.display;
    }
    return nullptr;
}

std::string CharacterToken(const std::string& display) {
    for (const auto& entry : kCharacterNames) {
        if (display == entry.display || display == entry.token) return entry.token;
    }
    return {};
}

void Diag(const char* text) {
    std::ofstream file("I:\\Anomalymain\\.local\\damage-echo-diag.txt", std::ios::app);
    if (file) file << (text != nullptr ? text : "") << "\n";
}

constexpr std::uint32_t kMaxTicksPerSecond = 1000;
constexpr std::size_t kMaxTargets = 64;
constexpr std::size_t kMaxEventsPerUpdate = 128;
constexpr std::size_t kMaxApplicationsPerUpdate = 8;
constexpr std::uint64_t kInitialEventBacklog = 16;
// How often an already-started native DoT is refreshed. Each refresh extends the
// duration and adds a stack, so this also controls how fast the damage grows.
constexpr double kNativeDotMaintenanceSeconds = 2.0;

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
    double native_check_accumulator{};
};

struct SharedState {
    std::mutex mutex;
    bool enabled{true};
    std::uint32_t mode{1};  // 0 = exact HP + damage text, 1 = native DoT GE; B is the default.
    std::uint32_t ticks_per_second{4};
    std::uint32_t dot_index{};
    std::vector<std::string> dot_names;
    std::vector<std::uint32_t> dot_indices;
    std::vector<std::string> squad_names;
    std::string squad_filter;
    bool dot_refreshed{};
    bool combat_ok{};
    bool echo_ok{};
    bool cold_start{};
    std::int32_t last_event_kind{-1};
    std::uint64_t last_event_sequence{};
    int pending_dot{-1};
    std::unordered_map<std::uint64_t, EchoTarget> targets;
    // Victims this build has already echoed to. Used to keep our own applications
    // from being captured as a fresh hit.
    std::unordered_set<std::uint64_t> echoed_victims;
    std::string status{"等待 Game 域伤害事件"};
    std::uint64_t cursor{};
    std::uint64_t last_world_generation{};
    std::uint64_t last_player_id{};
    std::uint64_t last_player_generation{};
    std::uint32_t debug_applies{};
};

struct PluginContext {
    const AnomalyHostApiV1* host{};
    const AnomalyUiServiceV1* ui{};
    const AnomalyNteCombatServiceV1* combat{};
    const AnomalyNteDamageEchoServiceV1* echo{};
    const AnomalyUe5BuildServiceV1* build{};
    const AnomalyCoreServiceV1* core{};
    const AnomalyNteEntitiesServiceV1* entities{};
    SharedState shared;
    double squad_refresh{};
    double service_retry{};
    double heartbeat{};
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
    if (shared.echoed_victims.contains(event.target.id)) return;
    if (shared.targets.size() >= kMaxTargets &&
        !shared.targets.contains(event.target.id)) {
        SetStatus(shared, "目标数量达到上限，忽略新的回响");
        return;
    }
    const auto [iterator, inserted] = shared.targets.try_emplace(event.target.id);
    if (!inserted) return;
    {
        static int logged_targets = 0;
        if (logged_targets < 12) {
            char line[160]{};
            std::snprintf(line, sizeof(line), "target add victim=%llu damage=%lld",
                static_cast<unsigned long long>(event.target.id),
                static_cast<long long>(damage));
            Diag(line);
            ++logged_targets;
        }
    }
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
                // B applies once, but the target can die or despawn later. Poll the host
                // on a fixed cadence: each poll refreshes the effect, so the cadence also
                // sets how fast the effect grows.
                target.native_check_accumulator += (std::max)(delta_seconds, 0.0);
                if (target.native_check_accumulator < kNativeDotMaintenanceSeconds) {
                    target.accumulator = 0.0;
                    break;
                }
                target.native_check_accumulator = 0.0;
                target.accumulator = interval;
            }

            AnomalyNteDamageEchoRequestV1 request{};
            FillRequest(target, request);
            request.ticks_per_second = shared.ticks_per_second;
            AnomalyNteDamageEchoResultV1 result{};
            result.struct_size = sizeof(result);
            const auto status = context.echo->apply_native_dot(
                context.echo->user, &request, &result);
            if (shared.debug_applies < 16U) {
                char line[320]{};
                std::snprintf(line, sizeof(line),
                    "apply mode=%u victim=%llu code=%u msg=%.*s hp=%.1f/%.1f flags=0x%X",
                    static_cast<unsigned>(shared.mode),
                    static_cast<unsigned long long>(target.victim.id), status.code,
                    static_cast<int>(status.message.size),
                    status.message.size != 0 && status.message.data != nullptr
                        ? status.message.data : "",
                    result.hp_before, result.hp_after,
                    static_cast<unsigned>(result.flags));
                Diag(line);
                if (context.core != nullptr) {
                    context.core->log(context.core->user, ANOMALY_CORE_LOG_LEVEL_V1_INFO,
                        View(line));
                }
                ++shared.debug_applies;
            }
            if (status.code == ANOMALY_STATUS_V1_OK) {
                target.failures = 0;
                target.native_started = true;
                if (shared.echoed_victims.size() >= 4096U) shared.echoed_victims.clear();
                shared.echoed_victims.insert(target.victim.id);
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

            constexpr std::uint32_t failure_limit = 60U;
            if (++target.failures >= failure_limit) {
                remove.push_back(target.victim.id);
                if (status.message.size != 0 && status.message.data != nullptr) {
                    shared.status.assign(status.message.data, status.message.size);
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
    if (context.core == nullptr) {
        context.core = QueryService<AnomalyCoreServiceV1>(
            context.host, ANOMALY_CORE_SERVICE_V1_ID);
    }
    if (context.entities == nullptr) {
        context.entities = QueryService<AnomalyNteEntitiesServiceV1>(
            context.host, ANOMALY_NTE_ENTITIES_SERVICE_V1_ID);
    }
}

void DrainEvents(PluginContext& context, const AnomalyNteCombatantSnapshotV1& player) {
    SharedState& shared = context.shared;
    for (std::size_t count{}; count < kMaxEventsPerUpdate; ++count) {
        AnomalyNteCombatEventV1 event{};
        event.struct_size = sizeof(event);
        const auto status = context.combat->next_event(
            context.combat->user, shared.cursor, &event);
        if (status.code == ANOMALY_STATUS_V1_NOT_FOUND) {
            // An expired cursor is reported as NOT_FOUND. Without resyncing, the
            // plugin would keep asking for a sequence the host no longer accepts and
            // never consume another event.
            const auto latest = context.combat->latest_event_sequence(context.combat->user);
            std::uint64_t resync = latest > kInitialEventBacklog
                ? latest - kInitialEventBacklog : 0;
            if (resync == shared.cursor) resync = 0;
            {
                static int logged_resync = 0;
                if (logged_resync < 8) {
                    char line[160]{};
                    std::snprintf(line, sizeof(line),
                        "cursor resync old=%llu latest=%llu new=%llu",
                        static_cast<unsigned long long>(shared.cursor),
                        static_cast<unsigned long long>(latest),
                        static_cast<unsigned long long>(resync));
                    Diag(line);
                    ++logged_resync;
                }
            }
            shared.cursor = resync;
            break;
        }
        if (status.code != ANOMALY_STATUS_V1_OK) {
            static int logged_failures = 0;
            if (status.code != ANOMALY_STATUS_V1_NOT_FOUND && logged_failures < 6) {
                char line[160]{};
                std::snprintf(line, sizeof(line), "next_event code=%u msg=%.*s",
                    status.code,
                    static_cast<int>(status.message.size),
                    status.message.size != 0 && status.message.data != nullptr
                        ? status.message.data : "");
                Diag(line);
                ++logged_failures;
            }
            break;
        }
        if (event.sequence <= shared.cursor) break;
        shared.cursor = event.sequence;
        shared.last_event_kind = static_cast<std::int32_t>(event.kind);
        shared.last_event_sequence = event.sequence;
        {
            static int logged_events = 0;
            if (logged_events < 12) {
                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "event kind=%u match=%d source=%llu target=%llu value=%lld",
                    static_cast<unsigned>(event.kind),
                    IsPlayer(player, event.source) ? 1 : 0,
                    static_cast<unsigned long long>(event.source.id),
                    static_cast<unsigned long long>(event.target.id),
                    static_cast<long long>(event.final_value));
                Diag(line);
                ++logged_events;
            }
        }
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

void RefreshSquad(PluginContext& context, double delta_seconds) {
    context.squad_refresh += (std::max)(delta_seconds, 0.0);
    if (context.squad_refresh < 2.0) return;
    context.squad_refresh = 0.0;
    if (context.entities == nullptr || context.entities->frame == nullptr ||
        context.entities->snapshot_at == nullptr ||
        context.entities->class_name_utf8 == nullptr) {
        return;
    }
    AnomalyNteEntityFrameV1 frame{};
    frame.struct_size = sizeof(frame);
    if (context.entities->frame(context.entities->user, &frame).code !=
        ANOMALY_STATUS_V1_OK) {
        return;
    }
    std::vector<std::string> names;
    const std::uint32_t maximum = (std::min)(frame.entity_count, 512U);
    for (std::uint32_t index = 0; index < maximum; ++index) {
        AnomalyNteEntitySnapshotV1 snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (context.entities->snapshot_at(
                context.entities->user, frame.generation, index, &snapshot).code !=
            ANOMALY_STATUS_V1_OK) {
            continue;
        }
        std::array<char, 256> buffer{};
        std::size_t size = buffer.size();
        if (context.entities->class_name_utf8(
                context.entities->user, snapshot.class_id, buffer.data(), &size).code !=
            ANOMALY_STATUS_V1_OK) {
            continue;
        }
        std::string class_name(buffer.data());
        std::string lower = class_name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.rfind("aplayer_", 0) != 0) continue;
        const auto last = lower.rfind('_');
        if (last == std::string::npos || last == 0) continue;
        const auto previous = lower.rfind('_', last - 1);
        if (previous == std::string::npos) continue;
        const std::string token = lower.substr(previous + 1, last - previous - 1);
        std::string display;
        if (token == "nanally") display = "Nanally";
        else if (token == "shinku") display = "Shinku";
        else if (token == "lacrimosa") display = "Lacrimosa";
        else if (token == "zankou") display = "Zankou";
        else if (token == "female") display = "Female051";
        else {
            display = token;
            if (!display.empty()) {
                display[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(display[0])));
            }
        }
        if (display.empty()) continue;
        if (std::find(names.begin(), names.end(), display) == names.end()) {
            names.push_back(std::move(display));
        }
        if (names.size() >= 4U) break;
    }
    if (names.size() < 4U) {
        static constexpr const char* kAllCharacters[] = {
            "Female051", "Shinku", "Nanally", "Lacrimosa", "Hathor",
            "Zankou", "Akane", "Cang", "Kuhara", "Sagiri", "Rouge",
            "Oneiroi", "Fadia"};
        for (const char* character : kAllCharacters) {
            if (std::find(names.begin(), names.end(), character) == names.end()) {
                names.emplace_back(character);
            }
        }
    }
    if (context.combat != nullptr &&
        context.combat->participant_display_name_utf8 != nullptr) {
        AnomalyNteCombatantSnapshotV1 player{};
        player.struct_size = sizeof(player);
        if (context.combat->current_combatant(context.combat->user, &player).code ==
                ANOMALY_STATUS_V1_OK &&
            (player.flags & ANOMALY_NTE_COMBATANT_V1_VALID) != 0) {
            std::array<char, 128> buffer{};
            std::size_t size = buffer.size();
            if (context.combat->participant_display_name_utf8(
                    context.combat->user, player.character, buffer.data(), &size).code ==
                ANOMALY_STATUS_V1_OK) {
                std::string display(buffer.data());
                std::string token = CharacterToken(display);
                if (token.empty()) token = display;
                if (!token.empty() &&
                    std::find(names.begin(), names.end(), token) == names.end()) {
                    names.insert(names.begin(), std::move(token));
                }
            }
        }
    }
    if (names.empty()) return;
    {
        std::string line = "squad:";
        for (const auto& name : names) { line += " "; line += name; }
        Diag(line.c_str());
    }
    std::scoped_lock lock(context.shared.mutex);
    context.shared.squad_names = std::move(names);
}

void RefreshDotChoices(PluginContext& context) {
    if (context.echo == nullptr || context.echo->dot_count == nullptr ||
        context.echo->dot_name_at == nullptr) return;
    {
        std::scoped_lock lock(context.shared.mutex);
        if (!context.shared.dot_names.empty() || context.shared.dot_refreshed) return;
    }
    const auto count = context.echo->dot_count(context.echo->user);
    std::string filter;
    {
        std::scoped_lock lock(context.shared.mutex);
        filter = context.shared.squad_filter;
    }
    std::string filter_lower = filter;
    const bool generic_filter = filter == "__generic__";
    if (generic_filter) filter_lower.clear();
    for (char& c : filter_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::string> names;
    std::vector<std::uint32_t> host_indices;
    bool added_skill_ge = false;
    for (std::uint32_t index = 0; index < count && index < 64U; ++index) {
        std::array<char, 256> buffer{};
        std::size_t size = buffer.size();
        const auto name_status = context.echo->dot_name_at(
            context.echo->user, index, buffer.data(), &size);
        if (name_status.code != ANOMALY_STATUS_V1_OK) continue;
        std::string name(buffer.data());
        if (name.empty()) continue;
        std::string name_lower = name;
        for (char& c : name_lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (generic_filter) {
            bool character_specific = false;
            for (const auto& entry : kCharacterNames) {
                std::string token = entry.token;
                for (char& c : token) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (name_lower.find(token) != std::string::npos) {
                    character_specific = true;
                    break;
                }
            }
            if (character_specific) continue;
        } else if (!filter_lower.empty() &&
                   name_lower.find(filter_lower) == std::string::npos) {
            continue;
        }
        const bool skill_ge = name.rfind("GE_Player_", 0) == 0 &&
            name.ends_with("_Damage_C");
        if (skill_ge) {
            if (added_skill_ge) continue;
            added_skill_ge = true;
        } else if (name.rfind("Buff_", 0) != 0) {
            continue;
        }
        names.push_back(std::move(name));
        host_indices.push_back(index);
    }
    std::scoped_lock lock(context.shared.mutex);
    context.shared.dot_names = std::move(names);
    context.shared.dot_indices = std::move(host_indices);
    context.shared.dot_refreshed = true;
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
        std::uint32_t host_index = 0;
        bool has_host_index = false;
        {
            std::scoped_lock lock(context.shared.mutex);
            if (pending >= 0 &&
                static_cast<std::size_t>(pending) < context.shared.dot_indices.size()) {
                host_index = context.shared.dot_indices[static_cast<std::size_t>(pending)];
                has_host_index = true;
            }
        }
        if (has_host_index && context.echo != nullptr &&
            context.echo->select_dot != nullptr) {
            if (context.echo->select_dot(context.echo->user, host_index).code ==
                ANOMALY_STATUS_V1_OK) {
                std::scoped_lock lock(context.shared.mutex);
                context.shared.dot_index = static_cast<std::uint32_t>(pending);
            }
        }
    }
    }
    {
        std::scoped_lock lock(context.shared.mutex);
        context.shared.combat_ok = context.combat != nullptr;
        context.shared.echo_ok = context.echo != nullptr;
    }
    if (context.combat == nullptr || context.echo == nullptr) {
        static bool logged_services = false;
        if (!logged_services) {
            char line[96]{};
            std::snprintf(line, sizeof(line), "services combat=%d echo=%d",
                context.combat != nullptr ? 1 : 0,
                context.echo != nullptr ? 1 : 0);
            Diag(line);
            logged_services = true;
        }
        return;
    }

    AnomalyNteCombatantSnapshotV1 player{};
    player.struct_size = sizeof(player);
    const auto player_status =
        context.combat->current_combatant(context.combat->user, &player);
    if (player_status.code != ANOMALY_STATUS_V1_OK ||
        (player.flags & ANOMALY_NTE_COMBATANT_V1_VALID) == 0) {
        static bool logged_invalid = false;
        if (!logged_invalid) {
            char line[160]{};
            std::snprintf(line, sizeof(line),
                "player invalid code=%u flags=0x%X id=%llu",
                player_status.code, player.flags,
                static_cast<unsigned long long>(player.character.id));
            Diag(line);
            logged_invalid = true;
        }
        return;
    }
    static std::uint32_t logged_mode = 99U;
    if (logged_mode != context.shared.mode) {
        char line[96]{};
        std::snprintf(line, sizeof(line), "mode=%u player=%llu/%llu",
            context.shared.mode,
            static_cast<unsigned long long>(player.character.id),
            static_cast<unsigned long long>(player.character.generation));
        Diag(line);
        logged_mode = context.shared.mode;
    }

    std::scoped_lock lock(context.shared.mutex);
    if (!context.shared.enabled) return;
    if (context.shared.cold_start) {
        // Cold start: replay the same bounded recent window used after a hot
        // reload so a hit that landed during world loading is not lost.
        const auto latest = context.combat->latest_event_sequence(context.combat->user);
        context.shared.cursor = latest > kInitialEventBacklog
            ? latest - kInitialEventBacklog : 0;
        context.shared.cold_start = false;
        context.shared.last_player_id = player.character.id;
        context.shared.last_player_generation = player.character.generation;
        context.shared.targets.clear();
        SetStatus(context.shared, "冷启动初始化完成");
    }
    if (context.shared.cursor == 0) {
        const auto latest = context.combat->latest_event_sequence(context.combat->user);
        // Consume the most recent event on the first service-visible tick. The
        // initial attack can otherwise arrive on the same tick this cursor was
        // initialized and be skipped forever.
        context.shared.cursor = latest > kInitialEventBacklog
            ? latest - kInitialEventBacklog : 0;
    }
    if (context.shared.last_player_id != player.character.id ||
        context.shared.last_player_generation != player.character.generation) {
        context.shared.last_player_id = player.character.id;
        context.shared.last_player_generation = player.character.generation;
        context.shared.targets.clear();
        context.shared.echoed_victims.clear();
        {
            static int logged_clears = 0;
            if (logged_clears < 4) {
                char line[128]{};
                std::snprintf(line, sizeof(line), "target clear player=%llu/%llu",
                    static_cast<unsigned long long>(player.character.id),
                    static_cast<unsigned long long>(player.character.generation));
                Diag(line);
                ++logged_clears;
            }
        }
    }
    context.heartbeat += (std::max)(delta_seconds, 0.0);
    if (context.heartbeat >= 2.0) {
        context.heartbeat = 0.0;
        const auto latest = context.combat->latest_event_sequence(context.combat->user);
        char line[220]{};
        std::snprintf(line, sizeof(line),
            "hb mode=%u cursor=%llu latest=%llu targets=%zu enabled=%d",
            static_cast<unsigned>(context.shared.mode),
            static_cast<unsigned long long>(context.shared.cursor),
            static_cast<unsigned long long>(latest),
            context.shared.targets.size(),
            context.shared.enabled ? 1 : 0);
        Diag(line);
    }
    DrainEvents(context, player);
    if (context.shared.targets.empty()) {
        SetStatus(context.shared, "等待玩家对怪物的有效伤害");
        return;
    }
    ApplyTargets(context, delta_seconds);
    char status[160]{};
    std::snprintf(status, sizeof(status), "目标 %zu / 速率 %u 次每秒",
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
    bool combat_ok{};
    bool echo_ok{};
    bool cold_start{};
    std::int32_t last_event_kind{-1};
    std::uint64_t last_event_sequence{};
    std::vector<std::string> dot_names;
    std::vector<std::string> squad_names;
    std::string squad_filter;
    std::uint32_t dot_index{};
    {
        std::scoped_lock lock(context.shared.mutex);
        enabled = context.shared.enabled;
        mode = context.shared.mode;
        rate = context.shared.ticks_per_second;
        targets = context.shared.targets.size();
        status = context.shared.status;
        combat_ok = context.shared.combat_ok;
        echo_ok = context.shared.echo_ok;
        last_event_kind = context.shared.last_event_kind;
        last_event_sequence = context.shared.last_event_sequence;
        dot_names = context.shared.dot_names;
        squad_names = context.shared.squad_names;
        squad_filter = context.shared.squad_filter;
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
        text("模式：B 原生 DoT");

        const auto select_category = [&](const std::string& filter) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.squad_filter = filter;
            context.shared.dot_names.clear();
            context.shared.dot_indices.clear();
            context.shared.dot_refreshed = false;
            context.shared.dot_index = 0;
        };
        text("角色分类");
        if (ui->button(ui->user, View("全部"), 0.0F, 0.0F) != 0) select_category({});
        if (ui->same_line != nullptr) ui->same_line(ui->user, 0.0F, 0.0F);
        if (ui->button(ui->user, View("通用"), 0.0F, 0.0F) != 0) select_category("__generic__");
        for (const auto& name : squad_names) {
            if (ui->same_line != nullptr) ui->same_line(ui->user, 0.0F, 0.0F);
            const char* localized = CharacterDisplay(name);
            const std::string label = (squad_filter == name ? "[当前] " : "") +
                (localized != nullptr ? localized : name);
            if (ui->button(ui->user, View(label.c_str()), 0.0F, 0.0F) != 0) {
                select_category(name);
            }
        }
        if (ui->button(ui->user, View("刷新列表"), 0.0F, 0.0F) != 0) {
            std::scoped_lock lock(context.shared.mutex);
            context.shared.dot_names.clear();
            context.shared.dot_indices.clear();
            context.shared.dot_refreshed = false;
            context.shared.dot_index = 0;
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
        std::uint32_t rate_value = rate;
        if (ui->input_uint32 != nullptr &&
            ui->input_uint32(ui->user, View("每秒次数"), &rate_value, 1U, 50U) != 0) {
            const auto next = std::clamp(rate_value, 1U, kMaxTicksPerSecond);
            std::scoped_lock lock(context.shared.mutex);
            context.shared.ticks_per_second = next;
        }


        char line[192]{};
        std::snprintf(line, sizeof(line), "活跃目标：%zu", targets);
        text(line);
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
        context->shared.squad_names = AllCharacterTokens();
        context->shared.dot_refreshed = false;
        SetStatus(context->shared, "等待玩家对怪物的有效伤害");
    }
    RefreshServices(*context);
    bool ready_at_start{};
    if (context->combat != nullptr && context->echo != nullptr &&
        context->combat->current_combatant != nullptr) {
        AnomalyNteCombatantSnapshotV1 player{};
        player.struct_size = sizeof(player);
        ready_at_start =
            context->combat->current_combatant(context->combat->user, &player).code ==
                ANOMALY_STATUS_V1_OK &&
            (player.flags & ANOMALY_NTE_COMBATANT_V1_VALID) != 0;
    }
    {
        std::scoped_lock lock(context->shared.mutex);
        context->shared.cold_start = !ready_at_start;
    }
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
