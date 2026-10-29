#include <windows.h>
#include <intrin.h>
#include "anomaly/sdk/services/ui.h"
#include "anomaly/sdk/services/core.h"
#include "anomaly/sdk/services/nte.h"
#include "anomaly/sdk/services/ui_resources.h"
#include "anomaly/sdk/services/interop.h"
#include "anomaly/sdk/services/ue5.h"
#include "ue_reflection.hpp"
#include "anomaly/sdk/services/ue5.h"
#include "ue_reflection.hpp"
// AnomalyUltimate 闂?婵炴挻纰嶇换鍐ㄧ暤娓氣偓瀵敻顢楁笟鍥т还闂備緡鍋勫Λ妤呭箖濠婂懏瀚氬ù锝囨嚀缁侊箓姊洪幓鎺斝㈢紓鍌涙崌閺佸秹宕煎鍛喒濠电偛鐭佸▔鏇㈠极閵堝绫嶇憸宥夋偪閸曨垱鍋濋悽顖ｅ枤缁€?
// 闁荤姴娲ｉ悞锕€鐣垫笟鈧鍫曞Ψ閿斿墽鍩嶉梺鎸庣⊕閻♀暜e/闂佸搫鐗冮崑鎾剁磽娴ｅ摜澧﹂柣娑欑懇瀵劑鎳滈棃娴舵捇鏌?md闂侀潧妫旈懓绺璭/闂佹悶鍎茬粙鎴炴叏濞戙垺鍤勯柤鎭掑劤閻栭亶鏌ｉ缁樼グ缂佺粯娲熷畷?md
//  - 闂佺顕х换妤呭醇椤忓棛绠旈柨鏇楀亾闁烩姍鍛當闁挎棁鍎婚々顐︽煥濞戞瑨澹橀悽顖ｅ亝閹?DLL 闁哄鏅滈崝姗€銆侀幋锕€瀚夐柣鏃囧吹濞堝爼鏌?AnomalyPluginEntryV1 闂佹眹鍔岀€氼垳鎹㈤幋锕€鐐婇柣鎰ゴ閸嬫挾娑垫搴ｎ槱RVA 0x495F0/0x49588/0x49608/0x49610闂?
//  - 闂佸搫鐗嗙粔瀛樻叏閻旂厧钃熼柕澶樼厛閸ゅ嫬顪冮妶鍛儓缂侇煈鍙冮弫宥咁潰绾惧€€_trace 闂佺鍩栭幐鎼佸春瀹€鍕剭?on_load 闁瑰吋娼欑换鎰板垂?
//  - 闂佸憡姊婚崰鏇㈠礂?id/闂佺粯顨呴悧濠傦耿娴煎瓨鏅慨姗嗗墮閺傃冣攽椤旂⒈鍎愮€殿喗瀵у濠氬箥椤曞懎鍓ㄦ繝闈涱樈閸嬪嫰宕㈤弽顓熸櫖闁革富婀滱 0x22E30/0x29C50/0x32380/0x3C0A0闂?
//  - 闂備焦婢樼粔鍫曟偪?schema / 闂備焦顑欓崳顖滄?rdata 闂佸搫瀚€涒晠寮?+ 闂佸憡顭囩划顖滄嫻閳轰脊搴ㄥ幢濡ゅ啯鎲荤紓浣诡殣缂傛氨鎲?
#include "anomaly/sdk/plugin.h"
#include "anomaly/sdk/services/core.h"
#include "anomaly/sdk/services/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <chrono>

namespace {

constexpr const char* kId = "anomaly.local.kx.exp";
constexpr const char* kName = "AnomalyKX Exp";
constexpr const char* kAuthor = "Anomaly";
constexpr const char* kVersion = "1.0.0";

struct FeatureDesc {
    const char* id;
    const char* name;
    const char* version;
    const char* schema_id;
    const char* schema_json;
    void (*on_load)(const AnomalyHostApiV1*, void**);
    void (*on_start)(void*);
    void (*on_stop)(void*);
    void (*on_unload)(void*);
};

AnomalyStatusV1 ok() { AnomalyStatusV1 s{}; s.code = ANOMALY_STATUS_V1_OK; return s; }
AnomalyStatusV1 fail(uint32_t code, const char* msg) {
    AnomalyStatusV1 s{}; s.code = code; s.message.data = msg;
    s.message.size = msg ? std::strlen(msg) : 0; return s;
}

const void* Query(const AnomalyHostApiV1* host, const char* id, uint32_t min_version) {
    if (!host || !host->query_service) return nullptr;
    const void* service = nullptr;
    AnomalyStringViewV1 view{id, std::strlen(id)};
    if (host->query_service(host->host_context, view, min_version, &service).code !=
        ANOMALY_STATUS_V1_OK)
        return nullptr;
    return service;
}

// 闂備焦婢樼粔鍫曟偪?schema闂佹寧绋戦悧濠偽ｉ幋锕€妫橀柛銉ｅ妽闂勫秹鏌?.rdata闂?x4C660 / 0x4D200 / 0x4D6D0 / 0x4F2C8闂?
constexpr const char* kChronosSchema =
    R"({"type":"object","additionalProperties":false,"required":["durationSeconds"],)"
    R"("properties":{"durationSeconds":{"type":"integer","minimum":1},)"
    R"("timedPauseHotkeyVirtualKey":{"type":"integer"},"timedPauseHotkeyModifiers":{"type":"integer"},)"
    R"("infinitePauseToggleHotkeyVirtualKey":{"type":"integer"},)"
    R"("infinitePauseToggleHotkeyModifiers":{"type":"integer"}}})";
constexpr const char* kDamageSchema =
    R"({"type":"object","additionalProperties":false,"required":["replayCount"],)"
    R"("properties":{"replayCount":{"type":"integer","minimum":1,"maximum":1024},)"
    R"("chainReaction":{"type":"boolean"},"enabled":{"type":"boolean"}}})";
constexpr const char* kVolleyballSchema =
    R"({"type":"object","additionalProperties":false,"properties":)"
    R"({"hotkey":{"type":"string"},"anomaly-ultimate-tabs":{"type":"string"}}})";
constexpr const char* kGmSchema =
    R"({"type":"object","additionalProperties":false,"properties":)"
    R"({"hotkey":{"type":"string"}}})";

// 濠殿噯绲界换瀣煂濠婂牆绀夐柣鏃囶嚙閸樻挳鏌?on_load 婵炴垶鎼╅崢濂稿储濞戙垹鍐€闁跨喓濮峰畷锝呫€掑顓犵畾缂佸倸妫濋弫宥呯暆閳ь剙顔忔潏銊ョ窞閺夊牄鍔嶉ˇ褍霉閻橆喖鍔氶柣銏╁亰瀹曘垽鎮㈤柨瀣槶闂?UNAVAILABLE 闂傚倸瀚粔鑸殿殽閸ヮ剚鏅柛顐ｇ矌閻熸繈鏌ｅ顓犵煁缂佷礁顕槐鎺楀箣濠靛牊姣庨梺?
struct Ctx {
    const AnomalyHostApiV1* host;
    const void* core;
    const void* config;
    const void* signature;
    const void* ue5_framework;
    const void* ue5_names;
    const void* ue5_objects;
    const void* ui;
    const void* nte_combat;
    const void* patch;
    const void* process_event;
    const void* hook;
    const void* damage_chain;
    const void* input;
    const void* entities;
    const void* actors;
    const void* functions;
};

Ctx* g_ctx = nullptr;
const void* LazyService(const void** slot, const char* id, uint32_t min_version);
void TryQueryIsGM();
static void PushLog(const char* text);
static int g_chronos_req = 0;            // 2=无限暂停(3600s) 3=恢复；由游戏线程消费
static std::uint64_t NowMs();
void RegisterDamageConfigSchema();
void LoadDamageConfig();
void TickConfigSave();
void DrawCaptureUi(const AnomalyUiServiceV1* ui);
void ReleaseCaptureSubscription();
void ReleaseExecThunk();
void ANOMALY_CALL OnProcessEvent(void*, uintptr_t, uintptr_t, void*);
static int g_pe_subscribed = 0;
static int g_pe_tries = 0;
static unsigned long long g_pe_total = 0;
static int g_legacy_capture = 0;   // OnDamaged/SendEvent/ApplyGE 涓夊鍘嗗彶鎹曡幏锛岄粯璁ゅ叧
static int g_cap_on = 0;           // 鎹曡幏鎬诲紑鍏筹細榛樿鍏筹紝鍕鹃€夊悗鎵嶈闃?鏅煡/姝﹁
void PeRingPush(std::uintptr_t function, std::uintptr_t object);
void LogHitTarget();
static constexpr int kPeelMax = 8;
static unsigned long long g_peel_calls[kPeelMax] = {};
static int g_peel_logged[kPeelMax] = {};
static char g_peel_name[kPeelMax][64] = {};
static unsigned char g_ecs_raw[0x38];
static std::uintptr_t g_ecs_self = 0;
static std::uintptr_t g_ecs_ufn = 0;
static int g_ecs_fresh = 0;
static unsigned long long g_ecs_calls = 0;
void RequestGeScan();
void ReplayTick();
void ReplayApplyStep();
void ReplayArm(std::uintptr_t asc, const char* name, std::uintptr_t ge_class, float hp,
               std::uint64_t victim_id = 0, float ref_dmg = 0.0f);
void AutoTargetTick();
void MonTrackTick();
static constexpr int kMonList = 16;
static std::uintptr_t g_mon_list[kMonList] = {};
static char g_mon_list_name[kMonList][48] = {};
static float g_mon_list_hp[kMonList] = {};
static int g_mon_list_n = 0;
static std::uintptr_t LastDamageGeClass(std::uintptr_t asc);
static void ObjectNameUtf8(std::uintptr_t object, char* out, std::size_t cap);
std::uintptr_t PlayerAscForApply();
void MonTrackTick();
void StopHeartbeat();
static bool AscLooksLikeAsc(std::uintptr_t asc);
static std::uintptr_t FindAscInActor(std::uintptr_t act);
static std::uintptr_t g_gec_ufn = 0;
static std::uintptr_t g_gea_ufn = 0;
static int g_ge_apply_rc = 0;
static int g_ge_shots = 0;         // shots owed for this turn (shot clock, max 4 per frame)
static int g_ge_req = 0;           // 1 = 绛変笅涓€鍙戝湪 Game 绾跨▼鎵ц锛坥n_draw 涓嶆槸 Game 绾跨▼锛?
static int g_ge_player_n = 0;
static int g_ge_sel2 = 0;
static int g_ge_scan_on = 0;
static int g_ge_pass = 0;              // 0 = 鎸夎鑹茶繃婊わ紱1 = 鏃犺繃婊ゅ厹搴曢噸鎵?
static int g_ge_seen = 0;
static int g_ge_named = 0;
static int g_ge_prefix = 0;
static int g_ge_damage = 0;
static int g_ge_token_hit = 0;
static char g_ge_token[32] = {};
static char g_ge_player_name[24][64] = {};
struct ReplayTarget {
    std::uintptr_t asc;
    std::uint64_t victim_id;   // 最近一次真实命中它的 victim 句柄（O(1) 自喂判定）
    char name[48];
    std::uintptr_t ge_class;
    float hp;
    std::uint64_t last_hit_ms;
    int blocked;
    int paused;
    int applied;
    int landed;
    float ref_dmg;      // 最近一次真实命中的 final_damage
    double dmg_sum;     // 已落地伤害累计（估算每次 apply 的伤害）
    int applied_sum;    // 与 dmg_sum 对应的 apply 次数
};
static constexpr int kMaxReplayTargets = 8;
static ReplayTarget g_rt[kMaxReplayTargets] = {};
static int g_rt_n = 0;
static std::uint64_t g_shot_clock_ms = 0;   // 上次拍发时刻（steady_clock 毫秒）
static double g_apply_budget = 0.0;         // 严格速率累积器：不足一发的部分留到下帧
static std::uint64_t g_apply_clock_ms = 0;
static int g_rr_target = 0;                 // 多目标轮转：每次 apply 换一个目标
static int g_ge_probe = 0;             // 1 = 閫愪釜璇?GE锛堟壘鑳界湡鐨勬帀琛€鐨勯偅涓級
static int g_ge_probe_idx = 0;
static int g_ge_probe_prev = -1;       // 涓婁竴鍙戠敤鐨?GE 涓嬫爣锛堝紓姝ュ埌璐﹁璁板埌瀹冨ご涓婏級
static int g_ge_probe_flush = 0;       // 1 = 鏀跺熬琛ヤ竴鎷嶏紝鍙 HP 褰掑洜鏈€鍚庝竴鍙?
static int g_ge_probe_hits[24] = {};
static AnomalyGenerationHandleV1 g_pe_cap_handle{};
static int g_patch_rc = -1;
static int g_hook_ok = 0;
static int g_dmg_chain_ok = 0;
static int g_query_rc = -1;
static int g_hook2_ok = 0;
int GmToggleOn();
static int g_sig_ok = 0;
static int g_load_count = 0;
static unsigned long long g_probe = 0;
static int g_find_rc = -1;
static unsigned int g_id_pc = 0, g_id_asc = 0, g_id_vb = 0;
static unsigned int g_id_ai = 0, g_id_mon = 0;   // /Script/HTGame.HTAICharacter / HTMonsterCharacter
static unsigned long long g_cls_pc = 0;
static unsigned long long g_cls_ai = 0, g_cls_mon = 0;
static unsigned long long g_cls_asc = 0;
static unsigned long long g_cls_vb = 0;
static unsigned int g_probe_nid = 0;
static unsigned int g_want_id = 0;
static int g_draw_count = 0;
static int g_names_ok = 0;
void TickFeatures();
bool InitUe(const void* signature_service, const void* names_service);

void TickCombatEvents();
void ReleaseHooks();
void TickSweep();
void ActivateGA();
void TickWorldScan();
void RequestWorldScan();
void InstallProbeHooks();
void ProbeStat();
void DiagThunkScan();
void InstallSgeNativeHook();
void ChainToPawn();
void LevelToActors();
void TickAscScan();
void TickReverseScan();
void CycleAscMode();
void TickRepeat();
void RequestAscScan();
void ApplyGEDirect();
void DumpTokkyDetourDirect();
void ReleaseEverything();
void UnwatchProcessEvent();
void TryInvokeApplyGE();   // Game 缂備焦宕樺▔鏇㈠煝婵傜鏄ラ柣鏃€妞块崵銏犆圭粭鍝勨偓鏇㈩敊婵犲啰顩查悗锝傛櫆椤愪粙鏌ㄥ☉妯煎閻㈩垯鍗抽弻锕傚醇濠靛牏锛涢梺鍝勫暢椤旀劗妲?

void ChronosLoad(const AnomalyHostApiV1*, void** out) {
    if (!g_ctx || !g_ctx->signature) { *out = nullptr; return; }
    *out = g_ctx;  // 闂佺儵鏅╅崰妤呮偉閿濆洦鍠嗛柨婵嗘閳ь兛绮欓弫宥夊捶椤ь溁sedGameExcludePlayerByTimer闂佹寧绋戦ˇ闈涖€掗懜鍨氦闁归偊鍨奸弨浠嬫煛閸愩劌顣虫い鎺斿厴瀹曘儱顓艰箛鏇狀槷闁?manifest
}
void DamageLoad(const AnomalyHostApiV1*, void** out) {
    if (!g_ctx || !g_ctx->nte_combat) { *out = nullptr; return; }
    *out = g_ctx;
}
void VolleyLoad(const AnomalyHostApiV1*, void** out) {
    if (!g_ctx || !g_ctx->ue5_framework) { *out = nullptr; return; }
    *out = g_ctx;
}
void GmLoad(const AnomalyHostApiV1*, void** out) {
    if (!g_ctx || !g_ctx->signature) { *out = nullptr; return; }
    *out = g_ctx;
}
void Noop(void*) {}

AnomalyStatusV1 ANOMALY_CALL on_load(const AnomalyHostApiV1* host, void** out) {
    ++g_load_count;
    if (!host || !out) return fail(ANOMALY_STATUS_V1_INVALID_ARGUMENT, "host is invalid");
    *out = nullptr;

    auto* ctx = new Ctx{};
    ctx->host = host;
    // 闂佸憡顭囬崰搴綖?on_load 闂佹眹鍔岀€氼厼锕㈤崶顒€绀夐柍鈺佸暟閸欌偓闁荤姴娲㈤崹浠嬪Υ鎼淬垺鍎熼煫鍥ュ劤缁€鍓唀t_trace 闁诲骸婀遍崑鐐电矈閹绢喗鏅?
    ctx->config       = Query(host, "anomaly.config", 1);
    ctx->core         = Query(host, "anomaly.core", 1);
    ctx->signature    = Query(host, "anomaly.interop.signature", 1);
    ctx->ue5_framework= Query(host, "anomaly.ue5.framework", 1);
    ctx->ue5_names    = Query(host, "anomaly.ue5.names", 2);
    ctx->ue5_objects  = Query(host, "anomaly.ue5.objects", 1);
    ctx->ui           = Query(host, "anomaly.ui", 1);
    ctx->nte_combat   = Query(host, "anomaly.nte.combat", 1);
    ctx->patch        = Query(host, "anomaly.interop.patch", 1);
    ctx->process_event= Query(host, "anomaly.ue5.process-event", 1);
    ctx->entities     = Query(host, "anomaly.nte.entities", 1);
    ctx->actors       = Query(host, "anomaly.nte.actors", 1);
    g_ctx = ctx;
    ctx->input        = Query(host, "anomaly.input", 1);
    InitUe(ctx->signature, ctx->ue5_names);

    if (!ctx->config) { delete ctx; g_ctx = nullptr;
        return fail(ANOMALY_STATUS_V1_UNAVAILABLE, "config service is unavailable"); }

    RegisterDamageConfigSchema();
    LoadDamageConfig();

    void* unused = nullptr;
    ChronosLoad(host, &unused);
    DamageLoad(host, &unused);
    VolleyLoad(host, &unused);
    GmLoad(host, &unused);

    *out = ctx;
    return ok();
}

AnomalyStatusV1 ANOMALY_CALL on_start(void* ctx) { return ctx ? ok() : fail(ANOMALY_STATUS_V1_INVALID_ARGUMENT, "context is invalid"); }
AnomalyStatusV1 ANOMALY_CALL on_stop(void* ctx, uint32_t) { if (!ctx) return ok(); return ok(); }
void ANOMALY_CALL on_unload(void* ctx) {
    if (ctx == nullptr) return;
    StopHeartbeat();
    ReleaseCaptureSubscription();
    ReleaseExecThunk();
    ReleaseHooks();
    if (ctx == g_ctx) g_ctx = nullptr;
    delete static_cast<Ctx*>(ctx);
}
void ANOMALY_CALL on_update(void*, double) {
    static int n = 0;
    // 娉ㄦ剰锛?*涓嶈鍦ㄨ繖閲岃皟 ProcessEvent**銆傚疄娴?`upd: gameThread=1`锛堟槸 Game 绾跨▼锛夛紝
    // 浣嗕竴鏃﹀湪杩欓噷鐪熸鍙戣捣 ProcessEvent 鏂藉姞锛屾暣涓繘绋嬬珛鍒诲崱姝伙紙UI 绾跨▼鐨?
    // on_draw 蹇冭烦涔熷仠锛夈€傛柦鍔犲彧鍦?ue5.process-event 鐨勮闃呭洖璋冨煙閲屽仛锛圙eApplyStep锛夈€?
    TickCombatEvents();                 // 婵炲濮撮幊搴★耿?Game 缂備焦宕樺▔鏇㈠煝婵傜鏄ラ柣鏃€妞块崵銏犆圭粭鍝勨偓鏇㈩敊婵犲啰顩查悗锝傛櫆椤愪粙鏌ㄥ☉妯煎閻㈩垯鍗抽弻锕傚醇濠靛牏锛涢梺鍝勫暢椤旀劗妲?
    TickSweep();
    TickAscScan();
    TickReverseScan();
    TickWorldScan();
    TickRepeat();
    TickConfigSave();
    if ((++n % 30) == 0) TickFeatures();
}
// 缂備焦鍔栭〃鍛般亹濞戞瑧鈻旈幖娣灩娴犳ê霉閻樻彃顣冲┑顔芥倐楠炩偓濞撴艾锕︾粈鍕煕濞嗘ê鐏￠悗鍨⒐缁嬪﹪宕ㄩ鐑嗗殭闂?anomaly.config闂?
struct UiState {
    int chronos = 0;
    int damage = 0;
    int chain = 0;
    int volley = 0;
    int gm = 0;
    float replay = 100.0f; // 每秒回放次数（间隔 = 1000/replay）；100 = 10ms 一发，每帧最多补 4 发
};
UiState g_ui_state;
int GmToggleOn() { return g_ui_state.gm; }
bool g_ui_dirty = false;

constexpr const char* kWindowTitle = "AnomalyUltimate##";
constexpr const char* kDamageSchemaId = "anomaly.ultimate.damage-replay.settings";

// 勾选状态/次数持久化：借 anomaly.config 的 schema 文档（宿主持久化）。
static AnomalyGenerationHandleV1 g_damage_schema{};
static int g_damage_schema_ok = 0;
static int g_cfg_seen_damage = -1;
static int g_cfg_seen_chain = -1;
static int g_cfg_seen_replay = -1;
static std::uint64_t g_cfg_save_due_ms = 0;

static const AnomalyConfigServiceV1* DamageConfigService() {
    if (g_ctx == nullptr || g_ctx->config == nullptr) return nullptr;
    return static_cast<const AnomalyConfigServiceV1*>(g_ctx->config);
}

static bool ConfigFieldAvailable(const AnomalyConfigServiceV1* cfg, std::size_t end) {
    return cfg != nullptr && static_cast<std::size_t>(cfg->struct_size) >= end;
}

static int JsonIntField(const char* doc, const char* key, int fallback) {
    const char* p = std::strstr(doc, key);
    if (p == nullptr) return fallback;
    p += std::strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':') ++p;
    bool negative = false;
    if (*p == '-') { negative = true; ++p; }
    if (*p < '0' || *p > '9') return fallback;
    int value = 0;
    while (*p >= '0' && *p <= '9') {
        if (value < 100000) value = value * 10 + (*p - '0');
        ++p;
    }
    return negative ? -value : value;
}

static bool JsonBoolField(const char* doc, const char* key, bool fallback) {
    const char* p = std::strstr(doc, key);
    if (p == nullptr) return fallback;
    p += std::strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':') ++p;
    if (std::strncmp(p, "true", 4) == 0) return true;
    if (std::strncmp(p, "false", 5) == 0) return false;
    return fallback;
}

void RegisterDamageConfigSchema() {
    const auto* cfg = DamageConfigService();
    if (cfg == nullptr) return;
    if (!ConfigFieldAvailable(
            cfg, offsetof(AnomalyConfigServiceV1, register_schema) +
                     sizeof(cfg->register_schema)) ||
        cfg->register_schema == nullptr) {
        PushLog("cfg: schema registration unavailable");
        return;
    }
    AnomalyStringViewV1 id{kDamageSchemaId, std::strlen(kDamageSchemaId)};
    AnomalyByteSpanV1 schema{reinterpret_cast<const std::uint8_t*>(kDamageSchema),
                             std::strlen(kDamageSchema)};
    g_damage_schema_ok =
        cfg->register_schema(cfg->user, id, 1, schema, &g_damage_schema).code ==
        ANOMALY_STATUS_V1_OK;
    PushLog(g_damage_schema_ok ? "cfg: schema registered" : "cfg: schema registration failed");
}

void LoadDamageConfig() {
    if (!g_damage_schema_ok) return;
    const auto* cfg = DamageConfigService();
    if (cfg == nullptr ||
        !ConfigFieldAvailable(
            cfg, offsetof(AnomalyConfigServiceV1, read) + sizeof(cfg->read)) ||
        cfg->read == nullptr) {
        return;
    }
    char buffer[256];
    std::size_t size = sizeof(buffer) - 1;
    std::uint32_t version = 0;
    AnomalyMutableByteSpanV1 destination{reinterpret_cast<std::uint8_t*>(buffer),
                                         sizeof(buffer) - 1};
    AnomalyStringViewV1 id{kDamageSchemaId, std::strlen(kDamageSchemaId)};
    if (cfg->read(cfg->user, id, &version, destination, &size).code != ANOMALY_STATUS_V1_OK ||
        size >= sizeof(buffer)) {
        return;
    }
    buffer[size] = 0;
    const int replay =
        JsonIntField(buffer, "\"replayCount\"", static_cast<int>(g_ui_state.replay));
    if (replay >= 1 && replay <= 1024) g_ui_state.replay = static_cast<float>(replay);
    g_ui_state.chain = JsonBoolField(buffer, "\"chainReaction\"", g_ui_state.chain != 0) ? 1 : 0;
    g_ui_state.damage = JsonBoolField(buffer, "\"enabled\"", g_ui_state.damage != 0) ? 1 : 0;
    char b[96];
    std::snprintf(b, sizeof(b), "cfg: loaded enabled=%d chain=%d replay=%d",
                  g_ui_state.damage, g_ui_state.chain, static_cast<int>(g_ui_state.replay));
    PushLog(b);
}

void SaveDamageConfig() {
    const auto* cfg = DamageConfigService();
    if (!g_damage_schema_ok || cfg == nullptr ||
        !ConfigFieldAvailable(
            cfg, offsetof(AnomalyConfigServiceV1, write_atomic) + sizeof(cfg->write_atomic)) ||
        cfg->write_atomic == nullptr) {
        return;
    }
    char doc[192];
    const int n = std::snprintf(
        doc, sizeof(doc), "{\"replayCount\":%d,\"chainReaction\":%s,\"enabled\":%s}",
        static_cast<int>(g_ui_state.replay),
        g_ui_state.chain ? "true" : "false",
        g_ui_state.damage ? "true" : "false");
    if (n <= 0) return;
    AnomalyStringViewV1 id{kDamageSchemaId, std::strlen(kDamageSchemaId)};
    AnomalyByteSpanV1 span{reinterpret_cast<const std::uint8_t*>(doc),
                           static_cast<std::size_t>(n)};
    const AnomalyStatusV1 status = cfg->write_atomic(cfg->user, id, 1, span);
    static int logged_failure = 0;
    if (status.code != ANOMALY_STATUS_V1_OK) {
        if (logged_failure == 0) {
            logged_failure = 1;
            char b[96];
            std::snprintf(b, sizeof(b), "cfg: save failed rc=%u",
                          static_cast<unsigned>(status.code));
            PushLog(b);
        }
        return;
    }
    g_ui_dirty = false;
}

// 勾选/次数变化后 400ms 落盘（on_draw 只置时间戳，真正的写盘放在 on_update）。
void TickConfigSave() {
    if (g_cfg_save_due_ms == 0 || NowMs() < g_cfg_save_due_ms) return;
    g_cfg_save_due_ms = 0;
    SaveDamageConfig();
    g_cfg_seen_damage = g_ui_state.damage;
    g_cfg_seen_chain = g_ui_state.chain;
    g_cfg_seen_replay = static_cast<int>(g_ui_state.replay);
}


static ue::Reader g_ue;
int g_sig_svc = 0;
int g_sig_rc = -1;
static unsigned long long g_gworld_instr = 0;
static unsigned long long g_pe_addr = 0;
static unsigned long long g_gworld_ptr = 0;
static unsigned long long g_gworld_addr = 0;
unsigned long long g_sig_addr = 0;
void TickFeatures();

// 濠殿噯绲界换鎰板箮濮樿鲸鍠嗛柨婵嗘閳ь兛绮欏畷璺衡槈濡ゅ懎娈濋梺鍛婃⒒閸犳洟宕楀鈧幆鍐礋椤撶喓鐛ラ梺?UClass/UFunction闂佹寧绋戦悧蹇曞垝閵娾晛鍑犳繝濠傚暙閸愨偓闂佸搫瀚慨鎾春?UI 闂佺粯顭堥崺鏍焵椤戞寧绁版い銏″灴閺佸秵寰勯崼姘壕?
// 闁荤喐鐟辩徊楣冩倵閼恒儲濯撮悹鎭掑妽閺?GObjects 闁荤姳绀佸鈥澄涢崼鏇為棷妞ゎ厽甯炵粈鍕磼濞戞瑥鍔ら柟顔芥礋閺? anomaly.ue5.names 闂?name_id 濠殿噯绲鹃弻銊╊敋椤曗偓婵?
struct ResolveState {
    int    tried = 0;
    int    sig_ok = 0;
    int    names_ok = 0;
    int    chronos = 0;
    int    damage = 0;
    int    volley = 0;
    int    gm = 0;
};
ResolveState g_resolve;


// 闂佹椿娼块崝宀勵敊閺嶃劎鈻?objects 闂佸搫鐗嗙粔瀛樻叏閻旂厧绠板璺侯煬閸ょ娀鎮归悘鎭掑€楅悢鍛偓鍨緲鐎氼剝銇?name_id闂佹寧绋戦悧鍛箔婢跺鐟规繝闈涳功椤?find_utf8闂?
unsigned int FindNameIdByPath(const char* path) {
    if (!g_ctx || !g_ctx->ue5_objects || !path) return 0;
    const auto* objs = static_cast<const AnomalyUe5ObjectsServiceV1*>(g_ctx->ue5_objects);
    if (!objs->find_exact || !objs->snapshot_by_handle) return 0;
    // BetterPose 闂佸憡鑹鹃張顒勵敄濞嗘挻鏅慨姗嗗墮鐢儵鏌″蹇曠瘈闁绘稒鐟х槐鎺楀箻鐎电硶鍋撻娑欏闁规儳鐏堥埀顒侇焽閳ь剙婀遍崑娑氣偓鍨耿瀹曘儵顢曢妷褏顔愭繛瀛樼矋缁嬫垿鎮鸿閳绘挾鍠婃径宀€顦╅梻渚囧墮閻忔繈宕㈤妶澶婄睄鐟滃繑鏅跺Δ鍛珘鐎广儱鎳庨～銈囩磽娴ｈ灏伴柣搴灣閹肩偓绻濋崟顒佺彧闂?
    if (objs->struct_size <
        offsetof(AnomalyUe5ObjectsServiceV1, find_exact) + sizeof(objs->find_exact)) return 0;
    if (objs->struct_size <
        offsetof(AnomalyUe5ObjectsServiceV1, snapshot_by_handle) +
            sizeof(objs->snapshot_by_handle)) return 0;
    AnomalyGenerationHandleV1 handle{};
    AnomalyStringViewV1 view{path, std::strlen(path)};
    g_find_rc = (int)objs->find_exact(objs->user, view, &handle).code;
    if (g_find_rc != ANOMALY_STATUS_V1_OK) return 0;
    AnomalyUe5ObjectSnapshotV1 snap{};
    snap.struct_size = sizeof(snap);
    g_find_rc = (int)objs->snapshot_by_handle(objs->user, handle, &snap).code;
    if (g_find_rc != ANOMALY_STATUS_V1_OK) return 0;
    return snap.name_id;
}


// 闂?names 闂佸搫鐗嗙粔瀛樻叏閻斿吋鍎?resolve_utf8 闂?FName id 闁荤喐鐟辩徊楣冩倵娴犲绠ｉ柟閭﹀幗閻庮噣鏌￠崼顐㈠⒕缂佽鲸绻堥幃浠嬪Ω閵堝洩澹橀梺鍝勭Ф椤牓顢氶鍓х＝闁哄稁鍋嗛崹?
void DecodeName(unsigned int id, char* out, unsigned long long cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!g_ctx || !g_ctx->ue5_names) return;
    const auto* n = static_cast<const AnomalyUe5NamesServiceV1*>(g_ctx->ue5_names);
    if (!n->resolve_utf8) return;
    std::size_t size = static_cast<std::size_t>(cap);
    n->resolve_utf8(n->user, id, out, &size);
    if (size < cap) out[size] = 0;
}


// 闂備緡鍓欑粔鏉戭啅?UClass 闂?Children闂佹寧绋戝﹢绂╰ruct+0x48 / UField+0x28闂佹寧绋戦¨鈧紒杈ㄧ箞閹?resolve_utf8 闁荤喐鐟辩徊鍧楁偉濠婂牆瑙︾€广儱鎳愰幗鐔告叏閿濆棙鐓ユい鏇ㄥ櫍閺?
// 婵炴垶鎸哥粔宕囨妞嬪孩灏?find_utf8闂佹寧绋戦悧鎰邦敋濮樿泛绠抽柕澶堝劚缂嶆捇鏌涢敂鍝勫婵犫偓閹殿喒鍋撻悷鑸粧閻庨潧鏈粙澶嬬節娴ｈ櫣鎲归梺鍛婄懐閸ㄦ娊寮妶澶嬫櫖濠㈣泛鐗冮崑?
void* FindFuncByName(void* uclass, const char* fname) {
    if (!uclass || !fname) return nullptr;
    std::uintptr_t child = 0;
    if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(uclass) + 0x48, &child)) return nullptr;
    char buf[128];
    for (int guard = 0; guard < 8192 && child != 0; ++guard) {
        unsigned int id = 0;
        if (ue::Reader::SafeU32(child + 0x18, &id)) {
            DecodeName(id, buf, sizeof(buf));
            if (buf[0] != 0 && std::strcmp(buf, fname) == 0) return reinterpret_cast<void*>(child);
        }
        std::uintptr_t next = 0;
        if (!ue::Reader::SafePtr(child + 0x28, &next)) return nullptr;
        child = next;
    }
    return nullptr;
}


// 闂?GObjects 婵炴垶鎼╅崢鐣屸偓?Class 闂佸湱顭堝ú顓㈠箯閿熺姴绠ラ柟顖嗗懏婢栨繛鎾磋壘椤戞垹妲愬▎鎴炲磯閻庡湱濮风粻?CDO "Default__*"闂?
using PE_FN = void (*)(void*, void*, void*);
PE_FN ReadProcessEvent(void* object);

void* FindInstanceOfClass(void* cls) {
    if (!cls) return nullptr;
    char buf[128];
    const unsigned int limit = g_ue.object_limit < 400000u ? g_ue.object_limit : 400000u;
    for (unsigned int i = 0; i < limit; ++i) {
        void* obj = g_ue.ObjectAt(i);
        if (!obj) continue;
        std::uintptr_t c = 0;
        if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(obj) + 0x10, &c)) continue;
        if (reinterpret_cast<void*>(c) != cls) continue;
        unsigned int id = 0;
        if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &id)) {
            DecodeName(id, buf, sizeof(buf));
            if (std::strncmp(buf, "Default__", 9) == 0) continue;
        }
        return obj;
    }
    return nullptr;
}

void* g_pc_inst = nullptr;
void* g_isgm_fn = nullptr;
int   g_isgm_result = -1;
int   g_isgm_called = 0;

void TryQueryIsGM() {
    if (g_isgm_called || !g_pc_inst || !g_isgm_fn) return;
    if (!g_ctx || !g_ctx->ue5_framework) return;
    const auto* fw = static_cast<const AnomalyUe5FrameworkServiceV1*>(g_ctx->ue5_framework);
    if (!fw->is_game_thread || !fw->is_game_thread(fw->user)) return;
    g_isgm_called = 1;
    struct { unsigned char ret; } params{0};
    auto process = g_pe_addr != 0
        ? reinterpret_cast<PE_FN>(g_pe_addr)
        : ReadProcessEvent(g_pc_inst);
    if (process) {
        process(g_pc_inst, g_isgm_fn, &params);
        g_isgm_result = params.ret ? 1 : 0;
    }
}

static int g_scan_budget = 0;

// 閻庢鍠楀ú婊堝极閵堝鐓ｉ柟鎯ф噽缁€鍕煕鐎ｎ亞绠虫禍娑㈡煛婢跺牆鍔ラ柛?5.6.1/CppSDK/SDK/Engine_classes.hpp闂佹寧绋戦¨鈧紒?
//   UWorld::OwningGameInstance 0x230
//   UGameInstance::LocalPlayers 0x38 (TArray: Data@0x38, Num@0x40)
//   UPlayer::PlayerController 0x30
void* PlayerControllerFromWorld() {
    if (g_gworld_ptr == 0) return nullptr;
    __try {
        void* instance = *reinterpret_cast<void**>(static_cast<std::uintptr_t>(g_gworld_ptr) + 0x230);
        if (instance == nullptr) return nullptr;
        void** data = *reinterpret_cast<void***>(reinterpret_cast<std::uintptr_t>(instance) + 0x38);
        const int num = *reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(instance) + 0x40);
        if (data == nullptr || num <= 0) return nullptr;
        void* local_player = data[0];
        if (local_player == nullptr) return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(local_player) + 0x30);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}


// Chronos闂佹寧绋掗惌顔剧箔濮椻偓閹墽浠﹂悙顒夋Н闂傚倸鍊瑰娆撳焵椤掆偓缁夎泛顔忔總鍛婃櫖闁割偅绻€閻掑﹦绱掓径宀€甯涙繛鑲╂暬閹?5.6.1/CppSDK/SDK/Engine_classes.hpp闂?
//   UWorld::PersistentLevel 0x30 闂?ULevel::WorldSettings 0x2B0 闂?HTWorldSettings +0x5E0 (float)
constexpr std::uintptr_t kPersistentLevel = 0x30;
constexpr std::uintptr_t kWorldSettings = 0x2B0;
constexpr std::uintptr_t kWorldDilation = 0x5E0;

float* WorldDilationSlot() {
    if (g_gworld_ptr == 0) return nullptr;
    __try {
        void* level = *reinterpret_cast<void**>(static_cast<std::uintptr_t>(g_gworld_ptr) + kPersistentLevel);
        if (level == nullptr) return nullptr;
        void* settings = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(level) + kWorldSettings);
        if (settings == nullptr) return nullptr;
        return reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(settings) + kWorldDilation);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void ProbeHook() {
    static int done = 0;
    if (done || !g_ctx) return;
    done = 1;
    const void* hook = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    g_hook_ok = hook ? 1 : 0;
    const void* dmg = LazyService(&g_ctx->damage_chain, "anomaly.nte.damage-chain", 1);
    g_dmg_chain_ok = dmg ? 1 : 0;
}

void EnsureSubscription() {
    if (g_pe_subscribed || !g_ctx) return;
    const void* pe_service = LazyService(&g_ctx->process_event, "anomaly.ue5.process-event", 1);
    if (!pe_service) return;
    const auto* pe = static_cast<const AnomalyUe5ProcessEventServiceV1*>(pe_service);
    if (!pe->subscribe) return;
    AnomalyGenerationHandleV1 h{};
    const AnomalyStatusV1 st = pe->subscribe(pe->user, OnProcessEvent, nullptr, &h);
    if (st.code == ANOMALY_STATUS_V1_OK) {
        g_pe_subscribed = 1;
        g_pe_cap_handle = h;
        PushLog("cap: process-event subscribed");
    } else if (++g_pe_tries <= 1) {
        char b[64];
        std::snprintf(b, sizeof(b), "cap: subscribe rc=%u", static_cast<unsigned>(st.code));
        PushLog(b);
    }
}

// 瀹夸富绔偣閲岃繖鏉¤闃呭繀椤绘樉寮忔挙閿€锛氭彃浠堕暅鍍忕儹閲嶈浇浼氬嵏杞芥湰 DLL锛岀暀鐫€浼氳瀹夸富鍥炶皟鍒?
// 宸插嵏杞界殑浠ｇ爜銆?
void ReleaseCaptureSubscription() {
    if (g_pe_subscribed == 0 || g_ctx == nullptr) return;
    g_pe_subscribed = 0;
    const void* svc = g_ctx->process_event;
    if (svc != nullptr) {
        const auto* pe = static_cast<const AnomalyUe5ProcessEventServiceV1*>(svc);
        if (pe->unsubscribe) {
            const AnomalyStatusV1 st = pe->unsubscribe(pe->user, g_pe_cap_handle);
            char b[80];
            std::snprintf(b, sizeof(b), "cap: unsubscribe rc=%u",
                          static_cast<unsigned>(st.code));
            PushLog(b);
        }
    }
    g_pe_cap_handle = {};
}

void TickChronos() {
    // 原生时停：UI 勾选 = 无限暂停开/关。真正的 PausedGameExcludePlayerByTimer
    // 调用放在游戏线程（OnProcessEvent）执行，渲染线程只投递请求。
    static int last = -1;
    if (last != g_ui_state.chronos) {
        last = g_ui_state.chronos;
        g_chronos_req = g_ui_state.chronos ? 2 : 3;
        char b[64];
        std::snprintf(b, sizeof(b), "chronos: request %d", g_chronos_req);
        PushLog(b);
    }
}


// GM闂佹寧绋掔喊宥嗕繆?IsGM 闂佹眹鍔岀€氼剛鏁锔藉仺?ExecFunction闂佹寧绋戝﹢绂檜nction::ExecFunction 0xD8闂佹寧绋戦ˇ浼村极婵傜绠?"mov al,1; ret"
int g_gm_patched = 0;
AnomalyGenerationHandleV1 g_gm_handle{};

// on_load 闂傚倸鍟抽崺鏍敊瀹€鍕珘鐎广儱鎳庨～銈夋煕濞嗘ê鐏ラ柛蹇旑焽娴滅顫濋鐔风处濠电偛顦崝宀勫船娴犲鏅€光偓閸愵亞顔愰梻浣瑰絻閺堫剛鈧灚绮庨弫顔款槼闁稿骸銈搁獮鈧憸鎴ｆ＂闂?
const void* LazyService(const void** slot, const char* id, uint32_t min_version) {
    if (*slot != nullptr) return *slot;
    if (!g_ctx || !g_ctx->host || !g_ctx->host->query_service) return nullptr;
    const void* service = nullptr;
    AnomalyStringViewV1 view{id, std::strlen(id)};
    g_query_rc = static_cast<int>(
        g_ctx->host->query_service(g_ctx->host->host_context, view, min_version, &service).code);
    if (g_query_rc == 0) *slot = service;
    return *slot;
}


// GM 闂佸憡绋掗崹婵嬪箮閵堝懌鈧帡宕ㄩ鐐殿槹闂佹寧绋掗鐚榯ch 闂佺懓鐡ㄩ崝鏇熸叏濞戙垹瑙﹂幖鎼灣缁€澶娒?HTCheatManager 缂備緡鍋夊畷闈涒枔?Children 闂備礁澧介崑鎾绘倶閸ャ劎鈻?UFunction 闂?
static char g_gm_cmd_names[32][64];
static int  g_gm_cmd_count = 0;
static int  g_gm_selected = -1;
static unsigned int g_gm_cls_id = 0;
static unsigned long long g_gm_cls_ptr = 0;
static unsigned long long g_gm_first_child = 0;
static unsigned long long g_gm_cheat = 0;

// 闂佸憡甯掑Λ妤呭箮濮樿泛绠ユい鎰剁到娴煎酣鏌ㄥ☉娆戭暡閻庡灚绮庨悮鍓ф嫚閸欏鈧娊鎮峰▎娆戞偧闁绘牗绮撳畷鐘恒亹閹烘垵璧?"HTCheatManager"闂佹寧绋戞總鏃傜箔婢跺鐟规繝闈涳功椤╊偄霉閻樹警鍞虹紓宥呯Ф缁辨帡骞樼€电硶鍋撻娑欏闁规儳鍘栭悞濠勭磼?
static unsigned int g_scan_cursor = 0;
static int g_scan_done = 0;

void CollectCommandsFromClass(void* cls) {
    char name[64];
    std::uintptr_t child = 0;
    if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(cls) + 0x48, &child)) return;
    g_gm_first_child = static_cast<unsigned long long>(child);
    for (int guard = 0; guard < 4096 && child != 0 && g_gm_cmd_count < 32; ++guard) {
        unsigned int id = 0;
        if (ue::Reader::SafeU32(child + 0x18, &id)) {
            DecodeName(id, name, sizeof(name));
            if (name[0] != 0) {
                std::snprintf(g_gm_cmd_names[g_gm_cmd_count], sizeof(g_gm_cmd_names[0]), "%s", name);
                ++g_gm_cmd_count;
            }
        }
        std::uintptr_t next = 0;
        if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
        child = next;
    }
}


// Chronos闂佹寧绋掗懝楣冨矗韫囨洑娌柍褜鍓熷顕€宕奸弴鐕傜吹闂佽壈缈伴崝蹇涘磿?+ 闂佺粯澹曢弲娑㈩敊瀹ュ洦鍠嗛柟鐑樻礀椤ュ繘鐓崶鈺傛珪闁哄棙鍔欏畷妤呭Ω閵壯勬嫳闂佽壈缈伴崝蹇涘磿?
//   5.6.1-0+UE5-HT dump:
//     AWorldSettings::TimeDilation        0x0400 (Engine_classes.hpp:28060)
//     AController::Pawn                   0x0308 (Engine_classes.hpp:30496)
//     AHTCharacter::IgnoreGlobalTimeDilation 0x0870 (HTGame_classes.hpp:1094)
static int   g_chronos_applied = 0;
static int   g_chronos_rc = 0;
static float g_chronos_saved_dilation = 1.0f;
static unsigned char g_chronos_saved_ignore = 0;
static float g_chronos_readback = -1.0f;
static char g_world_name[48] = {0};
static unsigned long long g_pawn_ptr = 0;
static int g_pawn_rc = 0;
static unsigned long long g_chronos_addr = 0;

bool ReadSpan(const AnomalyCoreServiceV1* core, std::uintptr_t address, void* dest, std::size_t size) {
    AnomalyMutableByteSpanV1 span{static_cast<unsigned char*>(dest), size};
    return core->read_memory(core->user, address, span).code == ANOMALY_STATUS_V1_OK;
}

bool WriteSpan(const AnomalyCoreServiceV1* core, std::uintptr_t address, const void* src, std::size_t size) {
    AnomalyByteSpanV1 span{static_cast<const unsigned char*>(src), size};
    return core->write_memory(core->user, address, span).code == ANOMALY_STATUS_V1_OK;
}

static uint32_t g_hot_vk[3] = {0x70u, 0x71u, 0x72u};
static int g_hot_once = 0;
static int g_hot_hold = 0;
static int g_hot_restore = 0;
static int g_hot_registered = 0;
static AnomalyGenerationHandleV1 g_hot_handle[3] = {};
static int g_hot_capture = -1;   // >=0 = 正在等待按键，值为要改的热键下标
static void* g_pause_fn = nullptr;   // HTPlayerController::PausedGameExcludePlayerByTimer
static void* g_pause_type_fn = nullptr;  // HTPlayerController::PauseGame(EPausedGameType)
static void* g_resume_fn = nullptr;      // HTPlayerController::ResumeGame(EPausedGameType)
static void* g_ps_pause_fn = nullptr;    // HTPlayerState::PauseGame(EPausedGameType, float)

// ---- 游戏线程调用通道（anomaly.ue5.functions） ----
// 原生时停与排球胜利都只能在该通道里执行：UI/输入回调在渲染线程，直接调 ProcessEvent 会崩。
static int g_volley_rc = 0;
static char g_volley_msg[96] = {0};
static std::uint64_t g_volley_next_ms = 0;

static bool InvokeGameFunction(std::uintptr_t object, std::uintptr_t function,
                               void* parameters, std::size_t size) {
    if (object < 0x10000 || function < 0x10000 || !g_ctx) return false;
    if (!g_ctx->functions) LazyService(&g_ctx->functions, "anomaly.ue5.functions", 1);
    if (!g_ctx->functions) return false;
    const auto* fn = static_cast<const AnomalyUe5FunctionsServiceV1*>(g_ctx->functions);
    if (!fn->invoke) return false;
    return fn->invoke(fn->user, object, function, parameters, size).code ==
           ANOMALY_STATUS_V1_OK;
}

// 直接 ProcessEvent（游戏线程）：functions 服务在部分宿主版本不可用时用它兜底。
// g_pe_addr 未解析到时与伤害回放一样退回 HTGame.exe + 0x3E41360。
static bool CallUFunction(std::uintptr_t object, std::uintptr_t function, void* parameters) {
    if (object < 0x10000 || function < 0x10000) return false;
    std::uintptr_t pe = static_cast<std::uintptr_t>(g_pe_addr);
    if (pe < 0x10000) {
        const std::uintptr_t base =
            reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
        if (base != 0) pe = base + 0x3E41360;
    }
    if (pe < 0x10000) return false;
    reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(object),
                                reinterpret_cast<void*>(function), parameters);
    return true;
}

// 排球：PlayerState -> GetOrCacheVolleyballGameManager -> Test_ForceWin(角色)
static void VolleyWinStep() {
    if (!g_pc_inst) { g_volley_rc = -1; return; }
    std::uintptr_t ps = 0;
    if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(g_pc_inst) + 0x2D0, &ps) ||
        ps < 0x10000) {
        g_volley_rc = -2;
        return;
    }
    std::uintptr_t ps_cls = 0;
    if (!ue::Reader::SafePtr(ps + 0x10, &ps_cls) || ps_cls < 0x10000) { g_volley_rc = -3; return; }
    void* f_mgr = FindFuncByName(reinterpret_cast<void*>(ps_cls),
                                 "GetOrCacheVolleyballGameManager");
    if (!f_mgr) { g_volley_rc = -4; return; }
    struct { std::uintptr_t ret; } mp{0};
    if (!InvokeGameFunction(ps, reinterpret_cast<std::uintptr_t>(f_mgr), &mp, sizeof(mp))) {
        g_volley_rc = -5;
        return;
    }
    std::uintptr_t mgr = mp.ret;
    if (mgr < 0x10000) { g_volley_rc = -6; return; }
    std::uintptr_t mgr_cls = 0;
    if (!ue::Reader::SafePtr(mgr + 0x10, &mgr_cls) || mgr_cls < 0x10000) { g_volley_rc = -7; return; }
    void* f_win = FindFuncByName(reinterpret_cast<void*>(mgr_cls), "Test_ForceWin");
    if (!f_win) { g_volley_rc = -8; return; }
    std::uintptr_t chr = 0;
    ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(g_pc_inst) + 0x308, &chr);
    struct {
        std::uintptr_t chr;
        unsigned char ret;
        unsigned char pad[7];
    } wp{chr, 0, {}};
    const bool ok = InvokeGameFunction(mgr, reinterpret_cast<std::uintptr_t>(f_win), &wp, sizeof(wp));
    g_volley_rc = ok ? (wp.ret ? 2 : 1) : -9;
    std::snprintf(g_volley_msg, sizeof(g_volley_msg), "mgr=0x%llX chr=0x%llX win=%d",
                  (unsigned long long)mgr, (unsigned long long)chr, (int)wp.ret);
}

void TickChronosDump() {
    if (!g_ctx) return;
    const void* core_service = LazyService(&g_ctx->core, "anomaly.core", 1);
    if (!core_service) { g_chronos_rc = -301; return; }
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(core_service);
    if (!core->read_memory || !core->write_memory) { g_chronos_rc = -302; return; }
    if (g_gworld_ptr == 0) { g_chronos_rc = -303; return; }

    {
        unsigned int wid = 0;
        if (ue::Reader::SafeU32(static_cast<std::uintptr_t>(g_gworld_ptr) + 0x18, &wid))
            DecodeName(wid, g_world_name, sizeof(g_world_name));
    }
    std::uintptr_t level = 0;
    if (!ue::Reader::SafePtr(static_cast<std::uintptr_t>(g_gworld_ptr) + 0x30, &level) || level == 0) {
        g_chronos_rc = -304;
        return;
    }
    std::uintptr_t settings = 0;
    if (!ue::Reader::SafePtr(level + 0x2B0, &settings) || settings == 0) { g_chronos_rc = -305; return; }
    const std::uintptr_t dilation_addr = settings + 0x5E0;   // HTWorldSettings 闂佺厧顨庢禍婊堟偩閻愵剛鈻曞璺洪閺嗘瑩鏌ら弬銉ヤ壕闂佹寧绋戝﹢绀絤eAccel 閻庣懓鎲￠悡锟犳偘濞嗘垶瀚氬〒姘功缁€?

    std::uintptr_t pawn = 0;
    if (g_pc_inst != nullptr) {
        ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(g_pc_inst) + 0x308, &pawn);
    }

    g_pawn_ptr = static_cast<unsigned long long>(pawn);
    if (g_hot_hold && !g_ui_state.chronos) g_ui_state.chronos = 1;
    if (g_hot_hold == 0 && g_ui_state.chronos && g_hot_restore == 0 && g_chronos_applied && g_hot_once == 0) {
        /* keep as-is until toggled off by UI */
    }
    if (g_hot_restore) { g_hot_restore = 0; g_ui_state.chronos = 0; g_hot_hold = 0; g_hot_once = 0; }
    if (g_hot_once) { g_ui_state.chronos = 1; g_hot_once = 0; g_hot_hold = 1; }
    if (g_ui_state.chronos && !g_chronos_applied) {
        if (!ReadSpan(core, dilation_addr, &g_chronos_saved_dilation, sizeof(float))) { g_chronos_rc = -306; return; }
        float frozen = 0.0001f;   // 闂佸搫鍟悥鐓幬涢崸妤€纾绘繝濠傚閸撻箖鏌ㄥ☉娆掑鐟滄澘娲ら妴鎺楀川婵犲倻浜為梺鐓庮殠娴滐綁妫呴埡鍛參闁稿繐鎳愮粔鏉戔槈閹炬剚鍎旀俊?
        float clamp_min = 0.0f;
        if (ReadSpan(core, settings + 0x40C, &clamp_min, sizeof(float)) &&
            clamp_min > 0.0f && clamp_min < 1.0f) {
            frozen = clamp_min;
        }
        if (!WriteSpan(core, dilation_addr, &frozen, sizeof(float))) { g_chronos_rc = -307; return; }
        if (pawn != 0) {
            const std::uintptr_t ignore_addr = pawn + 0x870;
            if (ReadSpan(core, ignore_addr, &g_chronos_saved_ignore, 1)) {
                const unsigned char on = 1;
                if (WriteSpan(core, ignore_addr, &on, 1)) g_pawn_rc = 1;
                else g_pawn_rc = -2;
                const float one = 1.0f;
                if (WriteSpan(core, pawn + 0x808, &one, sizeof(float)) && g_pawn_rc == 1)
                    g_pawn_rc = 3;
            } else {
                g_pawn_rc = -1;
            }
        }
        g_chronos_addr = static_cast<unsigned long long>(dilation_addr);
        g_chronos_readback = -1.0f;
        ReadSpan(core, dilation_addr, &g_chronos_readback, sizeof(float));
        g_chronos_applied = 1;
        g_chronos_rc = 0;
    } else if (!g_ui_state.chronos && g_chronos_applied) {
        WriteSpan(core, dilation_addr, &g_chronos_saved_dilation, sizeof(float));
        if (pawn != 0) {
            const std::uintptr_t ignore_addr = pawn + 0x870;
            WriteSpan(core, ignore_addr, &g_chronos_saved_ignore, 1);
        }
        g_chronos_applied = 0;
        g_chronos_rc = 0;
    }
}

void BuildGmCommandList() {
    if (!g_gm_patched || g_gm_cmd_count > 0 || g_scan_done) return;
    if (!g_sig_ok || !g_names_ok) return;

    const unsigned int limit = 1600000u;
    const unsigned int budget = 20000u;
    char class_name[64];
    unsigned int processed = 0;
    while (processed < budget && g_scan_cursor < limit) {
        void* obj = g_ue.ObjectAt(g_scan_cursor);
        ++g_scan_cursor;
        ++processed;
        if (obj == nullptr) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(obj) + 0x10, &cls) || cls == 0) continue;
        unsigned int id = 0;
        if (!ue::Reader::SafeU32(cls + 0x18, &id)) continue;
        DecodeName(id, class_name, sizeof(class_name));
        if (std::strcmp(class_name, "HTCheatManager") == 0) {
            g_gm_cls_ptr = static_cast<unsigned long long>(cls);
            g_gm_cheat = static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(obj));
            CollectCommandsFromClass(reinterpret_cast<void*>(cls));
            g_scan_done = 1;
            return;
        }
    }
    if (g_scan_cursor >= limit) g_scan_done = 1;
}


// 闂佸憡鐟﹂悧妤冪矓閻戣棄绀嬪Δ锝呭暊閸嬫挻寰勭€ｎ偒妲梺閫涘嫎閸婃繈寮ㄩ姀銏″仒闁靛鍊楅鏌ユ煥濞戞鎽渟GM 闂佽鍘归崹褰捤囬弻銉ュ偍闁绘棃顥撻弶浠嬫煟?
void RemoveGmPatch() {
    if (!g_gm_patched) return;
    const void* patch_service = LazyService(&g_ctx->patch, "anomaly.interop.patch", 1);
    if (patch_service) {
        const auto* patcher = static_cast<const AnomalyPatchServiceV1*>(patch_service);
        if (patcher->release) patcher->release(patcher->user, g_gm_handle);
    }
    g_gm_patched = 0;
    g_patch_rc = 0;
}

void ApplyGmPatch() {
    if (g_gm_patched) return;
    if (!g_ctx) { g_patch_rc = -201; return; }

    const void* core_service = LazyService(&g_ctx->core, "anomaly.core", 1);
    if (!core_service) { g_patch_rc = -202; return; }
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(core_service);
    if (!core->read_memory) { g_patch_rc = -203; return; }

    const void* patch_service = LazyService(&g_ctx->patch, "anomaly.interop.patch", 1);
    if (!patch_service) { g_patch_rc = -204; return; }
    const auto* patcher = static_cast<const AnomalyPatchServiceV1*>(patch_service);
    if (!patcher->apply) { g_patch_rc = -205; return; }

    if (!g_isgm_fn && g_cls_pc)
        g_isgm_fn = FindFuncByName(reinterpret_cast<void*>(g_cls_pc), "IsGM");
    if (!g_isgm_fn) { g_patch_rc = -206; return; }

    unsigned char pointer_bytes[8] = {0};
    AnomalyMutableByteSpanV1 read_span{pointer_bytes, 8};
    if (core->read_memory(core->user,
                          reinterpret_cast<std::uintptr_t>(g_isgm_fn) + 0xD8,
                          read_span).code != ANOMALY_STATUS_V1_OK) {
        g_patch_rc = -207;
        return;
    }
    std::uintptr_t exec = 0;
    std::memcpy(&exec, pointer_bytes, 8);
    if (exec == 0) { g_patch_rc = -208; return; }

    unsigned char body[3] = {0xB0, 0x01, 0xC3};   // mov al,1 ; ret
    AnomalyByteSpanV1 body_span{body, 3};
    AnomalyStringViewV1 label{"ultimate.gm.isgm", 16};
    AnomalyGenerationHandleV1 handle{};
    g_patch_rc = static_cast<int>(patcher->apply(patcher->user, exec, body_span, label, &handle).code);
    if (g_patch_rc == 0) { g_gm_patched = 1; g_gm_handle = handle; }
}

// Damage闂佹寧绋掗懝鎯э耿?process-event 闂佹悶鍎抽崑鐘绘儍閻斿吋鏅柛顐ゅ枔閸╂鏌熼弶鎴犵闁搞倕娴风划娆戔偓锝呭缁€鍡涙⒑閹绘帞鐒搁柛锝嗘倐瀵劑骞嗚閸婂崬鈽夐幘顖氫壕闁荤姴顑呴崯浼村极?
void* g_dmg_fn = nullptr;
int   g_replay_budget = 0;
static void PushLog(const char* text);
static void OnDamagedCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);
static void ApplyGeCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);
static void SendEventCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);
static void ApplyTargetCapture(std::uintptr_t function, std::uintptr_t object, void* parameters);

void ANOMALY_CALL OnProcessEvent(void* /*user*/, uintptr_t object, uintptr_t function, void* parameters) {
    ++g_pe_total;
    // 时停请求：只在游戏线程投递暂停函数。
    if (g_chronos_req != 0) {
        const int req = g_chronos_req;
        g_chronos_req = 0;
        // 与角色大招同一套暂停类型：PG_UltraSkill_PlayPassiveEffect(3)。
        // 优先直接 ProcessEvent（游戏线程），失败再走宿主的 functions 服务。
        constexpr unsigned char kPauseType = 3;
        bool ok = false;
        unsigned char ret = 0;
        if (g_pc_inst) {
            const std::uintptr_t fn = (req == 3)
                ? reinterpret_cast<std::uintptr_t>(g_resume_fn)
                : reinterpret_cast<std::uintptr_t>(g_pause_type_fn);
            if (fn >= 0x10000) {
                struct {
                    unsigned char type;
                    unsigned char ret;
                } p{kPauseType, 0};
                ok = CallUFunction(reinterpret_cast<std::uintptr_t>(g_pc_inst), fn, &p);
                ret = p.ret;
                if (!ok) {
                    ok = InvokeGameFunction(reinterpret_cast<std::uintptr_t>(g_pc_inst), fn, &p,
                                            sizeof(p));
                    ret = p.ret;
                }
            }
        }
        g_chronos_rc = ok ? req : -req;
        char b[160];
        std::snprintf(b, sizeof(b), "chronos: req=%d ok=%d ret=%u type=%u pause=0x%llX resume=0x%llX",
                      req, ok ? 1 : 0, static_cast<unsigned>(ret),
                      static_cast<unsigned>(kPauseType),
                      (unsigned long long)g_pause_type_fn, (unsigned long long)g_resume_fn);
        PushLog(b);
    }
    // 排球胜利：勾选后每 500ms 在游戏线程尝试一次。
    if (g_ui_state.volley != 0 && g_pc_inst) {
        const std::uint64_t now = NowMs();
        if (now >= g_volley_next_ms) {
            g_volley_next_ms = now + 500;
            VolleyWinStep();
        }
    }
    // 杩欓噷鏄涓讳繚璇佺殑 Game 绾跨▼鍥炶皟鍩燂細ProcessEvent 鏂藉姞鍙兘鍦ㄨ繖閲屽仛锛堣 GeApplyStep锛夈€?
    if (g_ge_req != 0) ReplayApplyStep();
    if (g_cap_on != 0) PeRingPush(function, object);
    ApplyTargetCapture(function, object, parameters);
    // 涓嬮潰涓夊鍘嗗彶鎹曡幏鍦?UFunction 鏈В鏋愬墠浼氬姣忎釜浜嬩欢璋冪敤涓€娆?DecodeName锛堝涓?
    // resolve_utf8 鈫?State::mutex锛夈€傚畠浠睘浜庡凡鍏抽棴鐨勫疄楠岃矾寰勶紝榛樿涓嶅惎鐢ㄣ€?
    if (g_legacy_capture != 0) {
        OnDamagedCapture(object, function, parameters);
        SendEventCapture(object, function, parameters);
        ApplyGeCapture(object, function, parameters);
    }
    if (!g_ui_state.damage || function == 0 || function != reinterpret_cast<uintptr_t>(g_dmg_fn)) return;
    int count = static_cast<int>(g_ui_state.replay);
    if (count <= 0 || count > 1024) return;
    if (g_replay_budget > 200) return;              // 濠殿噯绲界换鎰板箮濮橆厾鈻斿┑鐘叉处椤庢瑩鏌ㄥ☉妯肩劯濞村皷鏅犲畷妤€顓兼径瀣敍閻?
    if (!g_ctx || !g_ctx->ue5_framework) return;
    const auto* fw = static_cast<const AnomalyUe5FrameworkServiceV1*>(g_ctx->ue5_framework);
    if (!fw->is_game_thread || !fw->is_game_thread(fw->user)) return;
    auto process = g_pe_addr != 0 ? reinterpret_cast<PE_FN>(g_pe_addr) : nullptr;
    if (!process) return;
    for (int i = 0; i < count && i < 32; ++i) {
        process(reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), parameters);
        ++g_replay_budget;
    }
}

void TickFeatures() {
    // 闁诲海鎳撻ˇ顖炲矗韫囨稒鈷撻柍褜鍓熼弫宥咁潩閹垫寧甯″鎼佸箛椤掑倹娈梺鐟扮摠閸旀帞绮弽褜鍟呴柛娆忣槸閻熷繘鏌涢幒鎾寸凡闁汇倕瀚粭鐔衡偓锝庝簼閸婄數鎲搁幏宀婂殭缂併劍濞婂畷鎴濐煥閸曢潧澹橀梺瑙勵問閸嬪懎顕ｉ崸妤佹櫖鐎光偓閸曨亞绱氶梺绋跨箰缁夌敻鎯侀敍鍕殰鐟滃秹宕ｈ箛娑欑厒闊洦绋愰幉楣冩煕?
    if (g_resolve.chronos && g_resolve.damage && g_resolve.volley && g_resolve.gm && g_pc_inst)
        return;
    if (g_gworld_instr != 0) {
        __try {
            const int disp = *reinterpret_cast<const int*>((std::uintptr_t)g_gworld_instr + 3);
            const std::uintptr_t target = (std::uintptr_t)g_gworld_instr + 7 + (std::intptr_t)disp;
            g_gworld_addr = target;
            std::uintptr_t candidate = 0;
            __try { candidate = *reinterpret_cast<std::uintptr_t*>(target); } __except (EXCEPTION_EXECUTE_HANDLER) { candidate = 0; }
            if (candidate != 0) {
                unsigned int nid = 0;
                char nm[32] = {0};
                if (ue::Reader::SafeU32(candidate + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
                if (nm[0] == 0) {
                    std::uintptr_t alt = 0;
                    __try { alt = *reinterpret_cast<std::uintptr_t*>((std::uintptr_t)g_gworld_instr); } __except (EXCEPTION_EXECUTE_HANDLER) { alt = 0; }
                    if (alt != 0) candidate = alt;
                }
            }
            g_gworld_ptr = (unsigned long long)candidate;
        } __except (EXCEPTION_EXECUTE_HANDLER) { g_gworld_ptr = 0; }
    }
    if (!g_pc_inst) g_pc_inst = PlayerControllerFromWorld();
    static int tick = 0;
    if ((++tick % 30) != 0) return;   // 濠?30 闁汇埄鍨庨崶褎顏熼柣鐘叉处濞叉垹绮╅弶鎴旀瀻闁宠棄鎳愮粈澶愭⒑椤掆偓閻忔繈宕㈤妶澶婄睄闁冲搫鍊归崺鍌氼熆閹壆绨块悷娆欑畵瀹曘儲鎯旈姀鈽嗘敵婵炴垶鏌ㄩ幊蹇涘棘娴ｅ壊鍤?
    if (g_resolve.damage && g_resolve.volley && g_resolve.chronos && g_resolve.gm) return;
    if (!g_sig_ok || !g_names_ok) return;
    // retry every frame until resolved (services may appear after first frames)
    void* probe = g_ue.ObjectAt(0);
    g_probe = (unsigned long long)probe;
    if (probe) g_ue.ObjectNameId(probe, &g_probe_nid);
    g_ue.NameId("HTPlayerController", &g_want_id);
    if (g_want_id == 0) g_want_id = FindNameIdByPath("/Script/HTGame.HTPlayerController");
    if (g_id_pc == 0) g_id_pc = g_want_id ? g_want_id : FindNameIdByPath("/Script/HTGame.HTPlayerController");
    unsigned int pc_id = g_id_pc;
    void* pc_cls = g_ue.FindClassById(pc_id);
    g_cls_pc = (unsigned long long)pc_cls;
    if (pc_cls) {
        // DISABLED: full GObjects scan stalls/crashes the game
        if (void* pf = FindFuncByName(pc_cls, "PausedGameExcludePlayerByTimer")) {
            g_resolve.chronos = 1;
            g_pause_fn = pf;
        }
        // 角色大招的时停走 PauseGame(PG_UltraSkill_PlayPassiveEffect)/ResumeGame，
        // 这才是游戏自己“冻结敌人、玩家照常行动”的实现。
        if (void* pf = FindFuncByName(pc_cls, "PauseGame")) g_pause_type_fn = pf;
        if (void* rf = FindFuncByName(pc_cls, "ResumeGame")) g_resume_fn = rf;
        if (g_pause_type_fn && g_resume_fn) g_resolve.chronos = 1;
        if (FindFuncByName(pc_cls, "IsGM")) g_resolve.gm = 1;
        if (void* f = FindFuncByName(pc_cls, "CanShowGMConsole")) g_resolve.gm = 1;
        g_isgm_fn = FindFuncByName(pc_cls, "IsGM");
    }
    if (g_id_asc == 0) g_id_asc = FindNameIdByPath("/Script/HTGame.HTAbilitySystemComponent");
    unsigned int asc_id = g_id_asc;
    void* asc_cls = g_ue.FindClassById(asc_id);
    g_cls_asc = (unsigned long long)asc_cls;
    if (asc_cls) {
        if (void* df = FindFuncByName(asc_cls, "K2_ApplyGameplayEffectToTargetWithClass")) {
            g_resolve.damage = 1;
            g_dmg_fn = df;
        }
    }
    if (g_id_vb == 0) g_id_vb = FindNameIdByPath("/Script/HTGame.HTVolleyballGameManager");
    unsigned int vb_id = g_id_vb;
    void* vb_cls = g_ue.FindClassById(vb_id);
    g_cls_vb = (unsigned long long)vb_cls;
    if (vb_cls) {
        if (FindFuncByName(vb_cls, "GetRallyState")) g_resolve.volley = 1;
        if (FindFuncByName(vb_cls, "AwardPoint")) g_resolve.volley = 1;
        if (FindFuncByName(vb_cls, "Test_ForceWin")) g_resolve.volley = 1;
    }
    // 怪物基类路径：扫描时按继承链判断，不依赖类名前缀（覆盖名字不含 mon_/boss_ 的怪）。
    static int ai_tries = 0, mon_tries = 0;
    if (g_id_ai == 0) g_id_ai = FindNameIdByPath("/Script/HTGame.HTAICharacter");
    if (g_cls_ai == 0 && g_id_ai != 0 && ai_tries < 10) {
        ++ai_tries;
        g_cls_ai = (unsigned long long)g_ue.FindClassById(g_id_ai);
    }
    if (g_id_mon == 0) g_id_mon = FindNameIdByPath("/Script/HTGame.HTMonsterCharacter");
    if (g_cls_mon == 0 && g_id_mon != 0 && mon_tries < 10) {
        ++mon_tries;
        g_cls_mon = (unsigned long long)g_ue.FindClassById(g_id_mon);
    }
}

static int g_gm_selected2 = -1;
static unsigned int g_replay_count = 0;


// 婵炴垶鎸搁ˇ鏉款焽椤栨繄鐤€妞ゆ劧绲界粊褰掓⒑濞嗘儳娅欑紒杈ㄢ攺imeAccel 闂佸憡鑹鹃張顒勵敄濞嗘劗顩查柕鍫濆闂夊秹鏌ㄥ☉姗嗘缂佸彉鍗冲顔炬崉鏉炴壆涓嶆繛鎴炴尨閸嬫挻绻?/ 婵炴垶鎸撮崑鎾绘煟閳轰胶鎽犳俊鐐插€垮畷?/ 闂佽鍘归崹褰捤囬弻銉︾劵闁绘柨鍢查?
static const char* kHotkeyLabels[3] = {
    "\xE6\x97\xB6\xE5\x81\x9C\xE4\xB8\x80\xE6\xAC\xA1",              // 闂佸搫鍟冲▔娑欑閻樺磭鈻旈柍褜鍓欓埢?
    "\xE4\xB8\x80\xE7\x9B\xB4\xE6\x97\xB6\xE5\x81\x9C",              // 婵炴垶鎸撮崑鎾绘煟閳轰胶鎽犳俊鐐插€垮畷?
    "\xE6\x81\xA2\xE5\xA4\x8D\xE9\x80\x9F\xE5\xBA\xA6",              // 闂佽鍘归崹褰捤囬弻銉︾劵闁绘柨鍢查?
};

void ANOMALY_CALL OnHotkey(void* user, AnomalyGenerationHandleV1, const AnomalyInputSnapshotV1*) {
    const int index = static_cast<int>(reinterpret_cast<std::uintptr_t>(user));
    if (index == 3) {
        // F3 = 恢复速度（解除暂停）
        g_hot_restore = 1;
        g_ui_state.chronos = 0;
        g_chronos_req = 3;
        return;
    }
    if (index == 1) {
        // F1 = 时停一次
        g_hot_once = 1;
        g_ui_state.chronos = 1;
        g_chronos_req = 2;
        return;
    }
    else if (index == 2) {
        // F2 = 一直时停（开关）
        g_hot_hold = !g_hot_hold;
        g_ui_state.chronos = g_hot_hold ? 1 : 0;
        g_chronos_req = g_hot_hold ? 2 : 3;
    }
    else g_hot_restore = 1;
}

void EnsureHotkeys() {
    if (g_hot_registered || !g_ctx) return;
    const void* input_service = LazyService(&g_ctx->input, "anomaly.input", 1);
    if (!input_service) return;
    const auto* input = static_cast<const AnomalyInputServiceV1*>(input_service);
    if (!input->register_hotkey) return;
    for (int i = 0; i < 3; ++i) {
        AnomalyHotkeySpecV1 spec{};
        spec.struct_size = sizeof(spec);
        spec.modifiers = 0;
        spec.virtual_key = g_hot_vk[i];
        spec.flags = ANOMALY_HOTKEY_V1_ALLOW_WHILE_UI_CAPTURED;
        spec.id = AnomalyStringViewV1{kHotkeyLabels[i], std::strlen(kHotkeyLabels[i])};
        AnomalyGenerationHandleV1 handle{};
        input->register_hotkey(input->user, &spec, OnHotkey,
                               reinterpret_cast<void*>(static_cast<std::uintptr_t>(i + 1)), &handle);
        g_hot_handle[i] = handle;
    }
    g_hot_registered = 1;
}

// 把虚拟键显示成名字（改键后界面上要显示真实键名）。
static void VkName(uint32_t vk, char* out, std::size_t cap) {
    if (out == nullptr || cap == 0) return;
    if (vk >= 0x70 && vk <= 0x7B) {
        std::snprintf(out, cap, "F%u", static_cast<unsigned>(vk - 0x6F));
        return;
    }
    if (vk >= 0x41 && vk <= 0x5A) {
        std::snprintf(out, cap, "%c", static_cast<char>(vk));
        return;
    }
    if (vk >= 0x30 && vk <= 0x39) {
        std::snprintf(out, cap, "%c", static_cast<char>(vk));
        return;
    }
    switch (vk) {
        case 0x20: std::snprintf(out, cap, "Space"); break;
        case 0x09: std::snprintf(out, cap, "Tab"); break;
        case 0x0D: std::snprintf(out, cap, "Enter"); break;
        case 0x1B: std::snprintf(out, cap, "Esc"); break;
        case 0x25: std::snprintf(out, cap, "Left"); break;
        case 0x26: std::snprintf(out, cap, "Up"); break;
        case 0x27: std::snprintf(out, cap, "Right"); break;
        case 0x28: std::snprintf(out, cap, "Down"); break;
        case 0x21: std::snprintf(out, cap, "PgUp"); break;
        case 0x22: std::snprintf(out, cap, "PgDn"); break;
        case 0x23: std::snprintf(out, cap, "End"); break;
        case 0x24: std::snprintf(out, cap, "Home"); break;
        case 0x2D: std::snprintf(out, cap, "Insert"); break;
        case 0x2E: std::snprintf(out, cap, "Delete"); break;
        default: std::snprintf(out, cap, "0x%02X", static_cast<unsigned>(vk)); break;
    }
}

// 改键：点“改键”后进入等待，下一次按下的候选键就作为该热键，并重新注册。
static void TickHotkeyCapture() {
    if (g_hot_capture < 0 || !g_ctx) return;
    const void* input_service = LazyService(&g_ctx->input, "anomaly.input", 1);
    if (!input_service) return;
    const auto* input = static_cast<const AnomalyInputServiceV1*>(input_service);
    if (!input->was_pressed || !input->register_hotkey) return;
    static const uint32_t kCandidates[] = {
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B,  // F1-F12
        0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C,  // A-L
        0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,  // M-X
        0x59, 0x5A,                                                                // Y-Z
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,              // 0-9
        0x20, 0x09, 0x25, 0x26, 0x27, 0x28, 0x21, 0x22, 0x23, 0x24, 0x2D, 0x2E,  // 常用键
    };
    for (uint32_t vk : kCandidates) {
        int32_t pressed = 0;
        if (input->was_pressed(input->user, vk, &pressed).code != ANOMALY_STATUS_V1_OK) continue;
        if (pressed == 0) continue;
        const int i = g_hot_capture;
        g_hot_capture = -1;
        if (g_hot_handle[i].id != 0 && input->release_hotkey) {
            input->release_hotkey(input->user, g_hot_handle[i]);
            g_hot_handle[i] = {};
        }
        g_hot_vk[i] = vk;
        AnomalyHotkeySpecV1 spec{};
        spec.struct_size = sizeof(spec);
        spec.modifiers = 0;
        spec.virtual_key = vk;
        spec.flags = ANOMALY_HOTKEY_V1_ALLOW_WHILE_UI_CAPTURED;
        spec.id = AnomalyStringViewV1{kHotkeyLabels[i], std::strlen(kHotkeyLabels[i])};
        AnomalyGenerationHandleV1 handle{};
        input->register_hotkey(input->user, &spec, OnHotkey,
                               reinterpret_cast<void*>(static_cast<std::uintptr_t>(i + 1)), &handle);
        g_hot_handle[i] = handle;
        char nm[24] = {0};
        VkName(vk, nm, sizeof(nm));
        char b[64];
        std::snprintf(b, sizeof(b), "hotkey[%d] rebound to %s (0x%02X)", i, nm, vk);
        PushLog(b);
        return;
    }
}



// ===== 闂佺锕ょ粔鐢稿极妤ｅ啯鏅繛鍡欐異ply 闂備緡鍋勭换瀣閸洖绠伴柛宀€鍋炵亸?+ 濠殿噯绲界换鎺楋綖?N 濠电偛鏋撮妶鍥╊啎闂備礁銇樺ù鍥儍閻斿憡鍎?=====
using ApplyFn = void* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
static ApplyFn g_apply_original = nullptr;
static AnomalyGenerationHandleV1 g_apply_handle{};
static int g_apply_state = 0;
static int g_apply_hook_enabled = 0;   // 婵帗绋掗…鍫ヮ敇婵犳艾绀傞柟鎯板Г閿涙棃鏌ㄥ☉娆戠叝缂?Tokky 闂?burst 闂佸湱顭堥崐鍧楀箰椤掑嫬绀冮柤纰卞墰瀹?
static int g_apply_rc = -1;
static int g_apply_hits = 0;
// 闂佸憡鐟﹂悧鐐烘儊婢跺本濯奸柍鈺佸暞濞堝爼鏌ㄥ☉妯煎缂佹顦靛畷锝夊磼濞嗘垹鎲块梺鍛婂竾閸婃洟鎮炬ィ鍐╂櫖濠㈣埖绋撶粣妗漰ply 闁哄鏅滈弻銊ッ洪弽顓炵９闁稿繐鍚嬬瑧闂佸憡鐔紓姘辨嫻?0闂侀潧妫旂粈浣割嚕椤掑嫬鏋侀柣妤€鐗婄瑧闂佸憡鐔紓姘辨嫻閻旇櫣鐭?
static int g_ret_null = 0;
static int g_ret_nonnull = 0;
static int g_null_args = 0;
// 闂備焦褰冪粔鐢稿棘娴ｈ娓舵俊顖涱儥閸氬洭鏌涜箛鏂库枙婵☆偅鎸抽弫宥咁潩椤掆偓濞懷呯磽娴ｅ搫鏋欐い鎾存倐閺佸秶浠﹂懖鈺冩喒闁荤姴顑呴崯浼村极閵堝鏅€光偓閸曨亞绱氶梺绋跨箰缁夌數鎲伴崱娑樿摕闁规崘娉涘▓鐘电磼鐎ｎ亶鍎旈柛锝嗘倐瀵劑骞嗚閸ゃ倝鏌ゅ畡閭﹀殶婵炲牊鍨甸悾鐑藉煛閸屾粎姣?
static int g_replay_enabled = 0;
// 闂佸憡鐟﹂悧鐐烘儊婢舵劕绠ユい鎰剁到娴煎酣鏌ㄥ☉娆掑妞?a1/a2/a3 闂?0x100 闁诲孩绋掗〃澶嬩繆椤撱垹绠?0xFFFFFFFFFFFFFFFF 闁?
static int g_neg1_calls = 0;      // 闂佺厧鍢查崯鍧楁儍椤栨稓鈻旈柍褜鍓氱粙澶愵敂閸曨偅顥濋梺杞扮劍濠㈡﹢顢氶鐐闁跨喓濮村▍锟犳煕?-1 闁诲孩绋掗〃鍫濃枔閹寸姵瀚柛鎰典簼閺嗗繘鏌?
static int g_clean_calls = 0;     // 婵炴垶鎸搁ˇ顕€鏌屽鍫濈煑闁稿本绋掑▓鍫曟倵閻㈡鏀伴柦鏍у閺屽牓骞嬮幒鏃傛喒闂?-1 闁诲孩绋掗〃鍫濃枔閹寸姵瀚柛鎰典簼閺嗗繘鏌?
static int g_neg1_budget = 128;   // 闂佸憡鐟禍婵囩珶閸岀偛绠甸煫鍥ㄦ尭椤?128 濠电偛妫屽Σ鍕濠靛鐒奸柛顭戝枛鐢娊鏌熼梹鎰厡闁汇垼顕ч锝夊焵椤掑嫭鐓ラ柍?

// ===== Gate 闁荤喐鐟ラ崐鍦矈?=====
// dc_targets512 闂佸憡鐟ョ粔鐢告儑閸︻厾纾介柡宥囨暩缁€鍑碫A 0x7682F55~0x7682F5C闂佹寧绋戦¨鈧紒杈╂pply 闁?Gate 闂?test al,al; je ret闂?
// 闂佸湱顣介崑鎾趁?Gate 闂?AL 闁诲繐绻楁ご绋课?闂佸搫鐗滈崜姘额敃婵傜鍙婃い鏍ㄧ閸庡﹪鏌ｉ鍡楁瀾妞ゆ帗绮庣槐鎺楀箻閸撲焦姣堟繛瀵割儠閸婃洟顢?闂佹眹鍔岀€氼剟宕规禒瀣妞ゆ棁銆€閸嬫捇宕掑┑鍫㈩啇闂備焦褰冮懟顖濄亹瑜忛幏鐘诲及韫囷絽鏁归悷婊呭濞测晝妲愬┑鍥┾枖鐎广儱妫欓弳顓㈡煕濞嗘劦鐒剧紒缁樺灴瀹曞爼鎮欏ù瀣壕缂佹銆€閸?
using GateFn = unsigned char (*)(void*, void*, void*, void*, void*, void*, void*, void*);
static GateFn g_gate_original = nullptr;
static AnomalyGenerationHandleV1 g_gate_handle{};
static const void* g_gate_hook = nullptr;
static int g_gate_state = 0;
static int g_gate_rc = -1;
static int g_gate_true = 0;
static int g_gate_false = 0;
static int g_gate_seen = 0;
static int g_dmg_true = 0;
static int g_dmg_false = 0;

static unsigned char GateDetour(void* a1, void* a2, void* a3, void* a4,
                                void* a5, void* a6, void* a7, void* a8) {
    if (!g_gate_original) return 0;
    const unsigned char r = g_gate_original(a1, a2, a3, a4, a5, a6, a7, a8);
    if (r != 0) { ++g_gate_true; g_gate_seen = 1; } else { ++g_gate_false; }
    return r;
}

void InstallGateHook() {
    if (g_gate_state != 0 || !g_ctx) return;
    const void* svc = LazyService(&g_ctx->damage_chain, "anomaly.nte.damage-chain", 1);
    if (!svc) { g_gate_rc = -1; return; }
    const auto* dc = static_cast<const AnomalyNteDamageChainServiceV1*>(svc);
    if (!dc->gate_address) { g_gate_rc = -2; return; }
    std::uintptr_t addr = 0;
    if (dc->gate_address(dc->user, &addr).code != ANOMALY_STATUS_V1_OK || addr == 0) { g_gate_rc = -3; return; }
    const void* hook_service = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    if (!hook_service) { g_gate_rc = -4; return; }
    const auto* hook = static_cast<const AnomalyHookServiceV1*>(hook_service);
    if (!hook->create) { g_gate_rc = -5; return; }
    AnomalyHookRequestV1 req{};
    req.struct_size = sizeof(req);
    req.kind = ANOMALY_HOOK_V1_FUNCTION;
    req.target = addr;
    req.detour = reinterpret_cast<void*>(&GateDetour);
    req.label = AnomalyStringViewV1{"ultimate.dc.gate", std::strlen("ultimate.dc.gate")};
    AnomalyGenerationHandleV1 h{};
    std::uintptr_t orig = 0;
    const auto st = hook->create(hook->user, &req, &orig, &h);
    g_gate_rc = static_cast<int>(st.code);
    if (st.code == ANOMALY_STATUS_V1_OK) {
        g_gate_original = reinterpret_cast<GateFn>(orig);
        g_gate_handle = h;
        g_gate_hook = hook_service;
        g_gate_state = 1;
    } else {
        g_gate_state = 2;
    }
}

// 闂佸憡顨嗛悺鏇灻归崶顒€绫嶉柛顐ｆ礀濞呯偤鏌￠埀顒勫矗閵夈劉鎷℃繛?hook闂佹寧绋戦惌鍌涘閳哄懎绀傜€广儱娲ら崝浼存⒑閹绘帞啸婵炴潙娲畷?detour 闂佸湱顭堝ú銈夊箖濠婂喚鍟呴柟缁樺笒缁佸寮堕悙鏉戠亰闁哥儐鍓熼幆?
void ReleaseHooks() {
    if (!g_ctx) return;
    if (g_gate_state == 1) {
        if (g_gate_hook) {
            const auto* hook = static_cast<const AnomalyHookServiceV1*>(g_gate_hook);
            if (hook->release) hook->release(hook->user, g_gate_handle);
        }
        g_gate_hook = nullptr;
        g_gate_state = 0;
        g_gate_original = nullptr;
    }
        UnwatchProcessEvent();
    if (g_apply_state == 1) {
        if (g_ctx->hook) {
            const auto* hook = static_cast<const AnomalyHookServiceV1*>(g_ctx->hook);
            if (hook->release) hook->release(hook->user, g_apply_handle);
        }
        g_apply_state = 0;
        g_apply_original = nullptr;
    }
}

static void*  g_rq_args[8] = {nullptr};
static std::uintptr_t g_player_pawn_ref = 0;
static std::uintptr_t g_ondmg_ufn = 0;
static std::uintptr_t g_od_recv = 0;
static unsigned char  g_od_params[0x148] = {0};
static int g_od_remaining = 0;
static int g_od_max = 10;
static int g_od_interval = 100;
static std::uint32_t g_od_due = 0;
// 闂佺儵鏅╅崰妤呮偉閿濆妫橀柦妯侯槸椤綁姊洪幓鎺斝ｉ柡鍌欑窔閺佸秹宕煎┑鍐ㄤ壕鐎广儱妫欓弳浼存煥濞戞﹩妾х紒杈╁缁傛帗鎯旈敐鍡楃劯闁?OnDamaged 闂備焦褰冮懟顖濄亹?闂佺懓鐏氶幐鐑芥嚈閹达附鐒介柨婵嗗椤忛亶鏌涢幋锝呭惞闁轰降鍊濋幆?GE 缂?闂佹寧绋戞總鏃傜不閿濆瑙﹂幖娣灩閻﹀姊婚崒娑欏唉婵炲爜鍥ㄥ仺?
// HTAbilitySystemComponent:K2_ApplyGameplayEffectToTargetWithClass 婵炴垶鎹侀褎鎱ㄩ埡鍛闁芥ê顦～锝夋煏?
// 婵炴垶鎸哥€涒晛螞閿曞倸瀚夊璺侯槺閻熷繘鏌℃径娑氱瘈闁革絾鎮傚銊╁矗閵壯呮喒闂佸憡鑹鹃惌澶屾濠靛洦浜ゆ繛鍡樻尭濞呪€斥槈閹惧磭效闁革絾鎮傚銊╂倷椤掆偓缁€瀣煠閹冩Щ闁糕晛鐭傞幆鍐礋椤愩倖鎲婚梺鐓庢惈閸婄顭囬敓鐘虫櫖鐎光偓閸愶絽浜鹃悘鐐靛亾绗戦梻浣瑰絻缁夌敻寮绘繝鍥у嚑闁告洦鍨崑鎾绘偐閹绘帗顥濋梺杞扮劍婢瑰鍩€椤掍焦鐓涢柍?
// 婵炴垶鏌ㄩ鍛櫠閻樺啿绶為弶鍫亯琚濋梺姹囧妼鐎氼參鎮ч幘顔肩倞闁绘娅曠瑧 ModifyData 闂佺绻堥崝鎴澝归崒鐐存櫖闁秆勫trengthMult/nStackCount 闂備緡鍠涙慨銈呂?0闂佹寧绋戦¨鈧紒杈ㄧ箘缁灚寰勬径宀冨 0 闂佺锕ょ粔鏉戭啅鏉堛劍鍎熼柨鏃囥€€閸?
static std::uintptr_t g_ta_ge_class = 0;     // TSubclassOf<UGameplayEffect>
static std::uintptr_t g_ta_target_asc = 0;   // 闂佸憡鐟﹂〃鍛搭敊婵犲洦鍤€?ASC
static int g_ta_remaining = 0;
static int g_ta_max = 32;
static int g_ta_interval = 100;
static std::uint32_t g_ta_due = 0;
static std::uint64_t g_ta_fired = 0;
static void TickTargetApplyReplay();
static int g_capture_mode = 0;              // 0=OnDamaged  1=ApplyGE
static std::uintptr_t g_ae_ufn = 0;
static std::uintptr_t g_ae_recv = 0;
static unsigned char  g_ae_params[0x78] = {0};
static int g_ae_remaining = 0;
static int g_ae_max = 10;
static int g_ae_interval = 100;
static std::uint32_t g_ae_due = 0;
static int    g_rq_remaining = 0;
static int    g_rq_tier = 0;
static std::uint64_t g_rq_next_due = 0;

static std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
static std::uint64_t BaseIntervalMs() {
    int n = static_cast<int>(g_ui_state.replay);
    if (n < 1) n = 1;
    std::uint64_t ms = static_cast<std::uint64_t>(1000 / n);
    if (ms < 16) ms = 16;
    return ms;
}
static std::uint64_t TierIntervalMs(int tier) {
    const std::uint64_t base = BaseIntervalMs();
    return tier == 0 ? base : (tier == 1 ? base * 2 : base * 5);
}
static bool ArgsLookValid() {
    for (int i = 0; i < 8; ++i) {
        void* arg = g_rq_args[i];
        if (arg == nullptr) continue;
        if (reinterpret_cast<std::uintptr_t>(arg) == ~static_cast<std::uintptr_t>(0)) return false;
    }
    return true;
}
// 闂備焦褰冪粔鐢稿棘娓氣偓楠炲秷顦舵い銏″灥椤斿繘骞撻幒宥囦笉闂佹眹鍩勯悡澶屾閹殿喒鍋撻崷顓炰沪缂?replay 婵犫拃鍛槐闁哄棜椴搁幏鍛村幢濡厧鐓戦梺瑙勬綑缁绘劙宕弶鎴旀煢闁斥晛鍟粻鎺懳涢悧鍫㈢畵妞わ妇绮€靛ジ濡堕崪浣告暯闂佹寧绋戞總鏃傜箔閺嶎厽鈷曢煫鍥ㄦ尰閸婄數鈧娲嶉弲婊呰姳濠靛违?
// 婵烇絽娲︾换鍕汲閳ь剙霉閻欏懐鎮奸柣鏍ㄧ矋缁嬪鎯旈妶搴℃倎闂佽桨绀佹惔婊呮濠靛洨鈻旂€广儱鎳庨弲娆撴偣鐎ｎ亜鏆熼柡浣靛€栫粋鎺旀媼瀹曞洨协闂佸憡顭囬崰鎾诲极閹捐绀勯柤鎭掑劜濞堝爼鏌?
static constexpr bool kReplayExec = false;


// ===== OnDamaged 闂佺顫夊ú婵嬬嵁?+ 闂佸湱顭堥ˇ鏉款渻閸岀偞鈷掗柟缁㈠枛濞呫垽鏌￠埀顒勫箻閸涱垳顦╅梺鍦暯閸嬫捇鏌￠崼婵愭Ш閻㈩垵娅ｉ埀顒傤攰閸╂牕顭囬棃娑掓敠闁归偊浜濈缓娲倵閸︻収鍔滅紒杈ㄧ箖缁嬪顓奸崼銏㈢暫濠电姴顭堥崐妤冩偖椤愶箑绀嗘い鎰靛墰缁€?====
static void OnDamagedCapture(std::uintptr_t object, std::uintptr_t function, void* parameters) {
    if (parameters == nullptr || function == 0 || g_player_pawn_ref < 0x10000) return;
    if (g_ondmg_ufn == 0) {
        unsigned int nid = 0;
        char nm[48] = {0};
        if (!ue::Reader::SafeU32(function + 0x18, &nid)) return;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "OnDamaged") != 0) return;
        g_ondmg_ufn = function;
        PushLog("ondmg: ufn resolved");
    }
    if (function != g_ondmg_ufn) return;
    std::uintptr_t inst = 0, causer = 0;
    ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(parameters) + 0x138, &inst);
    ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(parameters) + 0x140, &causer);
    if (inst != g_player_pawn_ref && causer != g_player_pawn_ref) return;
    std::memcpy(g_od_params, parameters, sizeof(g_od_params));
    g_od_recv = object;
    // OnDamaged 闂佸憡鐟ラ崐褰掑汲閻旂儤鏆滈柛鎰╁妿濠€浼存煥濞戞ɑ绠絋Game_parameters.hpp闂?x148闂佹寧绋戦¨鈧紒?
    //   0x000 DamageAmount | 0x008 FHitResult(0x108) | 0x110 DamageTags | 0x130 DamageGameplayEffect
    //   0x138 InstigatorCharacter | 0x140 DamageCauser
    // 闂?GE 闁诲海鏁搁、濠囨寘閸曨垱鍎嶉柛鏇ㄥ弨椤箓鏌ㄥ☉妯昏嫙Object::Class @0x10闂佹寧绋戦ˇ顔剧礊閺冣偓缁?TSubclassOf闂佹寧绋戦懟顖濄亹閸懇鍋撻悷鎵瀮闁?ASC = object+0x940闂?
    {
        std::uintptr_t ge = 0, ge_class = 0, victim_asc = 0;
        if (ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(parameters) + 0x130, &ge) &&
            ge >= 0x10000 &&
            ue::Reader::SafePtr(ge + 0x10, &ge_class) && ge_class >= 0x10000 &&
            ue::Reader::SafePtr(object + 0x940, &victim_asc) && victim_asc >= 0x10000) {
            g_ta_ge_class = ge_class;
            g_ta_target_asc = victim_asc;
            g_ta_remaining = g_ta_max;
            g_ta_fired = 0;
            g_ta_due = static_cast<std::uint32_t>(NowMs() + g_ta_interval);
            char tb[128];
            std::snprintf(tb, sizeof(tb), "ta: arm ge=0x%llX asc=0x%llX n=%d",
                          (unsigned long long)ge_class,
                          (unsigned long long)victim_asc, g_ta_max);
            PushLog(tb);
        } else {
            PushLog("ta: arm skipped (ge/asc unresolved)");
        }
    }
    g_od_remaining = g_od_max;
    g_od_due = static_cast<std::uint32_t>(NowMs() + g_od_interval);
    char b[128];
    std::snprintf(b, sizeof(b), "ondmg: arm recv=0x%llX left=%d",
                  (unsigned long long)object, g_od_max);
    PushLog(b);
}

static void TickOnDamagedReplay() {
    if (g_od_remaining <= 0 || g_od_recv < 0x10000 || g_ondmg_ufn == 0) return;
    if (NowMs() < g_od_due) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { g_od_remaining = 0; return; }
    const std::uintptr_t pe = base + 0x3E41360;
    const std::uintptr_t flags = g_ondmg_ufn + 0xB0;
    unsigned int saved = 0;
    if (!ue::Reader::SafeU32(flags, &saved)) { g_od_remaining = 0; return; }
    DWORD op = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op) == 0) {
        g_od_remaining = 0; return;
    }
    *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);
    unsigned char local[0x148];
    std::memcpy(local, g_od_params, sizeof(local));
    reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(g_od_recv),
                                   reinterpret_cast<void*>(g_ondmg_ufn), local);
    DWORD op2 = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op2) != 0) {
        *reinterpret_cast<unsigned int*>(flags) = saved;
        VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);
    }
    --g_od_remaining;
    g_od_due = static_cast<std::uint32_t>(NowMs() + g_od_interval);
}


// ===== 闂佸搫鍊介～澶愩€佸鍡欘洸鐎光偓鐎ｎ剛鐛ラ梺纭咁潐濞叉繈鐛?K2_ApplyGameEffectWithClassByActor 闂佹眹鍔岀€氼喖锕㈤敍鍕ㄥ亾閸︻厼浠滅€殿噮鍓熷顐﹀级閼愁垰鐝梻浣瑰絻缁夌敻寮?=====
static void ApplyGeCapture(std::uintptr_t object, std::uintptr_t function, void* parameters) {
    if (g_capture_mode != 1 || parameters == nullptr || function == 0) return;
    if (g_ae_ufn == 0) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(function + 0x18, &nid)) return;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "K2_ApplyGameEffectWithClassByActor") != 0) return;
        g_ae_ufn = function;
        PushLog("applyge: ufn resolved");
    }
    if (function != g_ae_ufn) return;
    std::uintptr_t src = 0;
    ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(parameters), &src);
    if (g_player_pawn_ref >= 0x10000 && src != 0 && src != g_player_pawn_ref) return;
    std::memcpy(g_ae_params, parameters, sizeof(g_ae_params));
    g_ae_recv = object;
    g_ae_remaining = g_ae_max;
    g_ae_due = static_cast<std::uint32_t>(NowMs() + g_ae_interval);
    char b[160];
    std::snprintf(b, sizeof(b), "applyge: arm recv=0x%llX src=0x%llX ge=0x%llX left=%d",
                  (unsigned long long)object, (unsigned long long)src,
                  (unsigned long long)(*reinterpret_cast<std::uintptr_t*>(g_ae_params + 0x08)), g_ae_max);
    PushLog(b);
}

static void TickApplyGeReplay() {
    if (g_ae_remaining <= 0 || g_ae_recv < 0x10000 || g_ae_ufn == 0) return;
    if (NowMs() < g_ae_due) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { g_ae_remaining = 0; return; }
    const std::uintptr_t pe = base + 0x3E41360;
    const std::uintptr_t flags = g_ae_ufn + 0xB0;
    unsigned int saved = 0;
    if (!ue::Reader::SafeU32(flags, &saved)) { g_ae_remaining = 0; return; }
    DWORD op = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op) == 0) {
        g_ae_remaining = 0; return;
    }
    *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);
    unsigned char local[0x78];
    std::memcpy(local, g_ae_params, sizeof(local));
    reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(g_ae_recv),
                                reinterpret_cast<void*>(g_ae_ufn), local);
    DWORD op2 = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op2) != 0) {
        *reinterpret_cast<unsigned int*>(flags) = saved;
        VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);
    }
    --g_ae_remaining;
    g_ae_due = static_cast<std::uint32_t>(NowMs() + g_ae_interval);
}


// ===== 闂佺顫夊ú婵嬬嵁韫囨稓宓侀柤鎼佹涧閻﹀鏌￠崘銊у煟闁革絾鎮傚?SendGameplayEventToActor闂佹寧绋戦悧蹇擄耿閿涘嫧鍋撻崷顓炰粧缂併劊鍔嶇粋鎺楀垂濞戞ɑ娅旈梺鍛婎焽閸犳捇寮幘鏉戭嚤闁靛牆鎷嬮崬铏圭磼閻欏懐纾块柛銈呴叄閺?====
static std::uintptr_t g_sge_ufn = 0;
static std::uintptr_t g_sge_obj = 0;
static unsigned char  g_sge_params[0xC0] = {0};
static int g_sge_left = 0;
static int g_sge_done = 0;
static std::uint32_t g_sge_due = 0;
static int g_sge_max = 8;
static int g_sge_interval = 100;
static std::uintptr_t g_sge_player = 0;

static void SendEventCapture(std::uintptr_t object, std::uintptr_t function, void* parameters) {
    if (parameters == nullptr || function == 0) return;
    if (g_sge_ufn == 0) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(function + 0x18, &nid)) return;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "SendGameplayEventToActor") != 0) return;
        g_sge_ufn = function;
        PushLog("sge: ufn resolved");
    }
    if (function != g_sge_ufn) return;
    if (g_sge_player >= 0x10000) {
        std::uintptr_t inst = 0;
        ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(parameters) + 0x10, &inst);
        if (inst != 0 && inst != g_sge_player) return;
    }
    std::memcpy(g_sge_params, parameters, sizeof(g_sge_params));
    g_sge_obj = object;
    g_sge_left = g_sge_max;
    g_sge_done = 0;
    g_sge_due = static_cast<std::uint32_t>(NowMs() + g_sge_interval);
    char b[144];
    std::snprintf(b, sizeof(b), "sge: arm obj=0x%llX actor=0x%llX left=%d",
                  (unsigned long long)object,
                  (unsigned long long)(*reinterpret_cast<std::uintptr_t*>(g_sge_params)),
                  g_sge_max);
    PushLog(b);
}

static void TickSendEventReplay() {
    if (g_sge_left <= 0 || g_sge_ufn == 0 || g_sge_obj < 0x10000) return;
    if (NowMs() < g_sge_due) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { g_sge_left = 0; return; }
    unsigned char local[0xC0];
    std::memcpy(local, g_sge_params, sizeof(local));
    reinterpret_cast<PE_FN>(base + 0x3E41360)(reinterpret_cast<void*>(g_sge_obj),
                                              reinterpret_cast<void*>(g_sge_ufn), local);
    --g_sge_left;
    ++g_sge_done;
    char b[96];
    std::snprintf(b, sizeof(b), "sge: exec n=%d left=%d", g_sge_done, g_sge_left);
    PushLog(b);
    g_sge_due = static_cast<std::uint32_t>(NowMs() + g_sge_interval);
}

void TickReplayQueue() {
    if (!kReplayExec) { g_rq_remaining = 0; return; }
    if (g_rq_remaining <= 0 || !g_apply_original) return;
    if (NowMs() < g_rq_next_due) return;
    if (!ArgsLookValid()) {
        if (g_rq_tier < 2) { ++g_rq_tier; g_rq_next_due = NowMs() + TierIntervalMs(g_rq_tier); return; }
        g_rq_remaining = 0;
        return;
    }
    g_apply_original(g_rq_args[0], g_rq_args[1], g_rq_args[2], g_rq_args[3],
                     g_rq_args[4], g_rq_args[5], g_rq_args[6], g_rq_args[7]);
    --g_rq_remaining;
    ++g_replay_count;
    g_rq_next_due = NowMs() + TierIntervalMs(g_rq_tier);
}

// 闂佸憡鐟禍锝夘敋娴兼潙绠ユい鎰剁到娴煎酣鏌ㄥ☉娆掑鐎殿噮鍓熷顐﹀级濞嗘儳娈搁柣鐘靛亹閳ь剙鍟块·?0x100 闁诲孩绋掗〃澶嬩繆椤撱垺鐓傞悘鐐靛亾绗戦梺鍛婄啲缁犳垿鎮洪妸鈺佹嵍?0xFFFFFFFFFFFFFFFF 闁诲孩绋掗〃鎰濞嗗繒鏆﹂柍鍝勫€婚惃楣冩煕閿旇姤绶叉繛鐓庡暟缁辨帒鈻庤箛鎾寸様闂佹眹鍔岀€氼噣宕垫惔锝嗩潟妞ゎ偒鍠氱粈?
static bool HasNeg1Word(void* object) {
    if (!g_ctx) return false;
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(g_ctx->core);
    if (!core || !core->read_memory) return false;
    const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(object);
    if (p < 0x10000 || p == ~static_cast<std::uintptr_t>(0)) return false;
    unsigned char buf[0x100] = {0};
    AnomalyMutableByteSpanV1 span{buf, sizeof(buf)};
    if (core->read_memory(core->user, p, span).code != ANOMALY_STATUS_V1_OK) return false;
    for (std::size_t i = 0; i + 8 <= sizeof(buf); i += 8) {
        std::uint64_t v = 0;
        std::memcpy(&v, buf + i, 8);
        if (v == ~static_cast<std::uint64_t>(0)) return true;
    }
    return false;
}

// ===== A1/A2 闂佽浜介崹浠嬪箯閿熺姵鏅柛顐犲劤鐢盯鎮规担闈涒偓鏇㈠矗瑜斿濠氼敇閵娧咁槴=====
// dump: class UHTAbilitySystemComponent final : public UHTAttributeComponent
//       UHTAttributeComponent::HPCurrent = 0x1A38
static int g_probe_on = 0;
static std::uintptr_t g_asc[64] = {0};
static int g_asc_n = 0;
static int g_hp_drop = 0, g_hp_same = 0, g_hp_budget = 64;
static char g_asc_cls[32] = {0};
// 闁荤姳璀﹂崹鎵?闂佹寧绋掗鎻?缂備緡鍋嗙划顖滅博婵犳艾纭€闁哄浂浜炵粈鍕煙閹帒鐏查柟鍑ょ節楠炲秹顢曢敐鍛礋闂佸搫鍟抽崺鏍极瑜版帗鈷栭柛鈽嗗幘缁€澶娿€掑顓犫姇闁绘牕鐖奸獮瀣疀閹捐櫕鏆ラ梻渚囧亝閸旀牕顭囬鐐寸劵濠㈣泛顑呴銉╂煥?
static char g_ge_names[256][48];
static std::uintptr_t g_ge_ptrs[256];
static int g_ge_count = 0;
static int g_ge_filter = 0;
static const char* const kGeFilters[5] = {"", "Nanally", "Lacrimosa", "Shinku", "Zankou"};
static char g_asc_names[64][48];
static int g_asc_sel = -1;
static int g_ge_sel = -1;
// A2 闂佹椿浜為崰搴ㄥ焵椤掑倶鈧妲愰幍顔瑰亾閻熻埇浠掗悗闈涙湰鐎靛ジ濡堕崪浣告暯婵炲瓨绮岄鍕枎閵忕妴瑙勭瑹婵犲嫮顦〨ame 缂備焦宕樺▔鏇㈠煝婵傜鏄ラ柣鏇炲€荤粈?
static int g_combat_events = 0;
static std::uint64_t g_combat_seq = 0;
// 闁哄鏅濋崑銈夊绩閿濆鐭楁い蹇撳闊?闂佹椿浜為崰搴ㄦ偪閸曨剙顕遍柕鍫濇媼閸炲搫霉濠婂喚鍎庢繛?+ 闂佸搫鐗冮崑鎾诲级閳哄倸鐏ｇ紒鏃€娼欓埢?Apply"闂佸搫鍟冲▔娑氱磽閹惧墎鍗?
static std::uint64_t g_apply_ms = 0;
static int g_armed_for_apply = 0;

void TickCombatEvents() {
    if (!g_ctx || !g_ctx->nte_combat) return;
    if (!g_probe_on && !(g_ui_state.damage && g_replay_enabled)) return;   // 婵炴垶鎸哥粔鍫曞极閵堝绫嶉柡鍫㈡暩閻熸繈寮堕悜鍡楁灕妞?
    const auto* c = static_cast<const AnomalyNteCombatServiceV1*>(g_ctx->nte_combat);
    if (!c->next_damage_event) return;
    int got = 0;
    for (int i = 0; i < 16; ++i) {
        AnomalyNteDamageEventV1 ev{};
        ev.struct_size = sizeof(ev);
        if (c->next_damage_event(c->user, g_combat_seq, &ev).code != ANOMALY_STATUS_V1_OK) break;
        g_combat_seq = ev.sequence;
        ++g_combat_events;
        ++got;
    }
    if (got > 0 && g_ui_state.damage && g_replay_enabled && g_apply_original &&
        !g_armed_for_apply && (NowMs() - g_apply_ms) <= 250 && ArgsLookValid()) {
        int per = static_cast<int>(g_ui_state.replay);
        if (per < 1) per = 1;
        if (per > 64) per = 64;
        if (per > 1) {
            g_rq_remaining = per - 1;
            g_rq_tier = 0;
            g_rq_next_due = NowMs() + TierIntervalMs(0);
        }
        g_armed_for_apply = 1;
    }
}

static bool SafeF32(std::uintptr_t addr, float* out) {
    if (!out || addr < 0x10000) return false;
    __try {
        *out = *reinterpret_cast<const float*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// GObjects 闁荤姳绀佸鈥澄涢崼鏇為棷闁靛闄勫鎾绘煕鐎ｎ偆鐓紒缁樺哺楠炲酣濡惰箛鏇犵崶闁烩剝甯掗敃锕傚疾椤愶絽顕辨慨妯诲墯閸ゃ垽鏌涢幒鎾愁棆闁?chunk 闂佸湱顭堝ú顓㈠箯閿熺姵鏅柛顐ｇ箘閳规稒绻濊閸燁偄锕㈤幘顔奸敜闁?0x0闂佹寧绋戦¨鈧紒杈ㄧ箖濞艰鈻庨幘铏仦闂?SEH 闂佸憡鐗曢幊鎾绘晬?
static void* SafeObjectAt(unsigned int index) {
    __try {
        return g_ue.ObjectAt(index);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static int   g_hp_ok = 0;    // 闂佺懓鐡ㄩ崝鏇熸叏濞戞碍瀚氶悹鍥ㄥ絻閻?HPCurrent 闂佹眹鍔岀€氼剟鎮块崟顒傜懝閻庯綆鍋呭▓?
static int   g_hp_nz = 0;    // 闂佺绻戝﹢褰掓嚈閹达附顥堥柣鎰劋缁佲晠鏌ｉ妸銉ヮ仼闁汇倕瀚粭鐔衡偓锝庡亝濞?
static float g_hp_max = 0.0f;

static float SumHP() {
    float total = 0.0f;
    g_hp_ok = 0;
    g_hp_nz = 0;
    g_hp_max = 0.0f;
    for (int i = 0; i < g_asc_n; ++i) {
        float hp = 0.0f;
        if (!SafeF32(g_asc[i] + 0x1A38, &hp)) continue;
        ++g_hp_ok;
        if (hp > 0.001f) { ++g_hp_nz; if (hp > g_hp_max) g_hp_max = hp; }
        total += hp;
    }
    return total;
}

// 婵犫拃鍛槐闁革絾妞介獮宥夘敃閿濆懍绱?GObjects闂佹寧绋戦悧濠囨儊閿涘嫭鏆?2 婵炴垶鎸稿ù鍕濮樿埖鏅悘鐐靛亾閺嗩亪姊?UHTAbilitySystemComponent 闁诲骸婀遍崑妯兼閵夆晜鏅繛鎴烆殘椤忛亶寮堕悜鍡楁灍濠㈢懓鍊婚埀顒傛嚀閺堫剚鏅堕悩璇茬闁归偊浜濋崬澶岀磽娴ｈ灏伴柣?
// invoke probe: service availability + target derivation
static void ClassNameOf(void* object, char* out, std::size_t cap);
static int g_fn_ok = 0;
static std::uintptr_t g_ta_obj = 0;
static char g_ta_cls[32] = {0};
static int g_invoke_rc = 999;
static int g_sweep_on = 0;
static int g_sweep_i = 0;
static int g_sweep_fn = 0;
static int g_sweep_ge = 0;
static char g_recv_cls[32] = {0};
static std::uintptr_t g_applyge_fn = 0;
static char g_fn_names[32][64];
static std::uintptr_t g_fn_ptrs[32];
static int g_fn_count = 0;
static int g_fn_sel = -1;

// 闂佸湱绮敮濠傗枎閵忋倕绀冮柛娑卞枟閿涘鐓崶顭戞缂佽鲸鐟╅幃娆掋亹閹哄棗浜?GE 闂佸搫鍟崕濂稿吹濠婂牆绀夐柕濞垮妽鐎氬綊鏌涢弮鍌毿＄紒鏂款嚟閹澘鐣濈€ｎ剛顦?
static char g_log[32][96];
static int g_log_n = 0;

static void PushLog(const char* text) {
    if (text == nullptr) return;
    if (g_log_n < 32) {
        std::snprintf(g_log[g_log_n], sizeof(g_log[0]), "%s", text);
        ++g_log_n;
    } else {
        for (int i = 1; i < 32; ++i) std::memcpy(g_log[i - 1], g_log[i], sizeof(g_log[0]));
        std::snprintf(g_log[31], sizeof(g_log[0]), "%s", text);
    }
    // 闂佸憡鑹鹃張顒€顪冮崒娑欎氦闁芥ê顦～锝夋煕閹烘柨顣奸柣鏍х埣楠炲绠涢弴鐔虹崶閻?kx_log.txt闂佹寧绋戦悧鍛矚閼哥數顩查幖杈剧秵閻庤尙绱掗幑鎰《缂併劏娉曞☉鐢割敊濞嗘儳娈洪梺鍛婄懄閻楊厾妲?
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        char* slash = std::strrchr(path, '\\');
        if (slash != nullptr) {
            *slash = 0;
            char file[MAX_PATH + 32];
            std::snprintf(file, sizeof(file), "%s\\kx_log.txt", path);
            FILE* fp = std::fopen(file, "a");
            if (fp != nullptr) {
                std::fprintf(fp, "%s\n", text);
                std::fclose(fp);
            }
        }
    }
}

void ProbeFunctionsService() {
    static int frame = 0;
    if (!g_ctx) return;
    if (g_ctx->functions) { g_fn_ok = 1; return; }
    if ((++frame % 30) != 0) return;
    LazyService(&g_ctx->functions, "anomaly.ue5.functions", 1);
    g_fn_ok = g_ctx->functions ? 1 : 0;
}

// 婵炴垶鎸撮崑鎾绘⒑濞嗘儳娅嶅ù婊冪埣瀹曘垽宕遍鐘电崶濠殿噯绲界换鎰板箮濮橆厼绶為柛鏇ㄥ幗閸婄偛鈽夐幘顖氫壕闂佸搫顦Σ鍕濞嗘挸鎹?on_update / Game 缂備焦宕樺▔鏇㈠煝婵傜鏄ラ柣鏃傚帶濞咃繝鎮归悜妯虹厐缂?
// 闂?UClass::Children(0x48) 闂備緡鍓欑粔鏉戭啅閼姐倖鍠嗛柨婵嗘閳?UFunction闂佹寧绋戦懟顖炴嚐閻斿吋鍋?ExecFunction(0xD8) 闂佸搫绉村ú銈囨崲閺冣偓閹峰懎顓奸崼銏㈢暫濠?
// 闂佹寧绋戦悧鍛閻戣棄绠?Tokky-NTE 闂傚倸鐗婇悷锕傚焵椤戣法顦﹂柛銊ラ叄瀵悂骞囬鐣屾殸闁荤喐鐟辩徊楣冩倵娴犲妫橀柣鐔稿绾偓闂佹寧绋掔粙鎴﹀箖閺囩姭鍋撳☉娆嶄沪閻?dump 婵炴垶鎼╅崢濂稿吹椤撱垺鍋濋柟娈垮枟閻?K2_ApplyGameEffectWithClassByActor闂?
static std::uintptr_t ResolveApplyUfn() {
    if (g_applyge_fn != 0) return g_applyge_fn;
    if (!g_ctx) return 0;
    unsigned int cid = FindNameIdByPath("/Script/HTGame.HTAbilitySystemComponent");
    void* cls = cid ? g_ue.FindClassById(cid) : nullptr;
    if (cls == nullptr) return 0;
    std::uintptr_t child = 0;
    if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(cls) + 0x48, &child)) return 0;
    for (int guard = 0; guard < 4096 && child != 0; ++guard) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (ue::Reader::SafeU32(child + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "K2_ApplyGameEffectWithClassByActor") == 0 ||
            std::strstr(nm, "ApplyGameEffect") != nullptr) {
            std::uintptr_t exec = 0;
            if (ue::Reader::SafePtr(child + 0xD8, &exec) && exec >= 0x10000) {
                g_applyge_fn = child;
                return child;
            }
        }
        std::uintptr_t next = 0;
        if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
        child = next;
    }
    return 0;
}



// ===== ProcessEvent 闁荤喐鐟ラ崐鎼佹儌閸岀偞鏅柛顐ｇ箓濞懷囨偣閸ワ妇涓茬紒?====
// 闁荤姳闄嶉崹钘壩ｉ崟顓涘亾閻熻埇浠掗悗?anomaly.ue5.process-event闂佹寧绋戦惌渚€顢欓崶顏備汗闁哄洨鍋為崐鎶芥倵濞戞瑯娈旈悘蹇ｅ櫍閺屽﹤顓奸崶銊ф殸闂佸憡鍨兼慨銈夊汲閻旂儤瀚柛鎰典簼閺嗗繘鏌ㄥ☉妯肩伇闁轰降鍊栫粋宥嗘償閵堝孩缍嗛柣?Tokky 闂佹眹鍔岀€氼喖锕㈤敍鍕ㄥ亾閸︻厼浠╅柣銊у枛閹粙濡搁敐鍌氫壕?
static const void* g_pe_svc = nullptr;
static AnomalyGenerationHandleV1 g_pe_handle{};
static int g_pe_state = 0;
static int g_pe_count = 0;

static std::uintptr_t g_last_pe_fn = 0;
static std::uintptr_t g_tokky_lo = 0;
static std::uintptr_t g_tokky_hi = 0;

static void ANOMALY_CALL PeWatcher(void* user, std::uintptr_t object, std::uintptr_t function, void* parameters) {
    (void)user;
    if (function < 0x10000) return;
    unsigned int nid = 0;
    char fname[64] = {0};
    if (ue::Reader::SafeU32(function + 0x18, &nid)) DecodeName(nid, fname, sizeof(fname));
    if (std::strstr(fname, "ApplyGameEffect") == nullptr &&
        std::strstr(fname, "ApplyGameplayEffect") == nullptr) return;
    g_last_pe_fn = function;
    if (g_pe_count >= 64) return;
    ++g_pe_count;
    unsigned long long p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0;
    __try {
        if (parameters != nullptr) {
            const auto* q = reinterpret_cast<const std::uintptr_t*>(parameters);
            p0 = q[0]; p1 = q[1]; p2 = q[2]; p3 = q[3]; p4 = q[4];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
    char lb[160];
    std::snprintf(lb, sizeof(lb), "pe fn=0x%llX obj=0x%llX %s p=%llX,%llX,%llX,%llX,%llX",
                  (unsigned long long)function, (unsigned long long)object, fname, p0, p1, p2, p3, p4);
    PushLog(lb);
}


void UnwatchProcessEvent() {
    if (g_pe_state != 1 || g_pe_svc == nullptr) { return; }
    const auto* svc = static_cast<const AnomalyUe5ProcessEventServiceV1*>(g_pe_svc);
    if (svc->unsubscribe) {
        const AnomalyStatusV1 st = svc->unsubscribe(svc->user, g_pe_handle);
        char lb[64];
        std::snprintf(lb, sizeof(lb), "pe: unwatch rc=%u", static_cast<unsigned>(st.code));
        PushLog(lb);
    }
    g_pe_state = 0;
}

void StartProcessEventWatch() {
    if (g_pe_state == 1 || !g_ctx) return;
    if (g_pe_svc == nullptr) {
        g_pe_svc = LazyService(&g_ctx->process_event, "anomaly.ue5.process-event", 1);
    }
    if (g_pe_svc == nullptr) { char qb[64]; std::snprintf(qb, sizeof(qb), "pe: query rc=%d", g_query_rc); PushLog(qb); g_pe_state = -1; return; }
    const auto* svc = static_cast<const AnomalyUe5ProcessEventServiceV1*>(g_pe_svc);
    if (!svc->subscribe) { PushLog("pe: no subscribe"); g_pe_state = -1; return; }
    AnomalyGenerationHandleV1 h{};
    const AnomalyStatusV1 st = svc->subscribe(svc->user, &PeWatcher, nullptr, &h);
    if (st.code == ANOMALY_STATUS_V1_OK) { g_pe_handle = h; g_pe_state = 1; PushLog("pe: watching"); }
    else { g_pe_state = -1; PushLog("pe: subscribe failed"); }
}


// R2闂佹寧绋掑銊ッ洪崸妤€绠抽柕澶涚畱椤?GObjects闂佹寧绋戦張顒傗偓鍨矌閸掓帡宕滆閳ь剚锕㈠畷銉ヮ吋閸涱垱鎲婚梺?K2_ApplyGameEffectWithClassByActor
void ScanExactUfn() {
    int found = 0;
    for (unsigned int i = 0; i < 400000; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (nm[0] == 0) continue;
        if (std::strstr(nm, "K2_ApplyGameEffectWithClassByActor") != nullptr) {
            g_applyge_fn = reinterpret_cast<std::uintptr_t>(obj);
            char lb[128];
            std::snprintf(lb, sizeof(lb), "R2 HIT idx=%u ptr=0x%llX %s", i,
                          (unsigned long long)g_applyge_fn, nm);
            PushLog(lb);
            if (++found >= 3) break;
        }
    }
    char sb[96];
    std::snprintf(sb, sizeof(sb), "R2 done found=%d ufn=0x%llX", found,
                  (unsigned long long)g_applyge_fn);
    PushLog(sb);
}


// ===== 闂佸搫鍊藉Λ鍕叏閻愬搫绀勯柤鎭掑劜濞?hook闂佹寧绋戦悧鍛箔婢跺苯鏋?ProcessEvent闂佹寧绋戦懟顖毭哄鍐炬桨闁靛牆瀚悷婵堟喐閺夊灝鑸归柟?Tokky闂?====
using ApplyHookFn = void* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
static ApplyHookFn g_apply_hook_orig = nullptr;
static AnomalyGenerationHandleV1 g_apply_hook_handle{};
static const void* g_apply_hook_svc = nullptr;
static int g_apply_hook_state = 0;

static void* ApplyHookDetour(void* a1, void* a2, void* a3, void* a4,
                             void* a5, void* a6, void* a7, void* a8) {
    if (g_apply_hook_orig != nullptr) {
        static int logged = 0;
        if (logged < 120) {
            ++logged;
            const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const int from_tokky = (g_tokky_lo != 0 && caller >= g_tokky_lo && caller < g_tokky_hi) ? 1 : 0;
            char lb[176];
            std::snprintf(lb, sizeof(lb), "hook caller=0x%llX tokky=%d a1=%p a2=%p a3=%p a4=%p a5=%p",
                          (unsigned long long)caller, from_tokky, a1, a2, a3, a4, a5);
            PushLog(lb);
        }
        return g_apply_hook_orig(a1, a2, a3, a4, a5, a6, a7, a8);
    }
    return nullptr;
}

void HookLastApply() {
    if (g_apply_hook_state == 1) { PushLog("hook: already installed"); return; }
    if (g_last_pe_fn == 0) {
        PushLog("hook: no pe fn captured yet, auto-subscribing");
        StartProcessEventWatch();
        PushLog("hook: hit once, then press Hook apply again");
        return;
    }
    std::uintptr_t exec = 0;
    if (!ue::Reader::SafePtr(g_last_pe_fn + 0xD8, &exec) || exec < 0x10000) {
        PushLog("hook: no exec function");
        return;
    }
    if (g_apply_hook_svc == nullptr) {
        g_apply_hook_svc = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    }
    if (g_apply_hook_svc == nullptr) { PushLog("hook: service unavailable"); return; }
    const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_apply_hook_svc);
    AnomalyHookRequestV1 req{};
    req.struct_size = sizeof(req);
    req.kind = ANOMALY_HOOK_V1_FUNCTION;
    req.target = exec;
    req.detour = reinterpret_cast<void*>(&ApplyHookDetour);
    req.label = AnomalyStringViewV1{"ultimate.apply.dump", std::strlen("ultimate.apply.dump")};
    AnomalyGenerationHandleV1 h{};
    std::uintptr_t orig = 0;
    const auto st = svc->create(svc->user, &req, &orig, &h);
    char lb[128];
    std::snprintf(lb, sizeof(lb), "hook: exec=0x%llX rc=%u orig=0x%llX",
                  (unsigned long long)exec, static_cast<unsigned>(st.code), (unsigned long long)orig);
    PushLog(lb);
    if (st.code == ANOMALY_STATUS_V1_OK) {
        g_apply_hook_orig = reinterpret_cast<ApplyHookFn>(orig);
        g_apply_hook_handle = h;
        g_apply_hook_state = 1;
    }
}


// ===== 闂佺厧顨庢禍婊呯磽?ProcessEvent hook闂佹寧绋戝濡昦ble+0x260闂佹寧绋戞總鏃傜箔婢跺鐟规繝闈涳功椤╊偊鎮楅悷鑸粧閻庨潧鐬肩划鐢稿箚瑜庨崐鎶芥煥?====
using PeDirectFn = void* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
static PeDirectFn g_pe2_orig = nullptr;
static AnomalyGenerationHandleV1 g_pe2_handle{};
static const void* g_pe2_svc = nullptr;
static int g_pe2_state = 0;
static int g_pe2_logged = 0;

static void* PeDirectDetour(void* a1, void* a2, void* a3, void* a4,
                            void* a5, void* a6, void* a7, void* a8) {
    if (g_pe2_orig == nullptr) return nullptr;
    const std::uintptr_t fn = reinterpret_cast<std::uintptr_t>(a2);
    if (fn >= 0x10000 && g_pe2_logged < 200) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (ue::Reader::SafeU32(fn + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        if (std::strstr(nm, "ApplyGameEffect") != nullptr ||
            std::strstr(nm, "ApplyGameplayEffect") != nullptr) {
            ++g_pe2_logged;
            const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
            const int from_tokky = (g_tokky_lo != 0 && caller >= g_tokky_lo && caller < g_tokky_hi) ? 1 : 0;
            char lb[176];
            std::snprintf(lb, sizeof(lb), "pe2 fn=0x%llX obj=%p tokky=%d %s",
                          (unsigned long long)fn, a1, from_tokky, nm);
            PushLog(lb);
        }
    }
    return g_pe2_orig(a1, a2, a3, a4, a5, a6, a7, a8);
}

void HookProcessEventDirect() {
    if (g_pe2_state == 1) { PushLog("pe2: already hooked"); return; }
    // 闂佸憡鐟﹂悧鏃傜博鐎涙鈻旀い蹇撴噺缁犳帡鏌℃担鍝ュ妞ゆ洦鍠氶幐鎺楀煡閸涱垳顦柣?[obj] -> vtable -> +0x260
    std::uintptr_t pe_addr = 0;
    for (unsigned int i = 0; i < 2000 && pe_addr == 0; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        std::uintptr_t vt = 0;
        if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(obj), &vt) || vt < 0x10000) continue;
        std::uintptr_t f = 0;
        if (!ue::Reader::SafePtr(vt + 0x260, &f) || f < 0x10000) continue;
        pe_addr = f;
    }
    if (pe_addr == 0) { PushLog("pe2: cannot locate ProcessEvent"); return; }
    if (g_pe2_svc == nullptr) {
        g_pe2_svc = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    }
    if (g_pe2_svc == nullptr) { PushLog("pe2: hook service unavailable"); return; }
    const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_pe2_svc);
    AnomalyHookRequestV1 req{};
    req.struct_size = sizeof(req);
    req.kind = ANOMALY_HOOK_V1_FUNCTION;
    req.target = pe_addr;
    req.detour = reinterpret_cast<void*>(&PeDirectDetour);
    req.label = AnomalyStringViewV1{"ultimate.pe.direct", std::strlen("ultimate.pe.direct")};
    AnomalyGenerationHandleV1 h{};
    std::uintptr_t orig = 0;
    const auto st = svc->create(svc->user, &req, &orig, &h);
    char lb[128];
    std::snprintf(lb, sizeof(lb), "pe2: pe=0x%llX rc=%u orig=0x%llX",
                  (unsigned long long)pe_addr, static_cast<unsigned>(st.code), (unsigned long long)orig);
    PushLog(lb);
    if (st.code == ANOMALY_STATUS_V1_OK) {
        g_pe2_orig = reinterpret_cast<PeDirectFn>(orig);
        g_pe2_handle = h;
        g_pe2_state = 1;
    }
}


// ===== 婵炲顑曢崐鏇㈩敊婵犲啰鈻斿┑鐘插暙琚氶柣鐔哥懃閸婃悂鎯傞崒鐐存櫖婵炲棛鐫扐bilityCharacter.BroadcastCharacterOnDamaged (RVA 0x7CB1710) =====
// 闂佸憡鐟禍锝夘敋娴煎瓨鐒婚煫鍥ф捣閻愬﹪鏌ㄥ☉妯侯殭缂佹顦遍崰?Apply / ProcessEvent闂佹寧绋戦懟顖毭哄鍐炬桨闁靛牆瀚悷婵嗏槈?Tokky 闂佸憡鍔樼亸娆撴偘婵犲洤违?
static PeDirectFn g_dmg_orig = nullptr;
static AnomalyGenerationHandleV1 g_dmg_handle{};
static int g_dmg_state = 0;
static int g_dmg_logged = 0;

static void* DmgBroadcastDetour(void* a1, void* a2, void* a3, void* a4,
                                void* a5, void* a6, void* a7, void* a8) {
    if (g_dmg_orig == nullptr) return nullptr;
    if (g_dmg_logged < 120) {
        ++g_dmg_logged;
        const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        const int from_tokky = (g_tokky_lo != 0 && caller >= g_tokky_lo && caller < g_tokky_hi) ? 1 : 0;
        char lb[160];
        std::snprintf(lb, sizeof(lb), "dmg caller=0x%llX tokky=%d a1=%p a2=%p a3=%p a4=%p",
                      (unsigned long long)caller, from_tokky, a1, a2, a3, a4);
        PushLog(lb);
    }
    return g_dmg_orig(a1, a2, a3, a4, a5, a6, a7, a8);
}

void HookDamageBroadcast() {
    if (g_dmg_state == 1) { PushLog("dmg: already hooked"); return; }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { PushLog("dmg: no module"); return; }
    const std::uintptr_t target = base + 0x7CB1710;
    if (g_pe2_svc == nullptr) {
        g_pe2_svc = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    }
    if (g_pe2_svc == nullptr) { PushLog("dmg: hook service unavailable"); return; }
    const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_pe2_svc);
    AnomalyHookRequestV1 req{};
    req.struct_size = sizeof(req);
    req.kind = ANOMALY_HOOK_V1_FUNCTION;
    req.target = target;
    req.detour = reinterpret_cast<void*>(&DmgBroadcastDetour);
    req.label = AnomalyStringViewV1{"ultimate.dmg.broadcast", std::strlen("ultimate.dmg.broadcast")};
    AnomalyGenerationHandleV1 h{};
    std::uintptr_t orig = 0;
    const auto st = svc->create(svc->user, &req, &orig, &h);
    char lb[128];
    std::snprintf(lb, sizeof(lb), "dmg: target=0x%llX rc=%u orig=0x%llX",
                  (unsigned long long)target, static_cast<unsigned>(st.code), (unsigned long long)orig);
    PushLog(lb);
    if (st.code == ANOMALY_STATUS_V1_OK) {
        g_dmg_orig = reinterpret_cast<PeDirectFn>(orig);
        g_dmg_handle = h;
        g_dmg_state = 1;
    }
}


// ===== 闁?Tokky 闂佺厧顨庢禍婊呮崲娓氣偓閹?detour闂佹寧绋掗濯漰ly 缂備焦姊归悷褔宕戦敐鍡欌枖濠电姴瀚悾?JMP 闂佺儵鏅╅崰妤呮偉閿濆鏅柛顐ｇ箓濞懷囨偣閸ワ妇涓茬紒杈ㄧ箖缁嬪顓奸崼顐ｎ棅 hook闂?====
void DumpTokkyDetour() {
    if (!g_ctx) return;
    const void* core_svc = LazyService(&g_ctx->core, "anomaly.core", 1);
    if (!core_svc) { PushLog("tkjmp: core unavailable"); return; }
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(core_svc);
    if (!core->read_memory) { PushLog("tkjmp: no read_memory"); return; }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { PushLog("tkjmp: no module"); return; }
    const std::uintptr_t site = base + 0x18AACF0;

    unsigned char head[16] = {0};
    AnomalyMutableByteSpanV1 hs{head, sizeof(head)};
    if (core->read_memory(core->user, site, hs).code != ANOMALY_STATUS_V1_OK) {
        PushLog("tkjmp: read site failed");
        return;
    }
    {
        char bb[160];
        std::snprintf(bb, sizeof(bb), "tkjmp2: bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
                      head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
        PushLog(bb);
        MEMORY_BASIC_INFORMATION ms{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(site), &ms, sizeof(ms)) != 0) {
            char sb[160];
            std::snprintf(sb, sizeof(sb), "tkjmp2: site State=0x%X Protect=0x%X Type=0x%X Alloc=0x%llX",
                          static_cast<unsigned>(ms.State), static_cast<unsigned>(ms.Protect),
                          static_cast<unsigned>(ms.Type), (unsigned long long)reinterpret_cast<std::uintptr_t>(ms.AllocationBase));
            PushLog(sb);
        }
    }    if (head[0] != 0xE9) {
        char hb[96];
        std::snprintf(hb, sizeof(hb), "tkjmp: not jmp (op=%02X %02X %02X)", head[0], head[1], head[2]);
        PushLog(hb);
        return;
    }
    const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(head + 1);
    const std::uintptr_t target = site + 5 + static_cast<std::intptr_t>(rel);

    unsigned char code[512] = {0};
    AnomalyMutableByteSpanV1 cs{code, sizeof(code)};
    if (core->read_memory(core->user, target, cs).code != ANOMALY_STATUS_V1_OK) {
        PushLog("tkjmp: read target failed");
        return;
    }

    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) return;
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) return;
    *slash = 0;
    char file[MAX_PATH + 32];
    std::snprintf(file, sizeof(file), "%s\\tokky_detour.bin", path);
    FILE* fp = std::fopen(file, "wb");
    if (fp == nullptr) { PushLog("tkjmp: fopen failed"); return; }
    struct Header { std::uintptr_t base, site, target; } hdr{base, site, target};
    std::fwrite(&hdr, sizeof(hdr), 1, fp);
    std::fwrite(code, sizeof(code), 1, fp);
    std::fclose(fp);

    char lb[128];
    std::snprintf(lb, sizeof(lb), "tkjmp: site=0x%llX target=0x%llX saved",
                  (unsigned long long)site, (unsigned long long)target);
    PushLog(lb);
}

// ===== Tokky 闂佸憡鐟禍锝夘敋娴兼潙绠抽柕蹇嬪€栫€?=====
// 闂備緡鍋呮穱铏规崲閸愵亖鍋撻悷鏉挎殺闁搞倖绮岄蹇旀綇閳哄倻鏆犻梺鍝勫暔閸庤京鎹㈤弮鍌楀亾濞戞瑯娈樻い鎴滅劍缁嬪骞撻幒鏃€姣庢繛?闂傚倵鍋撴繝濠傚暙閸撳ジ鏌涚€ｎ亞绠虫禍?闂?濠碘槅鍨埀顒冩珪閸嬨儵鏌涚€ｎ亞绠虫禍?闂佹眹鍔岀€氼剙螣婵犲洤纾圭痪顓㈩棑缁€?
// 闂佸憡鍔曠粔闈涚暤娴ｅ壊娼伴柕鍫濈墱閸ゃ垽鏌涘▎鎰ⅹ闁伙絿濮风槐鎾诲箻瀹曞洦鎲奸梺?UFunction 闂佸湱顭堝ú顓㈠箯閿熺姵鏅柛顐犲劜濞堟棃鏌?0x3CE78闂佹寧绋戦張顒€顭囬悽鍛婂殜?0x77C8 婵?mov [rip+0x356a9]闂佹寧绋戦ˇ顓㈠焵?
static std::uintptr_t g_tokky_base = 0;
static std::uintptr_t g_tokky_ufn = 0;
static int g_tokky_state = 0;

void ProbeTokkyDump() {
    // 1) 闂佺绻愰悧濠勨偓鍨矊铻ｉ柍銉ㄦ珪閸嬨儵鏌＄仦璇插姕濠?
    HMODULE mod = GetModuleHandleA("Tokky-NTE.dll");
    std::uintptr_t found = 0;
    if (mod != nullptr) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mod);
        g_tokky_base = base;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const std::size_t image_size = nt->OptionalHeader.SizeOfImage;
        const char needle[] = "multi_hit: burst_rva=0x%X";
        __try {
            const auto* p = reinterpret_cast<const unsigned char*>(base);
            for (std::size_t i = 0; i + sizeof(needle) < image_size; ++i)
                if (p[i] == 'm' && std::memcmp(p + i, needle, sizeof(needle) - 1) == 0) { found = base + i; break; }
        } __except (EXCEPTION_EXECUTE_HANDLER) { found = 0; }
        char mb[96];
        std::snprintf(mb, sizeof(mb), "tokky: module base=0x%llX", (unsigned long long)base);
        PushLog(mb);
    } else {
        PushLog("tokky: module not loaded, scanning memory");
        // 闂佸湱鍎ょ敮鈥斥枍鎼淬劍鍤婃い蹇撴喘閻撯晜淇婇妞诲亾閾忣偄浠撮梺鎸庣☉閻倹瀵奸埡鍛鐎广儱鎳忛崵鎺戔槈閹垮啫骞楅柛銊︾矋缁傛帡顢楅崘銊︻唶閻庤鐡曟禍顒€鈻撻幋锔藉亱闁荤喐澹嗙粣娆忊槈閹炬潙绲婚柣鈯欏洦顥堥柕蹇嬪€曞▍?
        struct Range { std::uintptr_t lo, hi; };
        Range ex[4] = {};
        int exn = 0;
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&ProbeTokkyDump), &self) != 0) {
            const auto* d = reinterpret_cast<const IMAGE_DOS_HEADER*>(self);
            const auto* n = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<std::uintptr_t>(self) + d->e_lfanew);
            ex[exn++] = { reinterpret_cast<std::uintptr_t>(self),
                          reinterpret_cast<std::uintptr_t>(self) + n->OptionalHeader.SizeOfImage };
        }
        const char* const names[3] = { nullptr, "Anomaly.Core.dll", "Tokky-NTE.dll" };
        for (int k = 0; k < 3; ++k) {
            HMODULE m = (k == 0) ? GetModuleHandleA(nullptr) : GetModuleHandleA(names[k]);
            if (m == nullptr) continue;
            const auto* d = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
            const auto* n = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<std::uintptr_t>(m) + d->e_lfanew);
            if (exn < 4) ex[exn++] = { reinterpret_cast<std::uintptr_t>(m),
                                       reinterpret_cast<std::uintptr_t>(m) + n->OptionalHeader.SizeOfImage };
        }

        const char needle[] = "multi_hit: burst_rva=0x%X";
        const char marker[] = "MultiHitFeature";
        std::uintptr_t addr = 0x10000;
        int regions = 0;
        while (addr < 0x7FFFFFFF0000ULL && regions < 400000 && found == 0) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) break;
            const std::uintptr_t rbase = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const std::size_t rsize = mbi.RegionSize;
            if (rsize == 0) break;
            bool skip = false;
            for (int k = 0; k < exn; ++k)
                if (rbase < ex[k].hi && rbase + rsize > ex[k].lo) { skip = true; break; }
            if (!skip && mbi.State == MEM_COMMIT &&
                (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READWRITE)) != 0) {
                ++regions;
                std::uintptr_t cand = 0;
                __try {
                    const auto* p = reinterpret_cast<const unsigned char*>(rbase);
                    for (std::size_t i = 0; i + sizeof(needle) < rsize; ++i)
                        if (p[i] == 'm' && std::memcmp(p + i, needle, sizeof(needle) - 1) == 0) { cand = rbase + i; break; }
                } __except (EXCEPTION_EXECUTE_HANDLER) { cand = 0; }
                if (cand != 0) {
                    // 闂傚倸娲犻崑鎾绘偡?闁?4KB 闂佸憡鍔曢幊搴ㄦ偤閵娾晛鎹?Tokky 闂佺粯鐟崜姘耿娓氣偓閹?RTTI 闂佸憡鑹剧粙鍕濠靛绠抽柟鐑橆殕閻濈喖鏌涚€ｎ亝鍠樻俊鎻掋偢楠炩偓?
                    const std::uintptr_t lo = (cand > 0x10000) ? cand - 0x10000 : 0;
                    bool ok = false;
                    __try {
                        for (std::uintptr_t q = lo; q + sizeof(marker) < cand + 0x10000; ++q) {
                            const auto* b = reinterpret_cast<const unsigned char*>(q);
                            if (b[0] == 'M' && std::memcmp(b, marker, sizeof(marker) - 1) == 0) { ok = true; break; }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                    if (ok) found = cand;
                }
            }
            addr = rbase + rsize;
        }
        char sb[96];
        std::snprintf(sb, sizeof(sb), "tokky: scan done regions=%d found=0x%llX", regions, (unsigned long long)found);
        PushLog(sb);
    }
    if (found == 0) { PushLog("tokky: signature not found"); g_tokky_state = -3; return; }
    g_tokky_lo = (found > 0x400000) ? found - 0x400000 : 0x10000;
    g_tokky_hi = found + 0x1000000;
    // 闂侀潻璐熼崝宀勫礄閿涘嫧鍋撶憴鍕叝缂傚秴鎳橀幆?Tokky 闂傚倵鍋撴繝濠傚暙閸撳ジ姊洪幓鎺撴拱閻庡灚绮嶇粋鎺楁晲閸モ晛鐏ｉ梺缁橆殘閹虫挾娆㈠☉銏犵闁诡垎鍛疁闂佹眹鍔岀€氼喚妲愰敂閿亾濞戞顏堝疮閹捐绀傞柕澶樺灣缁愭ov [rip+disp], rbx  (48 89 1D ...)
    auto plausible = [](std::uintptr_t v) -> bool {
        return v >= 0x10000 && v <= 0x7FFFFFFFFFFFULL && (v & 7) == 0;
    };
    std::uintptr_t ufn = 0;
    std::uintptr_t slot = 0;
    __try {
        const auto* p = reinterpret_cast<const unsigned char*>(found > 0x200000 ? found - 0x200000 : 0);
        const std::uintptr_t scan_lo = (found > 0x200000) ? found - 0x200000 : 0x10000;
        for (std::uintptr_t i = scan_lo; i < found + 0x200000; ++i) {
            const auto* b = reinterpret_cast<const unsigned char*>(i);
            if (b[0] != 0x48 || b[1] != 0x89 || b[2] != 0x1D) continue;
            const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(i + 3);
            const std::uintptr_t target = i + 7 + static_cast<std::intptr_t>(disp);
            if (target < 0x10000 || target > 0x7FFFFFFFFFFFULL) continue;
            std::uintptr_t v = 0;
            __try { v = *reinterpret_cast<std::uintptr_t*>(target); }
            __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
            if (!plausible(v)) continue;
            std::uintptr_t vt = 0;
            __try { vt = *reinterpret_cast<std::uintptr_t*>(v); }
            __except (EXCEPTION_EXECUTE_HANDLER) { vt = 0; }
            if (!plausible(vt)) continue;
            slot = target;
            ufn = v;
            break;
        }
        (void)p;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ufn = 0; slot = 0; }

    if (ufn == 0) {
        PushLog("tokky: no valid ufn slot (kept 0)");
        g_tokky_state = 0;
        return;
    }
    g_tokky_ufn = ufn;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "tokky slot=0x%llX ufn=0x%llX",
                  (unsigned long long)slot, (unsigned long long)ufn);
    PushLog(buf);
    g_applyge_fn = ufn;
    g_tokky_state = 1;
    PushLog("tokky: ufn adopted");

}
void TickSweep() {
    if (!g_sweep_on) return;
    g_probe_on = 1;                                  // 婵炴垶鎸撮崑鎾绘⒑濞嗘儳娅嶅ù婊冪埣瀹曘垽宕卞顒侇唶闂佸憡鏌ｉ崝宀€妲愰幋锕€瑙︽い鏍ㄧ☉椤ュ洭鏌熺拠鏌ョ崪缂佽鲸绻堝顕€鎮╃紒妯圭帛闂佸憡鍔曠粔鍨櫠濠婂牆绀夐柕濞垮劚閻熷酣鏌熼幁鎺戠伈闁?
    if (!g_fn_ok || !g_ctx || !g_ctx->functions) { g_sweep_on = 0; PushLog("sweep abort: no service"); return; }
    if (g_ge_count == 0) return;                     // 缂備焦绋戦ˇ鐢搞€呯紒妯诲妞ゆ棁妫勯ˉ鍥煙鐠囪尙绠伴柛?GE 濠电偞鎸搁幊搴＄暦?
    // 婵炴潙鍚嬮敋闁告ɑ鐩獮?Tokky 闂佹眹鍔岀€氼參寮婚悢濂夊殨闊洢鍎崇粈鍕磼?Children + 闂佸憡鑹剧粔鎾偤瑜旈弫宥嗗緞楠烆喗甯″?K2_ApplyGameEffectWithClassByActor闂佹寧绋戦懟顖炴嚐閻旂儤鍋橀柕澶堝妿缁犲鏌涙繝鍐噰闁?
    if (g_sweep_fn == 0 && g_sweep_ge == 0) {
        if (ResolveApplyUfn() != 0) {
            bool present = false;
            for (int k = 0; k < g_fn_count; ++k)
                if (g_fn_ptrs[k] == g_applyge_fn) { present = true; break; }
            if (!present && g_fn_count < 32) {
                std::snprintf(g_fn_names[g_fn_count], sizeof(g_fn_names[0]), "%s",
                              "K2_ApplyGameEffectWithClassByActor");
                g_fn_ptrs[g_fn_count] = g_applyge_fn;
                ++g_fn_count;
                char hb[96];
                std::snprintf(hb, sizeof(hb), "k2 added as f%d/%d ptr=0x%llX", g_fn_count, g_fn_count,
                              (unsigned long long)g_applyge_fn);
                PushLog(hb);
            }
        } else {
            PushLog("k2 resolve failed");
        }
        if (g_asc_n == 0 && g_ta_obj == 0) {
            g_sweep_on = 0;
            PushLog("sweep abort: no receiver asc");
            return;
        }
    }
    const int fn_total = (g_fn_count > 0) ? g_fn_count : 1;
    if (g_sweep_fn >= fn_total || g_sweep_fn < 0) {
        g_sweep_on = 0;
        PushLog("sweep done");
        return;
    }
    if (g_sweep_ge == 0) {
        if (g_fn_count > 0) {
            g_applyge_fn = g_fn_ptrs[g_sweep_fn];
            char hb[96];
            std::snprintf(hb, sizeof(hb), "ufn %d/%d %s", g_sweep_fn + 1, fn_total, g_fn_names[g_sweep_fn]);
            PushLog(hb);
        } else {
            ResolveApplyUfn();
            char hb[96];
            std::snprintf(hb, sizeof(hb), "ufn resolved=0x%llX", (unsigned long long)g_applyge_fn);
            PushLog(hb);
            if (g_applyge_fn == 0) { g_sweep_on = 0; PushLog("sweep abort: no ufunction"); return; }
        }
    }
    const int gi = g_sweep_ge;
    g_ge_sel = gi;
    TryInvokeApplyGE();
    char lb[96];
    std::snprintf(lb, sizeof(lb), "sweep f%d/%d g%d/%d iv=%d %s",
                  g_sweep_fn + 1, fn_total, gi + 1, g_ge_count, g_invoke_rc, g_ge_names[gi]);
    PushLog(lb);
    if (++g_sweep_ge >= g_ge_count) { g_sweep_ge = 0; ++g_sweep_fn; }
}

void TryInvokeApplyGE() {
    if (!g_fn_ok || !g_ctx || !g_ctx->functions) { g_invoke_rc = -1; return; }
    if (g_ge_sel < 0 || g_ge_sel >= g_ge_count) { g_invoke_rc = -2; return; }
    const std::uintptr_t receiver = (g_asc_sel >= 0 && g_asc_sel < g_asc_n)
        ? g_asc[g_asc_sel]
        : ((g_asc_n > 0) ? g_asc[0] : g_ta_obj);
    if (receiver == 0) { g_invoke_rc = -3; return; }
    ClassNameOf(reinterpret_cast<void*>(receiver), g_recv_cls, sizeof(g_recv_cls));
    // UFunction 闂佹眹鍨兼ご鍝ユ暜娴煎瓨鐓㈤柛顐ゅ枎椤ュ洭鏌熺拠鑼閻庡灚绮庨埀顒傛暩椤㈠﹪鎸呴崟顖氳Е鐎广儱娲︾痪顖炴煙閹帒鍔滃┑顔煎瀹曪綁寮介悽闈涜€垮┑顔界缚閸婃洟藝閳哄倻鈻旂€广儱鎳庨弲娆撴偣瑜庢竟鍡欐偖?Children 闂佸搫琚崕鍙夌珶?
    if (g_applyge_fn == 0) { g_invoke_rc = -4; return; }
    const auto* fn = static_cast<const AnomalyUe5FunctionsServiceV1*>(g_ctx->functions);
    if (!fn->invoke) { g_invoke_rc = -5; return; }
    // HTAbilitySystemComponent_K2_ApplyGameEffectWithClassByActor闂佹寧绋戝鎭p 0x0078闂佹寧绋戦¨鈧紒?
    //   0x00 SourceObj(AActor*) | 0x08 GameplayEffect(TSubclassOf) | 0x10 ModifyData(FBufferData 0x58)
    //   0x68 pTriggerAbilityComp(UHTAbilitySystemComponent*) | 0x70 ReturnValue(bool)
    unsigned char params[0x78] = {0};
    void* ge = reinterpret_cast<void*>(g_ge_ptrs[g_ge_sel]);
    void* target = reinterpret_cast<void*>(receiver);
    void* source = nullptr;
    std::memcpy(params + 0x00, &source, sizeof(source));
    std::memcpy(params + 0x08, &ge, sizeof(ge));
    std::memcpy(params + 0x68, &target, sizeof(target));
    const AnomalyStatusV1 st = fn->invoke(fn->user, receiver, g_applyge_fn, params, sizeof(params));
    g_invoke_rc = static_cast<int>(st.code);
}

void ProbeScanStep() {
    static unsigned int cursor = 0;
    static std::uintptr_t tmp[64];
    static int tmp_n = 0;
    static char tmp_cls[32] = {0};
    static int cooldown = 0;
    if (cooldown > 0) { --cooldown; return; }   // 婵炴垶鎸撮崑鎾诲级閻戝棗鏋熷鐟板€婚埀顒傛嚀閼活垶骞冨Δ鍛鐎瑰嫭婢樼粊閬嶆煥濞戞鐒稿ù灏栨櫊瀹曟顓奸崱妤冩Х缂傚倷绶￠崢钘夘嚕瑜旈弻鍛存偄缁嬫妫侀梺?
    const unsigned int kLimit = 400000u;
    if (cursor == 0) { tmp_n = 0; tmp_cls[0] = 0; }
    char cls_name[48];
    unsigned int budget = 2000u;
    while (budget-- > 0 && cursor < kLimit) {
        void* obj = SafeObjectAt(cursor++);
        if (!obj) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(obj) + 0x10, &cls) || cls == 0) continue;
        unsigned int cid = 0;
        if (!ue::Reader::SafeU32(cls + 0x18, &cid)) continue;
        DecodeName(cid, cls_name, sizeof(cls_name));
        // 闂佸搫鍟版慨闈涱焽椤栨稓顩烽柤鎼佹涧缁€瀣煠?K2_ApplyGameEffectWithClassByActor闂佹寧绋戦悧鍛箔婢跺鐟规繝闈涳功椤╊偆绱掗銉殭闁?闂佸憡鎸哥粔鍓佹閹寸偞浜ら柛銉ｅ妽婵垽鏌?
        {
            unsigned int kid = 0;
            char knm[64] = {0};
            if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &kid)) {
                DecodeName(kid, knm, sizeof(knm));
                if (std::strstr(knm, "K2_ApplyGameEffectWithClassByActor") != nullptr) {
                    g_applyge_fn = reinterpret_cast<std::uintptr_t>(obj);
                    if (g_fn_count < 32) {
                        bool dup = false;
                        for (int k = 0; k < g_fn_count; ++k)
                            if (std::strcmp(g_fn_names[k], knm) == 0) { dup = true; break; }
                        if (!dup) {
                            std::snprintf(g_fn_names[g_fn_count], sizeof(g_fn_names[0]), "%s", knm);
                            g_fn_ptrs[g_fn_count] = g_applyge_fn;
                            ++g_fn_count;
                        }
                    }
                }
            }
        }
        if (g_fn_count < 32) {
            unsigned int fid = 0;
            char fname[64] = {0};
            if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &fid)) {
                DecodeName(fid, fname, sizeof(fname));
                if (std::strcmp(cls_name, "Function") == 0 &&
                    (std::strstr(fname, "ApplyGameplayEffect") != nullptr ||
                     std::strstr(fname, "ApplyGameEffect") != nullptr) &&
                    std::strncmp(fname, "Default__", 9) != 0 && std::strchr(fname, '/') == nullptr) {
                    bool dup = false;
                    for (int k = 0; k < g_fn_count; ++k)
                        if (std::strcmp(g_fn_names[k], fname) == 0) { dup = true; break; }
                    if (!dup) {
                        std::snprintf(g_fn_names[g_fn_count], sizeof(g_fn_names[0]), "%s", fname);
                        g_fn_ptrs[g_fn_count] = reinterpret_cast<std::uintptr_t>(obj);
                        ++g_fn_count;
                    }
                }
                if (std::strcmp(cls_name, "Function") == 0 &&
                    std::strcmp(fname, "K2_ApplyGameEffectWithClassByActor") == 0)
                    g_applyge_fn = reinterpret_cast<std::uintptr_t>(obj);
            }
        }
        if (g_ge_count < 256) {
            unsigned int oid = 0;
            char oname[64] = {0};
            if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &oid)) {
                DecodeName(oid, oname, sizeof(oname));
                if (std::strncmp(oname, "GE_Player_", 10) == 0 && std::strstr(oname, "_C") != nullptr &&
                    (std::strstr(oname, "Damage") != nullptr || std::strstr(oname, "Dmg") != nullptr ||
                     std::strstr(oname, "HitBack") != nullptr) &&
                    std::strstr(oname, "Dot") == nullptr && std::strstr(oname, "Buff") == nullptr &&
                    std::strstr(oname, "Passive") == nullptr && std::strstr(oname, "Recover") == nullptr &&
                    std::strstr(oname, "Heal") == nullptr) {
                    bool dup = false;
                    for (int k = 0; k < g_ge_count; ++k)
                        if (std::strcmp(g_ge_names[k], oname) == 0) { dup = true; break; }
                    if (!dup) {
                        std::snprintf(g_ge_names[g_ge_count], sizeof(g_ge_names[0]), "%s", oname);
                        g_ge_ptrs[g_ge_count] = reinterpret_cast<std::uintptr_t>(obj);
                        ++g_ge_count;
                    }
                }
            }
        }
        if (std::strstr(cls_name, "AbilitySystemComponent") == nullptr) continue;
        {
            unsigned int oid = 0;
            char oname[64] = {0};
            if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &oid)) DecodeName(oid, oname, sizeof(oname));
            if (std::strncmp(oname, "Default__", 9) == 0) continue;   // 缂備緡鍋勯…椋庡垝椤栨粍濯奸柕鍫濇媼閸ょ娀鎮归悙鈹惧亾瀹曞洦鎲诲┑鐐茬墦濞佳囧矗?0闂佹寧绋戦惌渚€鎮滈敂鑺ヤ氦?
        }
        if (tmp_n < 64) {
            {
                unsigned int oid2 = 0;
                char onm[48] = {0};
                if (ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &oid2))
                    DecodeName(oid2, onm, sizeof(onm));
                std::snprintf(g_asc_names[tmp_n], sizeof(g_asc_names[0]), "%s", onm);
            }
            tmp[tmp_n++] = reinterpret_cast<std::uintptr_t>(obj);
            if (tmp_cls[0] == 0) std::snprintf(tmp_cls, sizeof(tmp_cls), "%s", cls_name);
        }
    }
    if (cursor >= kLimit) {
        cursor = 0;
        g_asc_n = tmp_n;
        for (int i = 0; i < tmp_n; ++i) g_asc[i] = tmp[i];
        std::snprintf(g_asc_cls, sizeof(g_asc_cls), "%s", tmp_cls);
        cooldown = 300;   // 闂佽顔栭崑鍕偩椤掍胶鈻旈柍褜鍓氬顏堫敊閸撗勬儯 5 缂備礁顦扮敮鎺楀疮閳ь剟鏌?
    }
}

// 闁荤姳鐒﹀妯肩礊瀹ュ瀚夐柍褜鍓氬濠氬箣濠婂拋浼囧┑鐐叉灤閵堝洦顔嶉梺娲绘娇閸斿本鏅堕悩宕団枖濠㈣泛顦甸崵瀣煕濞嗗繐鈧綊寮抽悢琛″亾閻㈡鏀伴柦鏍у閹啴宕熼鈽嗘蕉闂佸憡鑹剧粙鍕濞嗘挸鐭楅柡宥庡墰濡插牓鏌ｉ姀鈺冨帨缂佽鲸绻冪粙澶婎吋閸涱厽顥濇繛鎴炴尭妤犳悂宕归悾灞稿亾鐟欏嫮鍟茬紒?
static char g_last_cls[3][24] = {{0}};

static void ClassNameOf(void* object, char* out, std::size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    if (!object) { std::snprintf(out, cap, "null"); return; }
    const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(object);
    if (p < 0x10000) { std::snprintf(out, cap, "low"); return; }
    std::uintptr_t cls = 0;
    if (!ue::Reader::SafePtr(p + 0x10, &cls) || cls == 0) { std::snprintf(out, cap, "noclass"); return; }
    unsigned int id = 0;
    if (!ue::Reader::SafeU32(cls + 0x18, &id)) { std::snprintf(out, cap, "noname"); return; }
    char name[24] = {0};
    DecodeName(id, name, sizeof(name));
    std::snprintf(out, cap, "%s", name[0] ? name : "?");
}

static void* ApplyDetour(void* a1, void* a2, void* a3, void* a4,
                         void* a5, void* a6, void* a7, void* a8) {
    ++g_apply_hits;
    if (g_neg1_budget > 0) {
        --g_neg1_budget;
        if (HasNeg1Word(a1) || HasNeg1Word(a2) || HasNeg1Word(a3)) ++g_neg1_calls;
        else ++g_clean_calls;
        ClassNameOf(a1, g_last_cls[0], sizeof(g_last_cls[0]));
        ClassNameOf(a2, g_last_cls[1], sizeof(g_last_cls[1]));
        ClassNameOf(a3, g_last_cls[2], sizeof(g_last_cls[2]));
    }
    if (!g_apply_original) return nullptr;
    const bool hp_track = (g_probe_on && g_hp_budget > 0 && g_asc_n > 0);
    const float hp_before = hp_track ? SumHP() : 0.0f;
    g_gate_seen = 0;
    {
        std::uintptr_t t = 0;
        __try {
            if (a2 != nullptr) t = *reinterpret_cast<std::uintptr_t*>(
                reinterpret_cast<std::uintptr_t>(a2) + 0xa8);
        } __except (EXCEPTION_EXECUTE_HANDLER) { t = 0; }
        if (t >= 0x10000) {
            std::uintptr_t asc = 0;
            __try { asc = *reinterpret_cast<std::uintptr_t*>(t + 0x940); }
            __except (EXCEPTION_EXECUTE_HANDLER) { asc = 0; }
            g_ta_obj = (asc >= 0x10000) ? asc : t;
            ClassNameOf(reinterpret_cast<void*>(g_ta_obj), g_ta_cls, sizeof(g_ta_cls));
        }
    }
    {
        static int s_left = 1;
        if (s_left > 0) {
            --s_left;
            void* args[8] = {a1, a2, a3, a4, a5, a6, a7, a8};
            for (int i = 0; i < 8; ++i) {
                const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(args[i]);
                if (p < 0x10000) continue;
                std::uintptr_t q[4] = {0, 0, 0, 0};
                __try { std::memcpy(q, reinterpret_cast<const void*>(p), sizeof(q)); }
                __except (EXCEPTION_EXECUTE_HANDLER) { q[0] = 0; }
                std::uintptr_t cls = 0;
                ue::Reader::SafePtr(p + 0x10, &cls);
                char nm[40] = {0};
                if (cls >= 0x10000) {
                    unsigned int nid = 0;
                    if (ue::Reader::SafeU32(cls + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
                }
                char b[224];
                std::snprintf(b, sizeof(b), "args[%d]=0x%llX cls=%s q=%llX,%llX,%llX,%llX",
                              i, (unsigned long long)p, nm[0] ? nm : "?",
                              (unsigned long long)q[0], (unsigned long long)q[1],
                              (unsigned long long)q[2], (unsigned long long)q[3]);
                PushLog(b);
            }
            PushLog("args: dump done");
        }
    }    void* result = g_apply_original(a1, a2, a3, a4, a5, a6, a7, a8);
    if (g_gate_seen) ++g_dmg_true; else ++g_dmg_false;
    if (hp_track) {
        --g_hp_budget;
        const float hp_after = SumHP();
        if (hp_after < hp_before - 0.001f) ++g_hp_drop; else ++g_hp_same;
    }
    if (result == nullptr) ++g_ret_null; else ++g_ret_nonnull;
    if (a2 == nullptr || a3 == nullptr) ++g_null_args;
    if (g_ui_state.damage && g_replay_enabled) {
        // 闂佸憡鐟禍鐐测枍閵壯勫闁哄鐏濆Λ姗€鏌℃笟鈧。锔剧箔瀹€鍕睄閻犲搫鎼悡婵嬫煥濞戞瑧鈯曟俊鍙夋倐瀹曘儵鏁傞悙顒€鐒搁梺姹囧妼鐎氼剛绱撻幘鍓佸崥閻庯綆鍘剧粻楣冩⒑婢跺摜绛忕紒杈ㄧ箞閹?TickCombatEvents 闂佽　鍋撻悹鍝勬惈閻撳倿鏌ｉ鍡楁瀻闁汇倕瀚€靛ジ濡堕崪浣告暯婵炲瓨绮岄鍕枎閵忋倕瑙﹂幖绮光偓铏€柣?
        g_rq_args[0] = a1; g_rq_args[1] = a2; g_rq_args[2] = a3; g_rq_args[3] = a4;
        g_rq_args[4] = a5; g_rq_args[5] = a6; g_rq_args[6] = a7; g_rq_args[7] = a8;
        g_apply_ms = NowMs();
        g_armed_for_apply = 0;
    }
    return result;
}

void InstallApplyHook() {
    if (g_apply_state != 0 || !g_ctx) return;
    const void* svc = LazyService(&g_ctx->damage_chain, "anomaly.nte.damage-chain", 1);
    if (!svc) { g_apply_rc = -1; return; }
    const auto* dc = static_cast<const AnomalyNteDamageChainServiceV1*>(svc);
    if (!dc->apply_address) { g_apply_rc = -2; return; }
    std::uintptr_t addr = 0;
    if (dc->apply_address(dc->user, &addr).code != ANOMALY_STATUS_V1_OK || addr == 0) { g_apply_rc = -3; return; }
    // 闁诲酣娼х紞濠勭礊閸儱绀嗛柟娈垮枟閸╁倿鎮楅崷顓炰粶闁搞倝浜跺顐﹀级閸喖姹查梺鍛婄懕缂嶅洨妲愬鏉沷file 闂佺粯顨堥幊鎾舵濞戙垹鐭楁い鏍ㄧ箓閸樻挳鏌﹂埀顒勬寠婢跺鍚梺鍛婂灱婵倝寮抽悢鐓庣闁告稒娼欓崝銉╂煛娴ｈ绶查柣鈯欏洦鍤嶉柛灞剧懅椤撴椽鏌涘顒€鍤辩紒?
    {
        unsigned char head[32] = {0};
        __try { std::memcpy(head, reinterpret_cast<const void*>(addr), sizeof(head)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { head[0] = 0; }
        for (int k = 0; k + 1 < 32; ++k) {
            if (head[k] == 0x48 && head[k + 1] == 0x85) { addr += k; break; }
        }
        char ab[96];
        std::snprintf(ab, sizeof(ab), "apply: aligned=%d addr=0x%llX", 0, (unsigned long long)addr);
        PushLog(ab);
    }
    // 闁诲海鏁搁崢褔宕?apply 闂佸憡鐟ラ敃銈夊矗妤ｅ啫瑙﹂幖杈剧悼閺佹儳顭跨捄鍝勵伀闁诡喖锕畷娆撴嚍閵夛附顔嶉梺鎸庣☉婵傛梻娆㈤悽鐢电焿闁煎鍊曞▓鐘绘煕濞嗗繒校闁活亜婀辩槐鎾诲冀閻㈢數顦╅梺鍛婄懐娴滐綁顢氭导瀛樻櫖?
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
        struct Item { std::uintptr_t off; const char* name; };
        const Item items[4] = {
            {0,           "apply_main.bin"},
            {0x767EFD4,   "apply_gate.bin"},
            {0x7601E74,   "apply_ins.bin"},
            {0x775AD64,   "apply_get.bin"},
        };
        char path[MAX_PATH] = {0};
        if (base != 0 && GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
            char* slash = std::strrchr(path, '\\');
            if (slash) {
                *slash = 0;
                for (int i = 0; i < 4; ++i) {
                    const std::uintptr_t target = (items[i].off == 0) ? addr : (base + items[i].off);
                    if (target < 0x10000) continue;
                    char file[MAX_PATH + 40];
                    std::snprintf(file, sizeof(file), "%s\\%s", path, items[i].name);
                    FILE* fp = std::fopen(file, "wb");
                    if (!fp) continue;
                    struct H { std::uintptr_t a; std::uint32_t s; } h{target, 0x400};
                    std::fwrite(&h, sizeof(h), 1, fp);
                    __try { std::fwrite(reinterpret_cast<const void*>(target), 1, 0x400, fp); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { }
                    std::fclose(fp);
                }
                PushLog("apply: dumped 4 ranges");
            }
        }
    }    const void* hook_service = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    if (!hook_service) { g_apply_rc = -4; return; }
    const auto* hook = static_cast<const AnomalyHookServiceV1*>(hook_service);
    if (!hook->create) { g_apply_rc = -5; return; }
    AnomalyHookRequestV1 request{};
    request.struct_size = sizeof(request);
    request.kind = ANOMALY_HOOK_V1_FUNCTION;
    request.target = addr;
    request.detour = reinterpret_cast<void*>(&ApplyDetour);
    request.label = AnomalyStringViewV1{"ultimate.dc.apply", std::strlen("ultimate.dc.apply")};
    AnomalyGenerationHandleV1 handle{};
    std::uintptr_t original = 0;
    const auto status = hook->create(hook->user, &request, &original, &handle);
    g_apply_rc = static_cast<int>(status.code);
    if (status.code == ANOMALY_STATUS_V1_OK) {
        g_apply_original = reinterpret_cast<ApplyFn>(original);
        g_apply_handle = handle;
        g_apply_state = 1;
    } else {
        g_apply_state = 2;
    }
}

void DrawTabs(const AnomalyUiServiceV1* ui) {
    if (!ui->begin_tab_bar || !ui->begin_tab_item || !ui->end_tab_bar) return;
    AnomalyStringViewV1 bar_id{"ultimate-tabs", 14};
    if (ui->begin_tab_bar(ui->user, bar_id, 0) == 0) return;

    struct TabDef { const char* label; int len; };
    TabDef tabs[] = {
        {"\xE6\xBF\x80\xE6\xB4\xBB", 6},        // 濠电姷顣介崑鎾寸箾?
        {"\xE6\x97\xB6\xE5\x81\x9C", 6},        // 闂佸搫鍟冲▔娑欑?
        {"\xE5\x80\x8D\xE6\x94\xBB", 6},        // 闂佺锕ょ粔鐢稿极?
        {"\xE6\x8E\x92\xE7\x90\x83", 6},        // 闂佸湱鍎ょ敮鐐哄箖?
        {"GM", 2},
        {"\xE5\x80\x99\xE9\x80\x89", 6},   // 闂佺锕ラ悷鈺呭焵?
    };
    for (int t = 0; t < 6; ++t) {
        AnomalyStringViewV1 label{tabs[t].label, static_cast<std::size_t>(tabs[t].len)};
        if (ui->begin_tab_item(ui->user, label, nullptr, 0, 1) != 0) {
            if (t == 0) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c1, &g_ui_state.chronos);
                }
                if (ui->text) {
                    char buf[96];
                    int n = snprintf(buf, sizeof(buf), "world=%llX chr=%d", g_gworld_ptr, g_chronos_rc);
                    if (n > 0) { AnomalyStringViewV1 v{buf, static_cast<std::size_t>(n)}; ui->text(ui->user, v); }
                }
            } else if (t == 1) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c1, &g_ui_state.chronos);
                }
                for (int i = 0; i < 3; ++i) {
                    if (ui->text) {
                        char key[24] = {0};
                        VkName(g_hot_vk[i], key, sizeof(key));
                        char row[128];
                        int len = snprintf(row, sizeof(row), "%s  \xE5\xBF\xAB\xE6\x8D\xB7\xEF\xBC\x9A%s%s",
                                          kHotkeyLabels[i], key,
                                          g_hot_capture == i ? "  (\xE6\x8C\x89\xE9\x94\xAE\xE4\xB8\xAD)" : "");
                        if (len > 0) { AnomalyStringViewV1 v{row, static_cast<std::size_t>(len)}; ui->text(ui->user, v); }
                    }
                    if (ui->same_line) ui->same_line(ui->user, 0.0f, -1.0f);
                    if (ui->button) {
                        char rb[24];
                        int rn = snprintf(rb, sizeof(rb), "\xE6\x94\xB9\xE9\x94\xAE##hk%d", i);
                        AnomalyStringViewV1 rebind{rb, static_cast<std::size_t>(rn > 0 ? rn : 0)};
                        if (ui->button(ui->user, rebind, 48.0f, 0.0f)) {
                            g_hot_capture = i;   // 等待下一次按键
                        }
                    }
                }
                if (ui->text) {
                    char buf[160];
                    int n = snprintf(buf, sizeof(buf), "wn=%s pz=%llX pe=%llX rc=%d prc=%d", g_world_name, reinterpret_cast<unsigned long long>(g_pause_fn), g_pe_addr, g_chronos_rc, g_pawn_rc);
                    if (n > 0) { AnomalyStringViewV1 v{buf, static_cast<std::size_t>(n)}; ui->text(ui->user, v); }
                }
            } else if (t == 2) {
                DrawCaptureUi(ui);
            } else if (t == 3) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c1, &g_ui_state.volley);
                }
                if (ui->text) {
                    char buf[160];
                    int n = snprintf(buf, sizeof(buf), "rc=%d %s", g_volley_rc, g_volley_msg);
                    if (n > 0) { AnomalyStringViewV1 v{buf, static_cast<std::size_t>(n)}; ui->text(ui->user, v); }
                }
            } else if (t == 4) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c1, &g_ui_state.gm);
                }
                if (ui->separator) ui->separator(ui->user);
                for (int i = 0; i < g_gm_cmd_count; ++i) {
                    AnomalyStringViewV1 cmd{g_gm_cmd_names[i], std::strlen(g_gm_cmd_names[i])};
                    if (ui->button && ui->same_line && (i % 4) != 0) ui->same_line(ui->user, 0.0f, -1.0f);
                    if (ui->button && ui->button(ui->user, cmd, 0.0f, 0.0f)) g_gm_selected2 = i;
                }
                if (g_gm_selected2 >= 0 && g_gm_selected2 < g_gm_cmd_count && ui->text) {
                    char sel[96];
                    int sn = snprintf(sel, sizeof(sel), "selected: %s", g_gm_cmd_names[g_gm_selected2]);
                    if (sn > 0) { AnomalyStringViewV1 sv{sel, static_cast<std::size_t>(sn)}; ui->text(ui->user, sv); }
                }
            }
            if (t == 5) {
                if (ui->begin_child && ui->end_child && ui->button) {
                    AnomalyStringViewV1 lid{"##fnlist", 7};
                    if (true) {
for (int fi = 0; fi < g_fn_count; ++fi) {
                            AnomalyStringViewV1 nm{g_fn_names[fi], std::strlen(g_fn_names[fi])};
                            if (ui->button(ui->user, nm, 0.0f, 0.0f)) {
                                g_fn_sel = fi;
                                g_applyge_fn = g_fn_ptrs[fi];
                                char fb[96];
                                std::snprintf(fb, sizeof(fb), "fn=%d/sel=%d %s",
                                              g_fn_count, fi, g_fn_names[fi]);
                                PushLog(fb);
                            }
                        }                    }
                }
                if (ui->text) {
                    char fs[96];
                    int fn2 = snprintf(fs, sizeof(fs), "fn=count %d sel %d", g_fn_count, g_fn_sel);
                    if (fn2 > 0) { AnomalyStringViewV1 v{fs, static_cast<std::size_t>(fn2)}; ui->text(ui->user, v); }
                }
            }
            ui->end_tab_item(ui->user);
        }
    }
    ui->end_tab_bar(ui->user);
}

// 闁哄鏅滈崝姗€銆侀幋锕€绫嶉柤鎼佹涧鐠у瓨绻?entities/actors 闂佸搫鐗嗙粔瀛樻叏閻旂厧鍙婃い鏍ㄧ閸庡﹪鎮楀☉娅亜锕㈤鍫熸櫖闁割偅绻冮崕娆撴煕閺傝濡芥俊顐熸櫈缁犳稑螖閳ь剝銇愯瀵即顢涘▎鎴犳噽闂佷紮绲介張顒€锕㈤鐘辩剨閺夊牃鏅涘畷鏇㈡煛閸愩劌顣虫繛鍫熷灱缁犳盯顢曢妶鍛闂?
void ProbeExtraServices() {
    static int frame = 0;
    if (!g_ctx) return;
    if (g_ctx->entities && g_ctx->actors && g_ctx->nte_combat) return;
    if ((++frame % 30) != 0) return;
    LazyService(&g_ctx->nte_combat, "anomaly.nte.combat", 1);   // Demo 闂佸湱绮敮濠傗枎閵忥紕鈻曢柣鏂挎啞绗戝┑顕嗙到缁绘劙骞庡璺鸿摕闁靛鍨崇粈澶娾槈閹惧磭孝鐟滄妸鍥ф嵍?on_load
    LazyService(&g_ctx->entities, "anomaly.nte.entities", 1);
    LazyService(&g_ctx->actors, "anomaly.nte.actors", 1);
}


// ===== 闁荤姳璀﹂崹鎵?闂佹寧绋掓穱鍝劽归崱娑樼＜?Apply 闂佸憡鍔曢幊姗€宕曠€涙鈻旈柕鍫濆閸ゅ鎮跺鐓庝函闁汇劎鍠栧畷娆撴嚍閵夛附顔嶉梺?x775AD60 / 0x7780290闂?====
static int g_callee_done = 0;
void DumpApplyCallees() {
    if (g_callee_done || !g_ctx) return;
    const void* core_svc = LazyService(&g_ctx->core, "anomaly.core", 1);
    if (!core_svc) return;
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(core_svc);
    if (!core->read_memory) { g_callee_done = 1; return; }
    const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) return;
    const std::uintptr_t c1 = base + 0x775AD60;
    const std::uintptr_t c2 = base + 0x7780290;
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) { g_callee_done = 1; return; }
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) { g_callee_done = 1; return; }
    *slash = 0;
    char file[MAX_PATH + 32];
    std::snprintf(file, sizeof(file), "%s\\dc_callees.bin", path);
    FILE* fp = std::fopen(file, "wb");
    if (fp == nullptr) { g_callee_done = 1; return; }
    struct Header { std::uintptr_t base, c1, c2; } hdr{base, c1, c2};
    std::fwrite(&hdr, sizeof(hdr), 1, fp);
    unsigned char buf[512] = {0};
    const std::uintptr_t targets[2] = {c1, c2};
    for (int i = 0; i < 2; ++i) {
        std::memset(buf, 0, sizeof(buf));
        AnomalyMutableByteSpanV1 span{buf, sizeof(buf)};
        if (core->read_memory(core->user, targets[i], span).code != ANOMALY_STATUS_V1_OK)
            std::memset(buf, 0, sizeof(buf));
        std::fwrite(buf, sizeof(buf), 1, fp);
    }
    std::fclose(fp);
    g_callee_done = 1;
}

// 婵炴垶鎸撮崑鎾寸箾閸″繐澧查柍褜鍎搁崱妯笺偖闂佺鐭堥悡澶屾閻х晝ply 缂備焦姊归悷褔宕?/ apply 闂佸憡顭囬崰搴ㄥ吹闁秴鏋侀柟宄扮灱閸庢煡鏌?/ gate 闂?512 闁诲孩绋掗〃澶嬩繆?
// 闁汇埄鍨伴崯顐︽儑椤掑嫭鏅慨婵囩se(8)+apply(8)+apply_orig(8)+gate(8) + 512*3 = 1568 闁诲孩绋掗〃澶嬩繆?
static int g_dump512_done = 0;
static int g_dump512_rc = 0;
void DumpDamageChain512() {
    if (g_dump512_done || !g_ctx || !g_apply_original) return;
    const void* svc = LazyService(&g_ctx->damage_chain, "anomaly.nte.damage-chain", 1);
    if (!svc) return;
    const auto* dc = static_cast<const AnomalyNteDamageChainServiceV1*>(svc);
    if (!dc->apply_address || !dc->gate_address) { g_dump512_done = 1; g_dump512_rc = -1; return; }
    std::uintptr_t a = 0, g = 0;
    if (dc->apply_address(dc->user, &a).code != ANOMALY_STATUS_V1_OK) { g_dump512_done = 1; g_dump512_rc = -2; return; }
    if (dc->gate_address(dc->user, &g).code != ANOMALY_STATUS_V1_OK) { g_dump512_done = 1; g_dump512_rc = -3; return; }
    const void* core_svc = LazyService(&g_ctx->core, "anomaly.core", 1);
    if (!core_svc) { g_dump512_done = 1; g_dump512_rc = -1; return; }
    const auto* core = static_cast<const AnomalyCoreServiceV1*>(core_svc);
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) { g_dump512_done = 1; g_dump512_rc = -4; return; }
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) { g_dump512_done = 1; g_dump512_rc = -4; return; }
    *slash = 0;
    struct Header { std::uintptr_t base, apply, apply_orig, gate; } hdr{
        reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe")), a,
        reinterpret_cast<std::uintptr_t>(g_apply_original), g};
    char file[MAX_PATH + 32];
    std::snprintf(file, sizeof(file), "%s\\dc_targets512.bin", path);
    FILE* fp = std::fopen(file, "wb");
    if (fp == nullptr) { g_dump512_done = 1; g_dump512_rc = -4; return; }
    std::fwrite(&hdr, sizeof(hdr), 1, fp);
    unsigned char buf[512] = {0};
    const std::uintptr_t targets[3] = {a, reinterpret_cast<std::uintptr_t>(g_apply_original), g};
    for (int i = 0; i < 3; ++i) {
        std::memset(buf, 0, sizeof(buf));
        AnomalyMutableByteSpanV1 span{buf, sizeof(buf)};
        if (core->read_memory(core->user, targets[i], span).code != ANOMALY_STATUS_V1_OK)
            std::memset(buf, 0, sizeof(buf));
        std::fwrite(buf, sizeof(buf), 1, fp);
    }
    std::fclose(fp);
    g_dump512_done = 1;
    g_dump512_rc = 1;
}


// 闂備焦褰冮敃銉╁棘娓氣偓瀹曟濡搁埡浣稿Г闂佸湱顭堥崐鍧楀箰椤掍胶鈻旈幖娣€ら崥鍌炴⒒閸愩劍瀚呯紒杈ㄧ懇瀹曘儵顢曢妶鍛Р闂備焦褰冪粔宄懊归崶褉鏋旈悗锝庡墯濞堝矂鏌?
void ReleaseEverything() {
    ReleaseHooks();                              // Apply / Gate
    if (g_pe_state == 1) {                       // 闁诲海鎳撻妵妯尖偓?ProcessEvent 闁荤姳闄嶉崹钘壩?
        UnwatchProcessEvent();
    }
    if (g_apply_hook_state == 1 && g_apply_hook_svc != nullptr) {
        const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_apply_hook_svc);
        if (svc->release) svc->release(svc->user, g_apply_hook_handle);
        g_apply_hook_state = 0;
        g_apply_hook_orig = nullptr;
        PushLog("release: apply native hook");
    }
    if (g_pe2_state == 1 && g_pe2_svc != nullptr) {
        const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_pe2_svc);
        if (svc->release) svc->release(svc->user, g_pe2_handle);
        g_pe2_state = 0;
        g_pe2_orig = nullptr;
        PushLog("release: pe2 hook");
    }
    PushLog("release: done");
}


// 闂佺儵鏅涢悺銊ф暜鐎靛憡瀚氶悹鍥ㄥ絻缁插潡鏌ㄥ☉妯煎ⅲ缂佺粯顭囩划娆戔偓锝庝簻閺佸爼鏌ㄥ☉姗嗘缂佽鲸绻冪粙澶婎吋閸モ晝姊鹃柡澶嗘櫅濞层倝顢欓弽銊р枖?read_memory 闂佹眹鍔岀€氼垳鈧灚鐗犲畷鍫曞箻椤旂瓔鈧瑩鏌?
void DumpTokkyDetourDirect() {
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { PushLog("tkjmp2: no module"); return; }
    const std::uintptr_t site = base + 0x18AACF0;
    unsigned char head[16] = {0};
    __try { std::memcpy(head, reinterpret_cast<const void*>(site), sizeof(head)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { PushLog("tkjmp2: read site failed"); return; }
    {
        char bb[160];
        std::snprintf(bb, sizeof(bb), "tkjmp2: bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
                      head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
        PushLog(bb);
        MEMORY_BASIC_INFORMATION ms{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(site), &ms, sizeof(ms)) != 0) {
            char sb[160];
            std::snprintf(sb, sizeof(sb), "tkjmp2: site State=0x%X Protect=0x%X Type=0x%X Alloc=0x%llX",
                          static_cast<unsigned>(ms.State), static_cast<unsigned>(ms.Protect),
                          static_cast<unsigned>(ms.Type), (unsigned long long)reinterpret_cast<std::uintptr_t>(ms.AllocationBase));
            PushLog(sb);
        }
    }    if (head[0] != 0xE9) {
        char hb[96];
        std::snprintf(hb, sizeof(hb), "tkjmp2: not jmp (%02X %02X)", head[0], head[1]);
        PushLog(hb);
        return;
    }
    const std::int32_t rel = *reinterpret_cast<const std::int32_t*>(head + 1);
    const std::uintptr_t target = site + 5 + static_cast<std::intptr_t>(rel);
    {
        char tb[96];
        std::snprintf(tb, sizeof(tb), "tkjmp2: site=0x%llX target=0x%llX",
                      (unsigned long long)site, (unsigned long long)target);
        PushLog(tb);
    }
    unsigned char code[1024] = {0};
    {
        DWORD oldProt = 0;
        const std::uintptr_t page = target & ~0xFFFULL;
        const bool prot = VirtualProtect(reinterpret_cast<LPVOID>(page), 0x2000,
                                         PAGE_EXECUTE_READ, &oldProt) != 0;
        char pb[96];
        std::snprintf(pb, sizeof(pb), "tkjmp2: protect=%d old=0x%X", prot ? 1 : 0,
                      static_cast<unsigned>(oldProt));
        PushLog(pb);
        __try { std::memcpy(code, reinterpret_cast<const void*>(target), sizeof(code)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { PushLog("tkjmp2: read target failed (after protect)"); return; }
        if (prot) { DWORD tmp = 0; VirtualProtect(reinterpret_cast<LPVOID>(page), 0x2000, oldProt, &tmp); }
    }

    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) return;
    char* slash = std::strrchr(path, '\\');
    if (slash == nullptr) return;
    *slash = 0;
    char file[MAX_PATH + 32];
    std::snprintf(file, sizeof(file), "%s\\tokky_detour.bin", path);
    FILE* fp = std::fopen(file, "wb");
    if (fp == nullptr) { PushLog("tkjmp2: fopen failed"); return; }
    struct Header { std::uintptr_t base, site, target; } hdr{base, site, target};
    std::fwrite(&hdr, sizeof(hdr), 1, fp);
    std::fwrite(code, sizeof(code), 1, fp);
    std::fclose(fp);
    PushLog("tkjmp2: saved 1024 bytes");
}


// ===== 闂佺儵鏅涢悺銊ф暜閹绢喖妫橀柦妯侯槸椤絽鈽夐幘顖氫壕濠电偛妫屽Σ鍕濮楃kky 闂佸搫鐗嗛幖顐﹀春濡や礁绶炵€广儱鎳庨悡婵嬫煥?====
// 闂佹椿浜為崰搴ㄦ偪?ProcessEvent 闂佺绻堥崕杈亹濞戙垺鏅繛鍡欑潚Game.exe + 0x3E41360闂佹寧绋戝鎭p 婵°倗濮撮惌渚€鎯佹径鎰櫖閻忕偟鍋撳鎾绘偠?hook闂?
// 闁荤姴顑呴崯浼村极閵堝绀堢€广儱娲ㄩ懜?UFunction 缂?0x400闂佹寧绋戝┃淇嘑unction+0xB0]闂佹寧绋戦¨鈧紒杈ㄧ箘閹奉偊宕橀鍛闂佸憡鑹惧锔炬崲閺囥垹鍌ㄩ柣鏃傝ˉ閸?
using PeRealFn = void (*)(void*, void*, void*);
static int g_apply_direct_rc = -999;

static std::uintptr_t FindAbiCompClass() {
    unsigned int cid = FindNameIdByPath("/Script/HTGame.HTAbilitySystemComponent");
    void* cls = cid ? g_ue.FindClassById(cid) : nullptr;
    if (cls != nullptr) return reinterpret_cast<std::uintptr_t>(cls);
    // 闂佺绻戠划宀€鑺遍幎鑺ユ櫖婵﹩鍘奸ˉ鍥煙鐠囪尙绠抽悶姘煎亰瀹?
    for (unsigned int i = 0; i < 400000; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        char cn[48] = {0};
        ClassNameOf(obj, cn, sizeof(cn));
        if (std::strcmp(cn, "Class") != 0) continue;
        unsigned int nid = 0;
        char nm[48] = {0};
        if (!ue::Reader::SafeU32(reinterpret_cast<std::uintptr_t>(obj) + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "HTAbilitySystemComponent") == 0)
            return reinterpret_cast<std::uintptr_t>(obj);
    }
    return 0;
}


static int g_asc_scan_req = 0;
static unsigned int g_asc_cursor = 0;
static int g_asc_scan_done = 0;
static int g_asc_players = 0, g_asc_monsters = 0;
static int g_asc_dead = 0;
static std::uintptr_t g_monster_asc = 0;
static std::uintptr_t g_player_pawn = 0;   // MonScan 椤烘墜鎹炲埌鐨勭帺瀹?pawn锛堜笉渚濊禆 world鈫扜I 閾撅級
static std::uintptr_t g_player_source = 0;
static std::uintptr_t g_player_asc = 0;

static int g_asc_mode = 0;
static int g_rev_phase = 0;
static unsigned int g_rev_cursor = 0;
static int g_rev_found = 0;
static int g_rev_logged = 0;
void RequestAscScan() {
    g_asc_scan_req = 1; g_asc_cursor = 0; g_asc_scan_done = 0;
    g_asc_players = 0; g_asc_monsters = 0; g_asc_dead = 0; g_monster_asc = 0; g_player_source = 0;
    PushLog("ascscan: requested");
    if (g_asc_mode == 1) {
        g_rev_phase = 1; g_rev_cursor = 0; g_rev_found = 0; g_rev_logged = 0;
        g_monster_asc = 0; g_player_asc = 0;
        PushLog("ascscan: reverse start");
        return;
    }
}

void TickAscScan() {
    if (!g_asc_scan_req) return;
    const unsigned int kBatch = 20000, kLimit = 400000;
    const unsigned int end = (g_asc_cursor + kBatch < kLimit) ? (g_asc_cursor + kBatch) : kLimit;
    for (unsigned int i = g_asc_cursor; i < end; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &cls) || cls < 0x10000) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(cls + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "HTAbilitySystemComponent") != 0) continue;
        std::uintptr_t pc = 0, inst = 0, coi = 0, causer = 0, atk = 0;
        ue::Reader::SafePtr(o + 0x2620, &pc);
        ue::Reader::SafePtr(o + 0x2370, &inst);
        ue::Reader::SafePtr(o + 0x2390, &coi);
        ue::Reader::SafePtr(o + 0x2368, &causer);
        ue::Reader::SafePtr(o + 0x2388, &atk);
        const bool live = (inst >= 0x10000) || (coi >= 0x10000) || (causer >= 0x10000) || (atk >= 0x10000);
        if (!live) {
            ++g_asc_dead;
            if (g_asc_dead <= 5) {
                char db[160];
                std::snprintf(db, sizeof(db), "ascscan: dead#%d o=0x%llX c=0x%llX i=0x%llX co=0x%llX atk=0x%llX",
                              g_asc_dead, (unsigned long long)o, (unsigned long long)causer,
                              (unsigned long long)inst, (unsigned long long)coi, (unsigned long long)atk);
                PushLog(db);
            }
            continue;
        }
        if (pc >= 0x10000) { ++g_asc_players; if (g_player_asc == 0) g_player_asc = o;
            if (g_player_source == 0) g_player_source = (inst >= 0x10000) ? inst : coi; }
        else { ++g_asc_monsters; if (g_monster_asc == 0) g_monster_asc = o; }
    }
    g_asc_cursor = end;
    if (g_asc_cursor >= kLimit) {
        g_asc_scan_req = 0; g_asc_scan_done = 1;
        char lb[128];
        std::snprintf(lb, sizeof(lb), "ascscan: done player=%d monster=%d dead=%d monsterAsc=0x%llX src=0x%llX",
                      g_asc_players, g_asc_monsters, g_asc_dead,
                      (unsigned long long)g_monster_asc, (unsigned long long)g_player_source);
        PushLog(lb);
    }
}


// ===== 闂備焦褰冪粔鎾囬弻銉ユ闁芥ê顦～锝夋偣鐎ｎ亜鏆欓悗瑙勫▕閺佸秹宕煎鍡忔嫛濠殿喗绺块崕閬嶅储鐟欏嫭鍎熼柡鍥朵簽缁愭鎱ㄩ敐鍛妞ゆ劘鍋愰幏顐﹀礃椤忓懏娈㈤梻渚囧枦婵倝鎮ф惔銏╂鐎光偓閸愵亞顔夐梺鎼炲劤閸庢垹妲愬┑鍥х窞閺夊牜鍋夎闂佸憡顨呴崯鎸庣閻樼粯鏅?====
static int g_rep_on = 0;
static int g_rep_count = 0;
static int g_rep_max = 300;
static unsigned long long g_rep_last_ms = 0;
static unsigned int g_rep_interval_ms = 100;
static std::uintptr_t g_rep_recv = 0;
static std::uintptr_t g_rep_ufn = 0;
static void* g_rep_ge = nullptr;
static void* g_rep_source = nullptr;



static bool ApplyOnceCore(std::uintptr_t recv, std::uintptr_t ufn, void* ge, unsigned char* ret_out) {
    if (recv < 0x10000 || ufn < 0x10000 || ge == nullptr) return false;   // 缂備礁鑻幖顐﹀垂?
    unsigned char params[0x78] = {0};
    void* source = g_rep_source;
    std::memcpy(params + 0x00, &source, 8);
    void* gem = ge;
    std::memcpy(params + 0x08, &gem, 8);
    void* trigger = reinterpret_cast<void*>(recv);
    std::memcpy(params + 0x68, &trigger, 8);
    const int level = 1, stacks = 1;
    std::memcpy(params + 0x38, &level, sizeof(level));
    std::memcpy(params + 0x48, &stacks, sizeof(stacks));

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) return false;
    const std::uintptr_t pe = base + 0x3E41360;
    unsigned int saved = 0;
    const std::uintptr_t flags = ufn + 0xB0;
    if (!ue::Reader::SafeU32(flags, &saved)) return false;
    DWORD op = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op) == 0) return false;
    *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);
    reinterpret_cast<PeRealFn>(pe)(reinterpret_cast<void*>(recv), reinterpret_cast<void*>(ufn), params);
    DWORD op2 = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op2);
    *reinterpret_cast<unsigned int*>(flags) = saved;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);
    if (ret_out) *ret_out = params[0x70];
    return true;
}

void TickRepeat() {
    if (!g_rep_on) return;
    const unsigned long long now = NowMs();
    if (now - g_rep_last_ms < g_rep_interval_ms) return;
    g_rep_last_ms = now;
    if (g_rep_count >= g_rep_max) {
        g_rep_on = 0;
        PushLog("repeat: stop (max reached)");
        return;
    }
    unsigned char ret = 0;
    if (!ApplyOnceCore(g_rep_recv, g_rep_ufn, g_rep_ge, &ret)) {
        g_rep_on = 0;
        PushLog("repeat: stop (invalid args)");
        return;
    }
    ++g_rep_count;
    if (ret != 1) {                       // 婵炴垶鎸堕崐鏍敆閻愬搫绀傛繝濠傚暟娣囨椽鏌ㄥ☉娆庝孩缂佺粯鍨垮畷鍫曟倷閹殿喚鎲归梺鐟扮摠閸旀洘鎱ㄥ☉姘剨闁告鍋樼划?
        g_rep_on = 0;
        char lb[96];
        std::snprintf(lb, sizeof(lb), "repeat: stop (ret=%02X at %d)", ret, g_rep_count);
        PushLog(lb);
        return;
    }
    if ((g_rep_count % 10) == 1) {
        char lb[96];
        std::snprintf(lb, sizeof(lb), "repeat: %d/%d ok", g_rep_count, g_rep_max);
        PushLog(lb);
    }
}


// ===== 婵犮垼鍩栧銊︻殽閸ヮ剙绀傛繝濠傚暟娣囨椽鏌ㄥ☉妯煎ⅲ缂佽绉堕幃鎵沪閻愵剙鐒搁梺鍛婄懐閸ㄤ即宕硅箛娑樼妞ゎ偒鍠氱粈澶愭煛閸愵亜鈻堟繛鎾崇埣閺屽苯顓奸崱妯煎帎缂傚倸鍊归悧鐐烘儊瑜旈弫?====
// mode 0 = 缂備緡鍋夐褔骞冮弴銏犵妞ゆ劧绲芥导搴ㄦ煥濞戞澧㈡い鎴濇处缁嬪鍩€椤掑倻妫憸婵堟濮樿埖鏅繛宀€顨宒e 1 = 闁荤喐鐟︾敮鐔哥珶?+0x940 闂佸憡鐟ョ粔鐢告偂閿熺姵鏅柛顐犲焺閸庢垵霉濠婂啰鐏卞Δ鐘叉喘閺?
void CycleAscMode() {
    g_asc_mode = (g_asc_mode == 0) ? 1 : 0;
    char lb[64];
    std::snprintf(lb, sizeof(lb), "ascscan: mode=%d (%s)", g_asc_mode,
                  g_asc_mode == 0 ? "class-scan" : "reverse+0x940");
    PushLog(lb);
}

void TickReverseScan() {
    if (g_rev_phase != 1) return;
    const unsigned int kBatch = 20000, kLimit = 400000;
    const unsigned int end = (g_rev_cursor + kBatch < kLimit) ? (g_rev_cursor + kBatch) : kLimit;
    for (unsigned int i = g_rev_cursor; i < end; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &cls) || cls < 0x10000) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(cls + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strstr(nm, "Character") == nullptr) continue;
        std::uintptr_t asc = 0;
        if (!ue::Reader::SafePtr(o + 0x940, &asc) || asc < 0x10000) continue;
        ++g_rev_found;
        std::uintptr_t pc = 0, inst = 0, atk = 0;
        ue::Reader::SafePtr(asc + 0x2620, &pc);
        ue::Reader::SafePtr(asc + 0x2370, &inst);
        ue::Reader::SafePtr(asc + 0x2388, &atk);
        if (g_rev_logged < 8) {
            char db[160];
            std::snprintf(db, sizeof(db), "ascscan: rev char=0x%llX asc=0x%llX pc=0x%llX inst=0x%llX atk=0x%llX",
                          (unsigned long long)o, (unsigned long long)asc,
                          (unsigned long long)pc, (unsigned long long)inst, (unsigned long long)atk);
            PushLog(db);
            ++g_rev_logged;
        }
        if (g_monster_asc == 0 && pc < 0x10000) g_monster_asc = asc;
        if (g_player_asc == 0 && pc >= 0x10000) g_player_asc = asc;
    }
    g_rev_cursor = end;
    if (g_rev_cursor >= kLimit) {
        g_rev_phase = 0;
        char lb[128];
        std::snprintf(lb, sizeof(lb), "ascscan: reverse done found=%d monsterAsc=0x%llX playerAsc=0x%llX",
                      g_rev_found, (unsigned long long)g_monster_asc, (unsigned long long)g_player_asc);
        PushLog(lb);
    }
}


// ===== 闂備胶鍋撳畷姗€鎯屾ィ鍐ㄧ煑闁哄诞灞芥闁荤姷鍋愬Σ鍕濡剧€榬ld -> level/pc/pawn闂佹寧绋戦懟顖氼焽鎼淬劌绠?dump 闂佺顑呯换鎺嶇昂闂?====
static std::uintptr_t g_chain_pawn = 0;
static std::uintptr_t g_monster_actor = 0;

static bool HasBaseClassChain(std::uintptr_t cls, const char* want) {
    for (int d = 0; d < 32 && cls >= 0x10000; ++d) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (ue::Reader::SafeU32(cls + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, want) == 0) return true;
        std::uintptr_t super = 0;
        if (!ue::Reader::SafePtr(cls + 0x40, &super)) return false;
        cls = super;
    }
    return false;
}


// ===== 婵炴垶鎸婚悧婊堝疾椤愶箑绀傛繝濠傚暟娣囨椽鏌ㄥ☉娆戭暡濠㈢懓鍊块獮?ULevel闂佹寧绋戦惌渚€顢?OwningWorld(0xC0) =====
static int g_ws_req = 0;
static unsigned int g_ws_cursor = 0;
static std::uintptr_t g_world_fixed = 0;
static int g_ws_hit = 0;

void RequestWorldScan() {
    g_ws_req = 1; g_ws_cursor = 0; g_world_fixed = 0; g_ws_hit = 0;
    PushLog("worldscan: requested");
}

void TickWorldScan() {
    if (!g_ws_req) return;
    const unsigned int kBatch = 20000, kLimit = 400000;
    const unsigned int end = (g_ws_cursor + kBatch < kLimit) ? (g_ws_cursor + kBatch) : kLimit;
    for (unsigned int i = g_ws_cursor; i < end; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &cls) || cls < 0x10000) continue;
        unsigned int nid = 0;
        char nm[48] = {0};
        if (!ue::Reader::SafeU32(cls + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "Level") != 0) continue;
        std::uintptr_t world = 0, arr = 0;
        unsigned int num = 0;
        if (!ue::Reader::SafePtr(o + 0xC0, &world) || world < 0x10000) continue;
        ue::Reader::SafePtr(o + 0xA0, &arr);
        ue::Reader::SafeU32(o + 0xA8, &num);
        if (g_world_fixed == 0 && num > 0) {
            g_world_fixed = world;
            ++g_ws_hit;
            char lb[144];
            std::snprintf(lb, sizeof(lb), "worldscan: hit level=0x%llX world=0x%llX actors=%u",
                          (unsigned long long)o, (unsigned long long)world, num);
            PushLog(lb);
        }
    }
    g_ws_cursor = end;
    if (g_ws_cursor >= kLimit) {
        g_ws_req = 0;
        char lb[112];
        std::snprintf(lb, sizeof(lb), "worldscan: done hit=%d world=0x%llX",
                      g_ws_hit, (unsigned long long)g_world_fixed);
        PushLog(lb);
    }
}

static std::uintptr_t ReadWorldFresh() {
    if (g_gworld_addr != 0) {
        std::uintptr_t w = 0;
        __try { w = *reinterpret_cast<std::uintptr_t*>(g_gworld_addr); } __except (EXCEPTION_EXECUTE_HANDLER) { w = 0; }
        if (w >= 0x10000) return w;
    }
    return static_cast<std::uintptr_t>(g_gworld_ptr);
}

// ===== 闁荤姴娲ら敃銉╁蓟閸ヮ剚鏅慨婵嗛敪unction thunk(+0xD8) 闂佸憡鍔曢幊鎰枔?call 闂佺儵鏅╅崰妤呮偉閿濆鏅柛顐ｇ箓濞懷囨偣閸ワ妇涓茬紒杈ㄧ箖缁嬪顓奸崼顐ｎ棅 hook闂?====
static void ThunkRange(std::uintptr_t base, std::uintptr_t* lo, std::uintptr_t* hi) {
    *lo = base; *hi = base;
    __try {
        const std::uint32_t e_lfanew = *reinterpret_cast<const std::uint32_t*>(base + 0x3C);
        const std::uint16_t magic = *reinterpret_cast<const std::uint16_t*>(base + e_lfanew + 0x18);
        const std::uint32_t size = *reinterpret_cast<const std::uint32_t*>(base + e_lfanew + 0x18 + (magic == 0x20B ? 0x38 : 0x38));
        *hi = base + size;
    } __except (EXCEPTION_EXECUTE_HANDLER) { *lo = 0; *hi = 0; }
}

static void DiagOneThunk(const char* tag, std::uintptr_t ufn) {
    if (ufn < 0x10000) { char b[96]; std::snprintf(b, sizeof(b), "thunk %s: ufn=0", tag); PushLog(b); return; }
    std::uintptr_t exec = 0;
    if (!ue::Reader::SafePtr(ufn + 0xD8, &exec) || exec < 0x10000) {
        char b[112]; std::snprintf(b, sizeof(b), "thunk %s: exec=0", tag); PushLog(b); return;
    }
    std::uintptr_t lo = 0, hi = 0;
    ThunkRange(reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe")), &lo, &hi);
    unsigned char buf[192] = {0};
    __try { std::memcpy(buf, reinterpret_cast<const void*>(exec), sizeof(buf)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { PushLog("thunk: read fail"); return; }
    char head[192];
    std::snprintf(head, sizeof(head), "thunk %s: ufn=0x%llX exec=0x%llX text=[0x%llX,0x%llX)",
                  tag, (unsigned long long)ufn, (unsigned long long)exec,
                  (unsigned long long)lo, (unsigned long long)hi);
    PushLog(head);
    int found = 0;
    for (int i = 0; i + 5 <= 192 && found < 4; ++i) {
        if (buf[i] != 0xE8) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, buf + i + 1, 4);
        const std::uintptr_t call_site = exec + i;
        const std::uintptr_t target = call_site + 5 + static_cast<std::intptr_t>(rel);
        const bool in_text = (lo != 0) && (target >= lo) && (target < hi);
        if (!in_text) continue;
        char cb[160];
        std::snprintf(cb, sizeof(cb), "thunk %s: call@+0x%X -> 0x%llX (inText=1)",
                      tag, i, (unsigned long long)target);
        PushLog(cb);
        ++found;
    }
    if (found == 0) PushLog("thunk: no call target inside HTGame text");
}

static std::uintptr_t FindChildUfnByName(std::uintptr_t cls, const char* want) {
    std::uintptr_t child = 0;
    if (!ue::Reader::SafePtr(cls + 0x48, &child)) return 0;
    for (int guard = 0; guard < 8192 && child >= 0x10000; ++guard) {
        unsigned int nid = 0;
        char nm[80] = {0};
        if (ue::Reader::SafeU32(child + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, want) == 0) return child;
        std::uintptr_t next = 0;
        if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
        child = next;
    }
    return 0;
}


// ===== 闂佸憡鐟禍锝夘敇閹间礁鏋侀柣妤€鐗嗙挧鎾⒑閽樺澧崇紒杈ㄧ懄缁嬪顓兼径濠冾仧闂佽　鍋撳ǎ鍥ㄥ閸嬫挻鎷呮搴ｆ喒闂佽　鍋撻柣鐔告緲濡﹢鏌℃担绋跨盎缂?====
typedef void* (*ProbeFn8)(void*, void*, void*, void*, void*, void*, void*, void*);
static ProbeFn8 g_probe_a_orig = nullptr;
static ProbeFn8 g_probe_b_orig = nullptr;
static int g_probe_a = 0, g_probe_b = 0;
static int g_probe_rc = 0;

static void* ProbeADetour(void* a1, void* a2, void* a3, void* a4,
                          void* a5, void* a6, void* a7, void* a8) {
    ++g_probe_a;
    if (g_probe_a == 1) PushLog("probe: A first hit");
    if (!g_probe_a_orig) return nullptr;
    return g_probe_a_orig(a1, a2, a3, a4, a5, a6, a7, a8);
}

static void* ProbeBDetour(void* a1, void* a2, void* a3, void* a4,
                          void* a5, void* a6, void* a7, void* a8) {
    ++g_probe_b;
    if (g_probe_b == 1) PushLog("probe: B first hit");
    if (!g_probe_b_orig) return nullptr;
    return g_probe_b_orig(a1, a2, a3, a4, a5, a6, a7, a8);
}

static bool InstallOneProbe(std::uintptr_t target, void* detour, const char* label,
                            ProbeFn8* out_orig) {
    const void* hook_service = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    if (!hook_service) return false;
    const auto* hook = static_cast<const AnomalyHookServiceV1*>(hook_service);
    if (!hook->create) return false;
    AnomalyHookRequestV1 request{};
    request.struct_size = sizeof(request);
    request.kind = ANOMALY_HOOK_V1_FUNCTION;
    request.target = target;
    request.detour = detour;
    request.label = AnomalyStringViewV1{label, std::strlen(label)};
    AnomalyGenerationHandleV1 handle{};
    std::uintptr_t original = 0;
    const auto status = hook->create(hook->user, &request, &original, &handle);
    if (status.code != ANOMALY_STATUS_V1_OK) return false;
    *out_orig = reinterpret_cast<ProbeFn8>(original);
    return true;
}

void InstallProbeHooks() {
    if (!g_ctx) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { PushLog("probe: no module"); return; }
    const bool a = InstallOneProbe(base + 0x187C480, reinterpret_cast<void*>(&ProbeADetour), "probe.A", &g_probe_a_orig);
    const bool b = InstallOneProbe(base + 0x187BAE0, reinterpret_cast<void*>(&ProbeBDetour), "probe.B.OnDamaged", &g_probe_b_orig);
    g_probe_rc = (a ? 1 : 0) | (b ? 2 : 0);
    char lb[128];
    std::snprintf(lb, sizeof(lb), "probe: install A=%d B=%d rc=%d", a ? 1 : 0, b ? 1 : 0, g_probe_rc);
    PushLog(lb);
}

void ProbeStat() {
    char lb[128];
    std::snprintf(lb, sizeof(lb), "probe: stat A=%d B=%d", g_probe_a, g_probe_b);
    PushLog(lb);
}

void DiagThunkScan() {
    const std::uintptr_t asc_cls = FindAbiCompClass();
    char hb[128];
    std::snprintf(hb, sizeof(hb), "diag: ascCls=0x%llX", (unsigned long long)asc_cls);
    PushLog(hb);
    if (asc_cls != 0) {
        const std::uintptr_t u1 = FindChildUfnByName(asc_cls, "K2_ApplyGameEffectWithClassByActor");
        DiagOneThunk("ApplyGE", u1);
    }
    if (g_monster_actor >= 0x10000) {
        std::uintptr_t ccls = 0;
        ue::Reader::SafePtr(g_monster_actor + 0x10, &ccls);
        std::uintptr_t u2 = 0;
        for (int d = 0; d < 32 && ccls >= 0x10000; ++d) {
            u2 = FindChildUfnByName(ccls, "OnDamaged");
            if (u2 != 0) {
                char db[96];
                std::snprintf(db, sizeof(db), "diag: OnDamaged at depth %d", d);
                PushLog(db);
                break;
            }
            std::uintptr_t sup = 0;
            if (!ue::Reader::SafePtr(ccls + 0x40, &sup) || sup < 0x10000) break;
            ccls = sup;
        }
        DiagOneThunk("OnDamaged", u2);
    } else {
        PushLog("diag: no monster actor (run Level->Actors first)");
    }
    PushLog("diag: done");
}


// ===== SendGameplayEventToActor 闂佸憡顭囬崰鎾诲极閹惧灈鍋撻崷顓炰户妤犵偛娲獮鎴﹀即閳ュ磭顔嗛梺鎸庣☉濠㈠崣unk闂佹剚鍋呮晶? 闁诲氦顫夐惌顔剧礊閸涘瓨鏅?====
typedef void* (*SgeFn8)(void*, void*, void*, void*, void*, void*, void*, void*);
static SgeFn8 g_sgn_orig = nullptr;
static int g_sgn_hooked = 0;
static int g_sgn_hits = 0;
static void* g_sgn_args[8] = {nullptr};
static int g_sgn_left = 0, g_sgn_done = 0;
static std::uint32_t g_sgn_due = 0;
static int g_sgn_unstable = 0;

static bool PtrLooksStable(void* p) {
    const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(p);
    if (v == 0) return true;
    if (v < 0x10000000ULL) return false;      // 闁诲繐绻愮换鎰耿閹绢喖閿ら柍褜鍓熼弫宥咁潩椤撶姴鐏?婵炴垶鎸搁悺銊ヮ渻?
    return true;
}

static void* SgeNativeDetour(void* a1, void* a2, void* a3, void* a4,
                             void* a5, void* a6, void* a7, void* a8) {
    ++g_sgn_hits;
    if (g_sgn_hits <= 3) {
        char b[224];
        std::snprintf(b, sizeof(b), "sge: hit a1=0x%llX a2=0x%llX a3=0x%llX a4=0x%llX a5=0x%llX",
                      (unsigned long long)reinterpret_cast<std::uintptr_t>(a1),
                      (unsigned long long)reinterpret_cast<std::uintptr_t>(a2),
                      (unsigned long long)reinterpret_cast<std::uintptr_t>(a3),
                      (unsigned long long)reinterpret_cast<std::uintptr_t>(a4),
                      (unsigned long long)reinterpret_cast<std::uintptr_t>(a5));
        PushLog(b);
    }
    if (g_sgn_hits == 1) {
        g_sgn_args[0] = a1; g_sgn_args[1] = a2; g_sgn_args[2] = a3; g_sgn_args[3] = a4;
        g_sgn_args[4] = a5; g_sgn_args[5] = a6; g_sgn_args[6] = a7; g_sgn_args[7] = a8;
        g_sgn_unstable = 0;
        for (int i = 0; i < 8; ++i) if (!PtrLooksStable(g_sgn_args[i])) ++g_sgn_unstable;
        if (g_sgn_unstable > 0) {
            g_sgn_left = 0;
            char b[96];
            std::snprintf(b, sizeof(b), "sge: unstable args=%d -> replay skipped", g_sgn_unstable);
            PushLog(b);
        } else {
            g_sgn_left = 8; g_sgn_done = 0;
            g_sgn_due = static_cast<std::uint32_t>(NowMs() + 100);
            PushLog("sge: arm native replay left=8");
        }
    }
    if (!g_sgn_orig) return nullptr;
    return g_sgn_orig(a1, a2, a3, a4, a5, a6, a7, a8);
}

static std::uintptr_t FindClassObjectByName2(const char* want) {
    for (unsigned int i = 0; i < 400000; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &cls) || cls < 0x10000) continue;
        unsigned int cid = 0;
        char cname[48] = {0};
        if (!ue::Reader::SafeU32(cls + 0x18, &cid)) continue;
        DecodeName(cid, cname, sizeof(cname));
        if (std::strcmp(cname, "Class") != 0 && std::strcmp(cname, "BlueprintGeneratedClass") != 0) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(o + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, want) == 0) return o;
    }
    return 0;
}

void InstallSgeNativeHook() {
    if (g_sgn_hooked != 0 || !g_ctx) return;
    g_sgn_hooked = 1;
    std::uintptr_t ufn = 0;
    for (unsigned int i = 0; i < 400000 && ufn == 0; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t k = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &k) || k < 0x10000) continue;
        unsigned int cid = 0;
        char cn[32] = {0};
        if (!ue::Reader::SafeU32(k + 0x18, &cid)) continue;
        DecodeName(cid, cn, sizeof(cn));
        if (std::strcmp(cn, "Function") != 0) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(o + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, "SendGameplayEventToActor") == 0) ufn = o;
    }
    if (ufn < 0x10000) { PushLog("sge: function not found"); return; }
    PushLog("sge: function located");
    std::uintptr_t thunk = 0;
    if (!ue::Reader::SafePtr(ufn + 0xD8, &thunk) || thunk < 0x10000) { PushLog("sge: no thunk"); return; }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    unsigned char buf[192] = {0};
    __try { std::memcpy(buf, reinterpret_cast<const void*>(thunk), sizeof(buf)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { PushLog("sge: thunk read fail"); return; }
    std::uintptr_t target = 0;
    for (int i = 0; i + 5 <= 192; ++i) {
        if (buf[i] != 0xE8) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, buf + i + 1, 4);
        const std::uintptr_t t = thunk + i + 5 + static_cast<std::intptr_t>(rel);
        if (t > base && t < base + 0x20000000ULL) { target = t; break; }
    }
    if (target == 0) { PushLog("sge: no native target"); return; }
    char lb[144];
    std::snprintf(lb, sizeof(lb), "sge: thunk=0x%llX native=0x%llX", (unsigned long long)thunk, (unsigned long long)target);
    PushLog(lb);
    const void* hook_service = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    if (!hook_service) { PushLog("sge: no hook service"); return; }
    const auto* hook = static_cast<const AnomalyHookServiceV1*>(hook_service);
    if (!hook->create) { PushLog("sge: no create"); return; }
    AnomalyHookRequestV1 request{};
    request.struct_size = sizeof(request);
    request.kind = ANOMALY_HOOK_V1_FUNCTION;
    request.target = target;
    request.detour = reinterpret_cast<void*>(&SgeNativeDetour);
    request.label = AnomalyStringViewV1{"sge.native", 10};
    AnomalyGenerationHandleV1 handle{};
    std::uintptr_t original = 0;
    const auto status = hook->create(hook->user, &request, &original, &handle);
    if (status.code == ANOMALY_STATUS_V1_OK) {
        g_sgn_orig = reinterpret_cast<SgeFn8>(original);
        PushLog("sge: hook installed");
    } else {
        char hb[96];
        std::snprintf(hb, sizeof(hb), "sge: hook failed rc=%d", static_cast<int>(status.code));
        PushLog(hb);
    }
}

static void TickSgeNativeReplay() {
    if (g_sgn_left <= 0 || !g_sgn_orig) return;
    if (NowMs() < g_sgn_due) return;
    g_sgn_orig(g_sgn_args[0], g_sgn_args[1], g_sgn_args[2], g_sgn_args[3],
               g_sgn_args[4], g_sgn_args[5], g_sgn_args[6], g_sgn_args[7]);
    --g_sgn_left; ++g_sgn_done;
    char b[96];
    std::snprintf(b, sizeof(b), "sge: exec n=%d left=%d", g_sgn_done, g_sgn_left);
    PushLog(b);
    g_sgn_due = static_cast<std::uint32_t>(NowMs() + 100);
}

void ChainToPawn() {
    if (g_gworld_ptr == 0) { PushLog("chain: no GWorld"); return; }
    const std::uintptr_t world = (g_world_fixed >= 0x10000)
        ? g_world_fixed : ReadWorldFresh();
    if (world < 0x10000) { PushLog("world: unresolved (run World scan)"); return; }
    std::uintptr_t level = 0, gi = 0, arr = 0, lp = 0, pc = 0, pawn = 0;
    unsigned int num = 0;
    ue::Reader::SafePtr(world + 0x30, &level);
    ue::Reader::SafePtr(world + 0x230, &gi);
    if (gi < 0x10000) { char b[96]; std::snprintf(b, sizeof(b), "chain: abort at gi (world=0x%llX level=0x%llX)", (unsigned long long)world, (unsigned long long)level); PushLog(b); return; }
    ue::Reader::SafePtr(gi + 0x38, &arr);
    ue::Reader::SafeU32(gi + 0x40, &num);
    if (arr < 0x10000 || num == 0) { PushLog("chain: abort at LocalPlayers"); return; }
    ue::Reader::SafePtr(arr, &lp);
    if (lp < 0x10000) { PushLog("chain: abort at LocalPlayer[0]"); return; }
    ue::Reader::SafePtr(lp + 0x30, &pc);
    if (pc < 0x10000) { PushLog("chain: abort at PlayerController"); return; }
    ue::Reader::SafePtr(pc + 0x308, &pawn);
    if (pawn >= 0x10000) g_chain_pawn = pawn;
    if (pawn >= 0x10000) g_sge_player = pawn;
    InstallSgeNativeHook();
    if (pawn >= 0x10000) g_player_pawn_ref = pawn;
    char lb[200];
    std::snprintf(lb, sizeof(lb), "chain: world=0x%llX level=0x%llX gi=0x%llX lp=0x%llX(num=%u) pc=0x%llX pawn=0x%llX",
                  (unsigned long long)world, (unsigned long long)level, (unsigned long long)gi,
                  (unsigned long long)lp, num, (unsigned long long)pc, (unsigned long long)pawn);
    PushLog(lb);
}

void LevelToActors() {
    g_monster_asc = 0;
    g_monster_actor = 0;
    g_player_asc = 0;
    PushLog("ascreset: cleared");
    if (g_gworld_ptr == 0) { PushLog("actors: no GWorld"); return; }
    const std::uintptr_t world = (g_world_fixed >= 0x10000)
        ? g_world_fixed : ReadWorldFresh();
    if (world < 0x10000) { PushLog("world: unresolved (run World scan)"); return; }
    std::uintptr_t level = 0, arr = 0;
    unsigned int num = 0;
    ue::Reader::SafePtr(world + 0x30, &level);
    if (level < 0x10000) { PushLog("actors: abort at level"); return; }
    ue::Reader::SafePtr(level + 0xA0, &arr);
    ue::Reader::SafeU32(level + 0xA8, &num);
    char hb[112];
    std::snprintf(hb, sizeof(hb), "actors: level=0x%llX arr=0x%llX num=%u",
                  (unsigned long long)level, (unsigned long long)arr, num);
    PushLog(hb);
    if (arr < 0x10000 || num == 0) return;
    const unsigned int cap = (num < 4000u) ? num : 4000u;
    int hits = 0;
    for (unsigned int i = 0; i < cap && hits < 8; ++i) {
        std::uintptr_t act = 0;
        if (!ue::Reader::SafePtr(arr + 8ull * i, &act) || act < 0x10000) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(act + 0x10, &cls) || cls < 0x10000) continue;
        int chainDepth = 0;
        bool chainHit = false;
        char chainName[64] = {0};
        {
            std::uintptr_t c = cls;
            for (int d = 0; d < 32 && c >= 0x10000; ++d) {
                unsigned int sid = 0;
                char snm[64] = {0};
                if (ue::Reader::SafeU32(c + 0x18, &sid)) DecodeName(sid, snm, sizeof(snm));
                if (d == 0 && snm[0] != 0) std::snprintf(chainName, sizeof(chainName), "%s", snm);
                if (std::strstr(snm, "HTAbilityCharacter") != nullptr) { chainHit = true; break; }
                std::uintptr_t sup = 0;
                if (!ue::Reader::SafePtr(c + 0x40, &sup) || sup < 0x10000) break;
                c = sup;
                ++chainDepth;
            }
        }
        {
            static int sampled = 0;
            if (sampled < 8) {
                char sb[200];
                std::snprintf(sb, sizeof(sb), "actors: sample i=%u act=0x%llX cls=0x%llX name=%s depth=%d hit=%d",
                              i, (unsigned long long)act, (unsigned long long)cls, chainName, chainDepth, chainHit ? 1 : 0);
                PushLog(sb);
                ++sampled;
            }
        }
        if (!chainHit) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (ue::Reader::SafeU32(cls + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        std::uintptr_t asc = 0;
        ue::Reader::SafePtr(act + 0x940, &asc);
        char lb[176];
        std::snprintf(lb, sizeof(lb), "actors: hit %s actor=0x%llX asc=0x%llX",
                      nm, (unsigned long long)act, (unsigned long long)asc);
        PushLog(lb);
        // 婵炴垶鎸撮崑鎾寸箾閸″繐澧查柍褜鍎搁崟顐ヨ崱闂備浇妫勯悧鐐垫閻楀牊浜ゆ繛鍡樻尭濞咃紕鈧鐡曠亸娆戝垝閿熺姴绠甸柨娑樺缁?actor 闂?UClass闂佹寧绋戝寮唖闂佹寧绋戦ˇ顖炲箯?actor 闂佸湱顭堝ú顓㈠箯閿熺姵鏅€光偓閳ь剙煤閸ф绠抽柕澶涘閸欌偓 OnDamaged闂?
        // 婵炴垶鎸哥粔鐑芥憘?FindNameIdByPath / FindClassById闂佺偨鍎查弻锟犲焵椤掍焦顥堥柛瀣噺缁嬪濡堕崱鈺佸Ρ闂佸搫瀚烽崹浼村矗韫囨洘鍋橀柕濞炬櫃閹查箖鏌涘Ο鐑橈紨缂佽鲸绻冪€电厧顫濋浣藉惈濠电偞鎸搁幉锟犲垂濞嗘垹妫悹鎭掑妷閺屻倕鈽夐幘绛瑰伐鐎规洖宕～銏犘ч崶锝呬壕?
        {
            static bool s_od_probed = false;
            if (!s_od_probed && act >= 0x10000 && cls >= 0x10000) {
                s_od_probed = true;
                void* od = nullptr;
                std::uintptr_t walk = cls;
                for (int depth = 0; depth < 8 && walk >= 0x10000 && od == nullptr; ++depth) {
                    od = FindFuncByName(reinterpret_cast<void*>(walk), "OnDamaged");
                    if (od != nullptr) {
                        char wb[128];
                        std::snprintf(wb, sizeof(wb), "od: found at depth=%d cls=0x%llX", depth,
                                      (unsigned long long)walk);
                        PushLog(wb);
                        break;
                    }
                    std::uintptr_t super = 0;   // UStruct::SuperStruct @0x40
                    if (!ue::Reader::SafePtr(walk + 0x40, &super) || super == walk) break;
                    walk = super;
                }
                const std::uintptr_t mod = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
                char ob[176];
                std::snprintf(ob, sizeof(ob), "od: cls=0x%llX ufn=0x%llX mod=0x%llX",
                              (unsigned long long)cls,
                              (unsigned long long)reinterpret_cast<std::uintptr_t>(od),
                              (unsigned long long)mod);
                PushLog(ob);
                if (od != nullptr && mod != 0) {
                    const std::uintptr_t u = reinterpret_cast<std::uintptr_t>(od);
                    for (int off = 0x80; off <= 0xF8; off += 8) {
                        std::uintptr_t v = 0;
                        if (!ue::Reader::SafePtr(u + off, &v)) continue;
                        if (v < mod || v >= mod + 0x11000000ULL) continue;
                        char vb[112];
                        std::snprintf(vb, sizeof(vb), "od: +0x%X -> 0x%llX", off, (unsigned long long)v);
                        PushLog(vb);
                    }
                    PushLog("od: scan done");
                }
                // 闂佸憡鍔曢崯鍧楁偩妤ｅ啫绠戠憸宀€绮╅弶鎴桨闁靛鍨崇粣妤呮煟?ProcessEvent 闁荤姴顑呴崯鈺冪博閺夋垟鏋?OnDamaged 濡ょ姷鍋為崕濂割敋娴兼潙绀堢€广儱鎳忛崐?HP闂?
                // 闂佸憡锚濡銇愰崜浣瑰闁哄娉曠粔鍧楁⒑?闂佸搫鍟版慨鍐测攽閳ь剟鎮?闂佹眹鍔岀€氫即宕戦崨顓涙瀻闁宠棄鎳愮粈濉砇eplayExec=false 闁荤姳璁查弲鐐垫崲閺嵮€鏋旈柛蹇撴憸閻倝鏌￠崼顐＄敖婵犫偓閳ユ剚娼伴柨婵嗘閳锋棃鎮跺☉妯肩効缂佽鲸鐛朹ondmg_ufn 婵炴垶鎸撮崑鎾绘煟閳轰胶鎽犳俊?0闂佹寧绋戦ˇ顓㈠焵?
                const std::uintptr_t od_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
                const std::uintptr_t od_pe = od_base != 0 ? od_base + 0x3E41360ULL : 0;
                if (od != nullptr && od_pe != 0 && act >= 0x10000) {
                    float hp_before = 0.0f, hp_after = 0.0f;
                    const bool read_ok = SafeF32(asc + 0x1A38, &hp_before);
                    unsigned char prm[0x148] = {0};
                    const float dmg = 1.0f;              // DamageAmount @0x00
                    std::memcpy(prm, &dmg, sizeof(dmg));
                    reinterpret_cast<PE_FN>(od_pe)(reinterpret_cast<void*>(act), od, prm);
                    SafeF32(asc + 0x1A38, &hp_after);
                    char rb[160];
                    std::snprintf(rb, sizeof(rb), "od: call ok=%d hp=%.1f -> %.1f",
                                  read_ok ? 1 : 0, hp_before, hp_after);
                    PushLog(rb);
                }
            }
        }
        if (asc >= 0x10000) {
            const bool isMon = (std::strncmp(chainName, "mon", 3) == 0);
            const bool isPl  = (std::strncmp(chainName, "player", 6) == 0) || (std::strncmp(chainName, "Player", 6) == 0);
            if (isMon) { if (g_monster_asc == 0) g_monster_asc = asc; if (g_monster_actor == 0) g_monster_actor = act; }
            else if (isPl) { if (g_player_asc == 0) g_player_asc = asc; }
            else if (g_monster_asc == 0) { g_monster_asc = asc; }
        }
        ++hits;
    }
    char sb[112];
    std::snprintf(sb, sizeof(sb), "actors: done hits=%d monsterAsc=0x%llX",
                  hits, (unsigned long long)g_monster_asc);
    PushLog(sb);
}


// ===== GA 濠电姷顣介崑鎾寸箾閹绘帩鐓奸柍褜鍓氬畝鍛婄妤ｅ啯鏅柛顐ｇ矌閻?GE 闂備緡鍋呭畝鍛婄妤ｅ啰宓侀悹楦挎閹界娀鏌?====
// UAbilitySystemComponent::TryActivateAbilityByClass(TSubclassOf<UGameplayAbility>)
// 闂佸憡鐟ラ崐褰掑汲閻旂儤鏆滈柛鎰╁妿濠€浼存煙?dump闂佹寧绋掗?TSubclassOf<UGameplayAbility> AbilityToActivate; bool ReturnValue; }
static std::uintptr_t g_activate_ufn = 0;

static std::uintptr_t FindClassObjectByName(const char* want) {
    for (unsigned int i = 0; i < 400000; ++i) {
        void* obj = SafeObjectAt(i);
        if (obj == nullptr) continue;
        const std::uintptr_t o = reinterpret_cast<std::uintptr_t>(obj);
        if (o >= 0x7FF000000000ULL) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(o + 0x10, &cls) || cls < 0x10000) continue;
        unsigned int cid = 0;
        char cname[48] = {0};
        if (ue::Reader::SafeU32(cls + 0x18, &cid)) DecodeName(cid, cname, sizeof(cname));
        if (std::strcmp(cname, "Class") != 0 && std::strcmp(cname, "BlueprintGeneratedClass") != 0 && std::strcmp(cname, "WidgetBlueprintGeneratedClass") != 0) continue;
        unsigned int nid = 0;
        char nm[64] = {0};
        if (!ue::Reader::SafeU32(o + 0x18, &nid)) continue;
        DecodeName(nid, nm, sizeof(nm));
        if (std::strcmp(nm, want) == 0) return o;
    }
    return 0;
}

static std::uintptr_t ResolveActivateUfn() {
    if (g_activate_ufn != 0) return g_activate_ufn;
    const std::uintptr_t cls = FindAbiCompClass();
    if (cls == 0) return 0;
    std::uintptr_t child = 0;
    if (!ue::Reader::SafePtr(cls + 0x48, &child)) return 0;
    for (int guard = 0; guard < 4096 && child != 0; ++guard) {
        unsigned int nid = 0;
        char nm[64] = {0};
        if (ue::Reader::SafeU32(child + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
        if (std::strstr(nm, "TryActivateAbilityByClass") != nullptr) { g_activate_ufn = child; return child; }
        std::uintptr_t next = 0;
        if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
        child = next;
    }
    return 0;
}

void ActivateGA() {
    if (!g_ctx) return;
    std::uintptr_t recv = 0;
    {
        std::uintptr_t pawnAsc = 0;
        if (g_chain_pawn >= 0x10000) ue::Reader::SafePtr(g_chain_pawn + 0x940, &pawnAsc);
        if (pawnAsc >= 0x10000) recv = pawnAsc;
        else if (g_player_asc >= 0x10000) recv = g_player_asc;
        else recv = g_monster_asc;
        char vb[112];
        std::snprintf(vb, sizeof(vb), "ga: pick pawnAsc=0x%llX playerAsc=0x%llX recv=0x%llX",
                      (unsigned long long)pawnAsc, (unsigned long long)g_player_asc, (unsigned long long)recv);
        PushLog(vb);
        if (recv >= 0x10000 && g_monster_actor >= 0x10000) {
            const std::uintptr_t slot = recv + 0x2388;
            DWORD opT = 0;
            if (VirtualProtect(reinterpret_cast<LPVOID>(slot & ~0xFFFULL), 0x2000, PAGE_READWRITE, &opT) != 0) {
                *reinterpret_cast<std::uintptr_t*>(slot) = g_monster_actor;
                DWORD tmpT = 0;
                VirtualProtect(reinterpret_cast<LPVOID>(slot & ~0xFFFULL), 0x2000, opT, &tmpT);
                char tb[112];
                std::snprintf(tb, sizeof(tb), "ga: set attackTarget=0x%llX", (unsigned long long)g_monster_actor);
                PushLog(tb);
            } else {
                PushLog("ga: cannot write attackTarget");
            }
        } else {
            PushLog("ga: no monster actor (skip target)");
        }
    }
    if (recv < 0x10000) { PushLog("ga: no asc receiver"); return; }
    const std::uintptr_t ufn = ResolveActivateUfn();
    char hb[128];
    std::snprintf(hb, sizeof(hb), "ga: ufn=0x%llX recv=0x%llX", (unsigned long long)ufn, (unsigned long long)recv);
    PushLog(hb);
    if (ufn < 0x10000) { PushLog("ga: ufn not found"); return; }
    const std::uintptr_t gaCls = FindClassObjectByName("GA_Nanally_Melee_C");
    char cb[112];
    std::snprintf(cb, sizeof(cb), "ga: class GA_Nanally_Melee_C = 0x%llX", (unsigned long long)gaCls);
    PushLog(cb);
    if (gaCls < 0x10000) { PushLog("ga: class not found"); return; }

    unsigned char params[16] = {0};
    void* klass = reinterpret_cast<void*>(gaCls);
    std::memcpy(params + 0x00, &klass, 8);

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    const std::uintptr_t pe = base + 0x3E41360;
    unsigned int saved = 0;
    const std::uintptr_t flags = ufn + 0xB0;
    if (!ue::Reader::SafeU32(flags, &saved)) return;
    DWORD op = 0;
    if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op) == 0) return;
    *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);
    reinterpret_cast<PeRealFn>(pe)(reinterpret_cast<void*>(recv), reinterpret_cast<void*>(ufn), params);
    DWORD op2 = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op2);
    *reinterpret_cast<unsigned int*>(flags) = saved;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);

    char rb[112];
    std::snprintf(rb, sizeof(rb), "ga: done allowRemote=%u ret=%02X", (unsigned)params[0x08], params[0x09]);
    PushLog(rb);
}

void ApplyGEDirect() {
    if (!g_ctx) return;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
    if (base == 0) { PushLog("direct: no module"); return; }
    const std::uintptr_t pe = base + 0x3E41360;

    // 1) UFunction
    std::uintptr_t ufn = g_applyge_fn;
    if (ufn == 0) {
        const std::uintptr_t cls = FindAbiCompClass();
        char cb[96];
        std::snprintf(cb, sizeof(cb), "direct: cls=0x%llX", (unsigned long long)cls);
        PushLog(cb);
        if (cls == 0) { g_apply_direct_rc = -1; return; }
        std::uintptr_t child = 0;
        if (!ue::Reader::SafePtr(cls + 0x48, &child)) { g_apply_direct_rc = -2; return; }
        for (int guard = 0; guard < 4096 && child != 0; ++guard) {
            unsigned int nid = 0;
            char nm[64] = {0};
            if (ue::Reader::SafeU32(child + 0x18, &nid)) DecodeName(nid, nm, sizeof(nm));
            if (std::strcmp(nm, "K2_ApplyGameEffectWithClassByActor") == 0) { ufn = child; break; }
            std::uintptr_t next = 0;
            if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
            child = next;
        }
        if (ufn == 0) { PushLog("direct: ufn not found"); g_apply_direct_rc = -3; return; }
        g_applyge_fn = ufn;
    }

    // 2) receiver
    std::uintptr_t recv = (g_asc_sel >= 0 && g_asc_sel < g_asc_n) ? g_asc[g_asc_sel]
                        : ((g_asc_n > 0) ? g_asc[0] : 0);
    if (g_monster_asc != 0) recv = g_monster_asc;
    if (recv == 0) { PushLog("direct: no asc receiver"); g_apply_direct_rc = -4; return; }

    // 3) GE
    if (g_ge_sel < 0 || g_ge_sel >= g_ge_count) { PushLog("direct: no ge selected"); g_apply_direct_rc = -5; return; }
    void* ge = reinterpret_cast<void*>(g_ge_ptrs[g_ge_sel]);

    // 4) 闂佸憡鐟ラ崐褰掑汲閻旂厧閿?0x78
    unsigned char params[0x78] = {0};
    // recv dump闂佹寧绋掑銊ф偖椤愶箑瑙?+ ClassPrivate + 0x2300..0x2400 闂佸憡鐗曢幖顐︽偂濞嗘挻鏅柛顐ｇ箓濞懷囨偣閸ワ妇涓茬紒杈ㄧ箞閹粙濡搁妶鍥闁诲氦顫夌喊宥夊焵椤戭剙绉剁粈?
    {
        char cn[64] = {0};
        ClassNameOf(reinterpret_cast<void*>(recv), cn, sizeof(cn));
        std::uintptr_t cls = 0;
        ue::Reader::SafePtr(recv + 0x10, &cls);
        char cb[128];
        std::snprintf(cb, sizeof(cb), "direct: recvCls=%s class=0x%llX", cn, (unsigned long long)cls);
        PushLog(cb);

        unsigned char buf[0x100] = {0};
        bool okread = false;
        const std::uintptr_t base_off = 0x2300;
        __try { std::memcpy(buf, reinterpret_cast<const void*>(recv + base_off), sizeof(buf)); okread = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { okread = false; }
        if (okread) {
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
                char* slash = std::strrchr(path, '\\');
                if (slash != nullptr) {
                    *slash = 0;
                    char file[MAX_PATH + 40];
                    std::snprintf(file, sizeof(file), "%s\\asc_dump.bin", path);
                    FILE* fp = std::fopen(file, "wb");
                    if (fp != nullptr) {
                        struct H { std::uintptr_t recv; std::uintptr_t off; std::size_t size; } h{recv, base_off, sizeof(buf)};
                        std::fwrite(&h, sizeof(h), 1, fp);
                        std::fwrite(buf, sizeof(buf), 1, fp);
                        std::fclose(fp);
                    }
                }
            }
            for (int k = 0; k < 16; ++k) {
                std::uintptr_t v = 0;
                std::memcpy(&v, buf + k * 8, 8);
                char lb[80];
                std::snprintf(lb, sizeof(lb), "direct: +0x%llX = 0x%llX",
                              (unsigned long long)(base_off + k * 8), (unsigned long long)v);
                PushLog(lb);
            }
        } else {
            PushLog("direct: recv dump failed");
        }
    }
    void* source = nullptr;
    {
        std::uintptr_t sInst = 0, sCoi = 0, sAtk = 0;
        ue::Reader::SafePtr(recv + 0x2370, &sInst);
        ue::Reader::SafePtr(recv + 0x2390, &sCoi);
        ue::Reader::SafePtr(recv + 0x2388, &sAtk);
        std::uintptr_t pick = 0;
        if (sInst >= 0x10000) pick = sInst;
        else if (sCoi >= 0x10000) pick = sCoi;
        else if (sAtk >= 0x10000) pick = sAtk;
        if (pick == 0 && g_player_asc != 0) {
            std::uintptr_t pInst = 0, pCoi = 0, pOwner = 0;
            ue::Reader::SafePtr(g_player_asc + 0x2370, &pInst);
            ue::Reader::SafePtr(g_player_asc + 0x2390, &pCoi);
            ue::Reader::SafePtr(g_player_asc + 0x160, &pOwner);
            if (pInst >= 0x10000) pick = pInst;
            else if (pCoi >= 0x10000) pick = pCoi;
            else if (pOwner >= 0x10000) pick = pOwner;
            if (pick < 0x10000 && g_chain_pawn >= 0x10000) pick = g_chain_pawn;
        }
        if (pick < 0x10000) {
            PushLog("direct: no source actor (abort)");
            g_apply_direct_rc = -7;
            return;
        }
        source = reinterpret_cast<void*>(pick);
        char sb[96];
        std::snprintf(sb, sizeof(sb), "direct: src=0x%llX", (unsigned long long)pick);
        PushLog(sb);
    }
    std::memcpy(params + 0x00, &source, 8);
    std::memcpy(params + 0x08, &ge, 8);
    void* trigger = reinterpret_cast<void*>(recv);
    std::memcpy(params + 0x68, &trigger, 8);

    // 5) 缂?0x400 -> 闁荤姴顑呴崯浼村极?-> 闁哄鏅滈敋閻?
    unsigned int saved = 0;
    const std::uintptr_t flags = ufn + 0xB0;
    if (!ue::Reader::SafeU32(flags, &saved)) { g_apply_direct_rc = -6; return; }
    DWORD op = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op);
    *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);

    reinterpret_cast<PeRealFn>(pe)(reinterpret_cast<void*>(recv), reinterpret_cast<void*>(ufn), params);

    DWORD op2 = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &op2);
    *reinterpret_cast<unsigned int*>(flags) = saved;
    VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);

    // 闁哄鏅炲Λ鍕叏閻愮儤鏅慨姗嗗幗閺嗩參鏌?dump 闂備焦褰冮惉鐓庘枔?K2_ApplyGameplayEffectToTargetWithClass 闂佸搫鍊藉Λ鍕叏閻愬搫瑙﹂悘鐐跺亹椤忓崬鈽?GE闂?
    // 婵炴垶鎸搁敃顏勵焽娴煎瓨鐒介柨婵嗗閸?...ByActor 闂佽鍘界敮鐔烘崲閹达箑鐐?1 闂佸憡顨堢换婵堢箔婢跺本濯奸柍鍝勫暞缁愭鏌″鍛閻㈩垪鍋撻柣鐐寸▓閸嬫捇鏌ㄥ☉娆戭灱缂佺粯鐗楃粙澶愵敂閸曨偅鐦ｉ梺杞扮劍濠㈡﹢鎮?FBufferData ModifyData闂?
    // 闂佸吋婢橀懟顖氼啅婵犳艾鐭楅柟缁樺竾娴滃ジ鎮归幇鈺佸姢婵炲牊鍨垮浠嬫偡閻楀牏顦┑顔界箰缁叉儳危?ModifyData 闂佺绻堥崝鎴澝归崒鐐存櫖闁秆勫trengthMult=0 / nStackCount=0 闂?0 婵炲顑曢崐鏇㈩敊婵犲洦鏅璺虹墐閸?
    if (g_dmg_fn != nullptr) {
        void* ta_ge = ge;
        void* ta_target = reinterpret_cast<void*>(recv);
        unsigned char tp[0x78] = {0};
        std::memcpy(tp + 0x00, &ta_ge, 8);      // TSubclassOf<UGameplayEffect>
        std::memcpy(tp + 0x08, &ta_target, 8);  // UHTAbilitySystemComponent* 闂佺儵鏅╅崰妤呮偉?
        int   n_level = 1;
        float dur = 5.0f;
        float period = 0.0f;
        int   stack = 1;
        float mult = 1.0f;                      // 1.0 = 濠电偛鐭佸▔鏇㈠极?GE 闂佺厧顨庢禍锝夋閳哄懎鏋侀柡澶婄氨閸?
        float add = 0.0f;
        std::memcpy(tp + 0x10 + 0x28, &n_level, 4);
        std::memcpy(tp + 0x10 + 0x2C, &dur, 4);
        std::memcpy(tp + 0x10 + 0x34, &period, 4);
        std::memcpy(tp + 0x10 + 0x38, &stack, 4);
        std::memcpy(tp + 0x10 + 0x3C, &mult, 4);
        std::memcpy(tp + 0x10 + 0x40, &add, 4);
        tp[0x69] = 0;   // bUsePredictionKey
        std::uintptr_t src_asc = g_player_asc >= 0x10000 ? g_player_asc : recv;
        // 闂佺绻愰悧蹇涘灳濡吋濯?ge 闂佸憡甯楀妯艰姳閹惰棄鍙婃い鏍ㄧ懅閻棗鈽夐弬璺ㄥ⒊缂佽京娅塖ubclassOf 闁荤喐娲戦懗璺衡枔閹达箑鍙?UClass闂佹寧绋戦惌浣衡偓姘ュ灪濞艰鈻庨幘铏仦闂?CDO/闁诲骸婀遍崑妯兼閵夆晜鏅?
        // 闂佸搫鍊藉Λ鍕叏閻愬墎鐤€闁告侗鍨版慨褔鏌熷畡鑸粧缂佹顦靛畷姘舵偋閸喓鐣抽梺杞扮閻楀棜銇愰悙鍝勮摕闁告洍鏂侀崑鎾诲磼濮橆厼鈧亶鏌￠崘顓炵厫濠?dump 闁荤姳鐒﹀妯肩礊瀹ュ鍎嶉柛鏇ㄥ亜閺傃囨煟?RVA 闂佺懓鐏氶幐鎼佸吹椤撱垹绾ч柕澶堝劙缁敻鎮楅棃娑欒础闁告瑥閰ｆ俊?
        {
            char gecls[64] = {0};
            ClassNameOf(ta_ge, gecls, sizeof(gecls));
            std::uintptr_t ge_meta = 0;
            ue::Reader::SafePtr(reinterpret_cast<std::uintptr_t>(ta_ge) + 0x10, &ge_meta);
            char meta_name[64] = {0};
            if (ge_meta >= 0x10000) ClassNameOf(reinterpret_cast<void*>(ge_meta), meta_name, sizeof(meta_name));
            const std::uintptr_t mod = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
            char gb[240];
            std::snprintf(gb, sizeof(gb),
                "ta3: ge=%p geCls=%s meta=0x%llX metaCls=%s ufn=0x%llX rva7778E50=0x%llX",
                ta_ge, gecls, (unsigned long long)ge_meta, meta_name,
                (unsigned long long)reinterpret_cast<std::uintptr_t>(g_dmg_fn),
                (unsigned long long)(mod != 0 ? mod + 0x7778E50ULL : 0));
            PushLog(gb);
        }
        const std::uintptr_t tflags = reinterpret_cast<std::uintptr_t>(g_dmg_fn) + 0xB0;
        unsigned int tsaved = 0;
        if (ue::Reader::SafeU32(tflags, &tsaved)) {
            DWORD top = 0;
            VirtualProtect(reinterpret_cast<LPVOID>(tflags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &top);
            *reinterpret_cast<unsigned int*>(tflags) = tsaved | 0x400u;
            VirtualProtect(reinterpret_cast<LPVOID>(tflags & ~0xFFFULL), 0x2000, top, &top);
            for (unsigned char ev = 0; ev <= 2; ++ev) {
                tp[0x68] = ev;                       // EBuffEventType: 0=UNKNOW 1=BEGIN 2=PERIODIC
                std::memset(tp + 0x6C, 0, 8);        // 濠电偞鎸搁幊蹇曟暜閳ь剙鈽夐幘绛规缂佹梹娼欓埢搴ㄦ晬閸曨剛鏆犻柡澶嗘櫆閺屻劌煤閺嶎厼鐭楅柕澶涘閸?
                reinterpret_cast<PeRealFn>(pe)(reinterpret_cast<void*>(src_asc), g_dmg_fn, tp);
                char tb[176];
                std::snprintf(tb, sizeof(tb), "ta3: ev=%u handle=%02X%02X%02X%02X%02X%02X%02X%02X",
                              static_cast<unsigned>(ev),
                              tp[0x6C], tp[0x6D], tp[0x6E], tp[0x6F],
                              tp[0x70], tp[0x71], tp[0x72], tp[0x73]);
                PushLog(tb);
            }
            if (VirtualProtect(reinterpret_cast<LPVOID>(tflags & ~0xFFFULL), 0x2000, PAGE_READWRITE, &top) != 0) {
                *reinterpret_cast<unsigned int*>(tflags) = tsaved;
                VirtualProtect(reinterpret_cast<LPVOID>(tflags & ~0xFFFULL), 0x2000, top, &top);
            }
        } else {
            PushLog("ta3: function flags unreadable");
        }

    }

    g_apply_direct_rc = 0;
    if (params[0x70] != 1) {                       // 婵炴垶鎸堕崐鏍敆閻愬搫绀傛繝濠傚暟娣囨椽鏌ㄥ☉娆庝孩缂佺粯鍨垮畷鍫曟倷閺夋垹浜ｉ梺鍛婃⒒閸犳劖鏅堕悩鐢电＜鐟滃繘鎮?
        char fb[96];
        std::snprintf(fb, sizeof(fb), "direct: rejected ret=%02X (no repeat)", params[0x70]);
        PushLog(fb);
    } else {
        g_rep_recv = recv; g_rep_ufn = ufn; g_rep_ge = ge; g_rep_source = source;
        g_rep_count = 0; g_rep_last_ms = NowMs(); g_rep_on = 1;
        char ab[112];
        std::snprintf(ab, sizeof(ab), "repeat: armed interval=%ums max=%d", g_rep_interval_ms, g_rep_max);
        PushLog(ab);
    }
    char rb[112];
    std::snprintf(rb, sizeof(rb), "direct: ok recv=0x%llX ge=%p ufn=0x%llX ret=%02X",
                  (unsigned long long)recv, ge, (unsigned long long)ufn, params[0x70]);
    PushLog(rb);
}

// ---------------------------------------------------------------------------
// 浼ゅ鍥炴斁 搂5锛氬厛鎶撴父鎴忚嚜宸辩殑 K2_ApplyGameplayEffectToTargetWithClass 鍙傛暟锛屽啀鐢?
// 杩欎唤鐪熷疄鍙傛暟澶嶇幇鏂藉姞锛屼笉鍐嶇寽 FBufferData銆?
//
// 瀹夸富鎶?ue5.process-event 鐨勮闃呭洖璋冨垎鍙戝湪 original ProcessEvent **涔嬪墠**
// 锛坕nclude/anomaly/sdk/services/ue5.h 鐨勫绾︼級锛屾墍浠ヨ繖閲岃鍒扮殑 0x78 鏄父鎴忎紶鍏?
// 鐨勫叆鍙傦紱ReturnValue@0x6C 姝ゅ埢灏氭湭鍐欏叆锛屼笉鑳芥嬁瀹冨垽鏂?bPassedFilters銆?
// ---------------------------------------------------------------------------
static constexpr int kApplyUfnMax = 24;
static std::uintptr_t g_au_fn[kApplyUfnMax] = {};
static char           g_au_name[kApplyUfnMax][64] = {};
static int            g_au_n = 0;
static int            g_au_ready = 0;

static int ApplyUfnIndex(std::uintptr_t function) {
    for (int i = 0; i < g_au_n; ++i) {
        if (g_au_fn[i] == function) return i;
    }
    return -1;
}

static void ObjectClassName(std::uintptr_t object, char* out, std::size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = 0;
    if (object < 0x10000) { std::snprintf(out, cap, "null"); return; }
    std::uintptr_t cls = 0;
    unsigned int id = 0;
    if (!ue::Reader::SafePtr(object + 0x10, &cls) || cls < 0x10000) {
        std::snprintf(out, cap, "noclass");
        return;
    }
    if (!ue::Reader::SafeU32(cls + 0x18, &id)) { std::snprintf(out, cap, "noname"); return; }
    DecodeName(id, out, cap);
    if (out[0] == 0) std::snprintf(out, cap, "?");
}

// HTAbilitySystemComponent 鍙婄埗绫?Children 閾鹃噷鎵€鏈夊悕瀛楀惈 "Apply" 鐨?UFunction銆?
// 鍙仛涓€娆★紱绫绘寚閽堝鐢?TickFeatures 宸茶В鏋愮殑 g_cls_asc锛岄伩鍏嶅啀鍋?GObjects 鍏ㄦ壂銆?
static void CollectApplyUfns() {
    if (g_au_ready != 0) return;
    std::uintptr_t cls = g_cls_asc;
    if (cls < 0x10000) return;
    char nm[64] = {0};
    for (int depth = 0; depth < 8 && cls >= 0x10000 && g_au_n < kApplyUfnMax; ++depth) {
        std::uintptr_t child = 0;
        if (ue::Reader::SafePtr(cls + 0x48, &child)) {
            for (int guard = 0; guard < 4096 && child != 0 && g_au_n < kApplyUfnMax; ++guard) {
                unsigned int nid = 0;
                if (ue::Reader::SafeU32(child + 0x18, &nid)) {
                    DecodeName(nid, nm, sizeof(nm));
                    if (nm[0] != 0 && std::strstr(nm, "Apply") != nullptr &&
                        ApplyUfnIndex(child) < 0) {
                        g_au_fn[g_au_n] = child;
                        std::snprintf(g_au_name[g_au_n], sizeof(g_au_name[0]), "%s", nm);
                        ++g_au_n;
                    }
                }
                std::uintptr_t next = 0;
                if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
                child = next;
            }
        }
        std::uintptr_t super = 0;
        if (!ue::Reader::SafePtr(cls + 0x40, &super)) break;
        cls = super;
    }
    if (g_au_n == 0) return;
    g_au_ready = 1;
    char b[112];
    std::snprintf(b, sizeof(b), "cap: %d apply ufns cls=0x%llX", g_au_n, g_cls_asc);
    PushLog(b);
    for (int i = 0; i < g_au_n; ++i) {
        std::snprintf(b, sizeof(b), "cap ufn[%02d] 0x%llX %s", i,
                      (unsigned long long)g_au_fn[i], g_au_name[i]);
        PushLog(b);
    }
}

// HTAttributeComponent 鐨?HP 璧?ASC + 0x1A38锛坒loat锛夛紱鍦ㄨ鑹?actor 涓婅浼氬緱鍒?0銆?
static bool ReadAscHP(std::uintptr_t asc, float* out) {
    if (asc < 0x10000 || out == nullptr) return false;
    float v = 0.0f;
    __try { v = *reinterpret_cast<const float*>(asc + 0x1A38); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!(v >= 0.0f) || v > 1.0e9f) return false;   // NaN / 璐熷€?/ 鏄庢樉涓嶆槸琛€閲?
    *out = v;
    return true;
}

static constexpr int kCapRing = 12;
struct ApplyCap {
    unsigned char  raw[0x78];      // 娓告垙浼犲叆鐨?0x78 鍙傛暟鍧楋紝鍘熸牱
    std::uintptr_t function;
    std::uintptr_t receiver;       // 鏂藉姞鏂?ASC锛圥rocessEvent 鐨?this锛?
    std::uintptr_t ge_class;       // raw+0x00 TSubclassOf<UGameplayEffect>
    std::uintptr_t target_asc;     // raw+0x08 UHTAbilitySystemComponent*
    unsigned int   buff_id;        // FBufData.BuffName @ raw+0x10
    int            buff_num;
    char           buff[64];
    char           ge[64];
    char           fn[64];
    char           tags[4][64];
    int            tag_n;
    unsigned char  tag_raw[8 * 8];   // DynamicGrantedTags 閲?FName 鐨勫壇鏈紙鏈€澶?8 鏉★級
    int            tag_raw_n;
    unsigned char  ev;
    float          hp;             // 鏈鏂藉姞鍓嶇殑鐩爣 HP
    float          hp_delta;       // 鍚屼竴鐩爣涓婃鎹曡幏鑷充粖鎺夌殑琛€
    int            hp_ok;
    std::uint64_t  ms;
};

static ApplyCap g_cap[kCapRing];
static int g_cap_n = 0;
static int g_cap_last = -1;
static int g_cap_hits = 0;
static int g_cap_suppress = 0;
static int g_cap_log_left = 64;
static int g_cap_logged = 0;
static int g_cap_dup = 0;

// 閽╁瓙/鍥炶皟閲屽彧鍋氭寚閽堟瘮杈冧笌 memcpy锛氫笉璋冪敤浠讳綍瀹夸富鏈嶅姟銆傚涓?ue5.process-event 鐨?
// 璁㈤槄鍥炶皟鍦?Game 绾跨▼ original ProcessEvent 涔嬪墠娲惧彂锛岃€?resolve_utf8 杩欑被鏈嶅姟浼氭嬁
// 瀹夸富鑷繁鐨勯攣锛坲e5_nte_adapter 鐨?State::mutex锛夛紝鍦ㄦ淳鍙戦噷璋冪敤鏄崱姝荤殑鐩存帴瀚岀枒銆?
// 鍚嶅瓧瑙ｆ瀽銆丠P 璇诲彇銆佹棩蹇楀叏閮ㄦ帹杩熷埌 on_draw 鐨?ConsumePendingCapture銆?
static unsigned char  g_pend_raw[0x78];
static std::uintptr_t g_pend_recv = 0;
static std::uintptr_t g_pend_func = 0;
static const char*    g_pend_name = nullptr;
static int            g_pend_fresh = 0;

static void PendCapture(const unsigned char* raw, std::uintptr_t receiver,
                        std::uintptr_t function, const char* name) {
    std::memcpy(g_pend_raw, raw, sizeof(g_pend_raw));
    g_pend_recv = receiver;
    g_pend_func = function;
    g_pend_name = name;
    g_pend_fresh = 1;
}

// ue5.process-event 璁㈤槄鍥炶皟閲岃蛋杩欐潯璺細鍙瘮 UFunction 鎸囬拡銆?
static void ApplyTargetCapture(std::uintptr_t function, std::uintptr_t object, void* parameters) {
    if (g_cap_on == 0 || g_cap_suppress != 0 || parameters == nullptr || function < 0x10000) return;
    const int idx = ApplyUfnIndex(function);
    const char* fn_name = nullptr;
    if (idx >= 0) fn_name = g_au_name[idx];
    else if (g_dmg_fn != nullptr && reinterpret_cast<std::uintptr_t>(g_dmg_fn) == function)
        fn_name = "K2_ApplyGameplayEffectToTargetWithClass";
    if (fn_name == nullptr) return;
    ++g_cap_hits;
    PendCapture(static_cast<const unsigned char*>(parameters), object, function, fn_name);
}

// 鍙湪 on_draw锛堝涓荤粰鎻掍欢鐨?UI 鍥炶皟鍩燂級閲岃皟鐢細杩欓噷鎵嶅厑璁哥鍚嶅瓧鏈嶅姟涓庢棩蹇椼€?
static void StoreCapture(const unsigned char* raw, std::uintptr_t receiver,
                         std::uintptr_t function, const char* fn_name) {
    ApplyCap c{};
    std::memcpy(c.raw, raw, sizeof(c.raw));
    c.function = function;
    c.receiver = receiver;
    std::memcpy(&c.ge_class, raw + 0x00, 8);
    std::memcpy(&c.target_asc, raw + 0x08, 8);
    std::memcpy(&c.buff_id, raw + 0x10, 4);
    std::memcpy(&c.buff_num, raw + 0x14, 4);
    DecodeName(c.buff_id, c.buff, sizeof(c.buff));
    ObjectClassName(c.ge_class, c.ge, sizeof(c.ge));
    std::snprintf(c.fn, sizeof(c.fn), "%s", fn_name != nullptr ? fn_name : "?");
    c.ev = raw[0x68];
    c.hp_ok = ReadAscHP(c.target_asc, &c.hp) ? 1 : 0;
    c.ms = NowMs();
    // FBufData.DynamicGrantedTags @ raw+0x18锛欶GameplayTagContainer = 涓や釜 TArray
    {
        std::uintptr_t data = 0;
        int num = 0;
        std::memcpy(&data, raw + 0x18, 8);
        std::memcpy(&num, raw + 0x20, 4);
        if (data >= 0x10000 && num > 0 && num <= 8) {
            for (int i = 0; i < num; ++i) {
                unsigned char t8[8] = {0};
                __try { std::memcpy(t8, reinterpret_cast<const void*>(data + i * 8), 8); }
                __except (EXCEPTION_EXECUTE_HANDLER) { break; }
                unsigned int tid = 0;
                std::memcpy(&tid, t8, 4);
                if (tid == 0) continue;
                std::memcpy(c.tag_raw + c.tag_raw_n * 8, t8, 8);
                ++c.tag_raw_n;
                if (c.tag_n < 4) {
                    DecodeName(tid, c.tags[c.tag_n], sizeof(c.tags[0]));
                    if (c.tags[c.tag_n][0] != 0) ++c.tag_n;
                }
            }
        }
    }
    // PE 璁㈤槄涓?exec thunk 浼氱湅鍒板悓涓€娆¤皟鐢紝閫愬瓧鑺傜浉鍚岀殑蹇収鍙瓨涓€娆°€?
    if (g_cap_last >= 0) {
        const ApplyCap& prev = g_cap[g_cap_last];
        if (prev.function == c.function && prev.receiver == c.receiver &&
            std::memcmp(prev.raw, c.raw, sizeof(c.raw)) == 0) {
            ++g_cap_dup;
            return;
        }
    }
    if (c.hp_ok != 0) {
        for (int i = 0; i < g_cap_n; ++i) {
            const int k = (g_cap_last - i + kCapRing) % kCapRing;
            if (g_cap[k].hp_ok != 0 && g_cap[k].target_asc == c.target_asc) {
                c.hp_delta = g_cap[k].hp - c.hp;
                break;
            }
        }
    }
    g_cap_last = (g_cap_last + 1) % kCapRing;
    g_cap[g_cap_last] = c;
    if (g_cap_n < kCapRing) ++g_cap_n;
    if (g_cap_log_left <= 0) return;

    --g_cap_log_left;
    char b[256];
    std::snprintf(b, sizeof(b),
                  "cap[%d]: fn=%s ge=%s(0x%llX) recv=0x%llX tgt=0x%llX ev=%u buff=%s(#%d) hp=%.1f d=%.1f",
                  g_cap_last, c.fn, c.ge, (unsigned long long)c.ge_class,
                  (unsigned long long)c.receiver, (unsigned long long)c.target_asc,
                  static_cast<unsigned>(c.ev), c.buff, c.buff_num, c.hp, c.hp_delta);
    PushLog(b);
    {
        int   lvl = 0;
        int   stack_count = 0;
        float dur = 0.0f, mdur = 0.0f, per = 0.0f, mul = 0.0f, add = 0.0f;
        long long start = 0;
        std::memcpy(&lvl, raw + 0x38, 4);
        std::memcpy(&dur, raw + 0x3C, 4);
        std::memcpy(&mdur, raw + 0x40, 4);
        std::memcpy(&per, raw + 0x44, 4);
        std::memcpy(&stack_count, raw + 0x48, 4);
        std::memcpy(&mul, raw + 0x4C, 4);
        std::memcpy(&add, raw + 0x50, 4);
        std::memcpy(&start, raw + 0x58, 8);
        std::snprintf(b, sizeof(b),
                      "cap[%d]m: lvl=%d dur=%.2f mdur=%.2f per=%.2f stack=%d mul=%.3f add=%.3f "
                      "pred=%u start=%lld hpok=%d tagN=%d/%d tags=%s|%s|%s|%s",
                      g_cap_last, lvl, dur, mdur, per, stack_count, mul, add,
                      static_cast<unsigned>(raw[0x69]), start, c.hp_ok, c.tag_n, c.tag_raw_n,
                      c.tags[0], c.tags[1], c.tags[2], c.tags[3]);
        PushLog(b);
    }
    if (g_cap_logged < 8) {          // 鍓嶅嚑娆￠檮鍘熷瀛楄妭锛屼究浜庝笌 Dump 閫愬瓧鑺傚鐓?
        ++g_cap_logged;
        for (int row = 0; row < 2; ++row) {
            char hx[256];
            int n = std::snprintf(hx, sizeof(hx), "cap[%d]hex%d:", g_cap_last, row);
            for (int i = 0; i < 0x3C; ++i) {
                n += std::snprintf(hx + n, sizeof(hx) - static_cast<std::size_t>(n),
                                   " %02X", raw[row * 0x3C + i]);
            }
            PushLog(hx);
        }
    }
}

// 姣忓抚鎶婇挬瀛愮暀涓嬬殑寰呭鐞嗗揩鐓ц惤鎴愪竴娆℃崟鑾枫€?
static void ConsumePendingCapture() {
    if (g_pend_fresh == 0) return;
    unsigned char raw[0x78];
    std::memcpy(raw, g_pend_raw, sizeof(raw));
    const std::uintptr_t recv = g_pend_recv;
    const std::uintptr_t fn = g_pend_func;
    const char* name = g_pend_name;
    g_pend_fresh = 0;
    StoreCapture(raw, recv, fn, name);
}

// ===== 鍛戒腑鐜板満鏅煡锛氫激瀹冲彂鐢熼偅涓€鍒绘父鎴忓疄闄呰皟鐢ㄤ簡鍝簺 UFunction =====
// 涔嬫墍浠ラ渶瑕佸畠锛氳矾绾?A 鐨勮闃呮椿鐫€锛坧e: total 鍦ㄦ定锛変絾 caphit 涓€鐩存槸 0锛岃鏄庢父鎴忔牴鏈?
// 涓嶈蛋閭?8 涓?Apply* UFunction 鏉ユ寕浼ゅ銆傚洖璋冮噷鍙啓涓や釜鎸囬拡锛堜笉纰板涓绘湇鍔★級锛?
// 瑙﹀彂鐐圭敤瀹夸富鑷繁鐨勪激瀹充簨浠舵祦锛圕haracterOnDamaged 閽╁瓙锛夛紝鍚嶅瓧鍦?on_draw 閲岃В鏋愩€?
static constexpr int kPeRing = 4096;
static std::uintptr_t g_pe_ring_fn[kPeRing];
static std::uintptr_t g_pe_ring_obj[kPeRing];
static unsigned long long g_pe_ring_seq = 0;
static int g_census_hits = 0;
static unsigned long long g_dmg_seq = 0;
static int g_dmg_seq_init = 0;
static int g_census_reported = 0;

static void PeRingPush(std::uintptr_t function, std::uintptr_t object) {
    const int slot = static_cast<int>(g_pe_ring_seq % kPeRing);
    g_pe_ring_fn[slot] = function;
    g_pe_ring_obj[slot] = object;
    ++g_pe_ring_seq;
}

// 伤害事件里的 victim 是宿主编码过的 UE 对象句柄：低 32 位 = GObjects 下标 + 1，
// 高 32 位 = serial（见 adapter EncodeObjectHandle）。这里只还原 actor 指针，
// 序列号由后续「类指针/HP」校验兜底；解不出就跳过这一发，不猜。
// GObjects 按宿主 registry 布局取对象：items 数组 + 每 chunk 0x10000 项、每项 0x18 字节、
// 对象指针在项首（profile: itemsOffset 16 / chunkSize 65536 / itemStride 24 / objectOffset 0）。
// 句柄解码必须用这个布局，才能和宿主索引对齐；访问器函数走的是另一套数组。
static std::uintptr_t g_gobjects_items = 0;

// 句柄解码的 items 指针要延迟解析：插件在游戏启动早期加载时 GObjects 还没建好，
// on_load 里读一次会拿到 0，之后所有句柄都解不出来。这里按需重试，成功后缓存。
static bool ResolveGObjectsItems() {
    if (g_gobjects_items != 0) return true;
    const std::uintptr_t fn = static_cast<std::uintptr_t>(g_sig_addr);
    if (fn < 0x10000) return false;
    std::uintptr_t items = 0;
    __try {
        const int disp = *reinterpret_cast<const int*>(fn + 3);
        const std::uintptr_t items_addr = fn + 7 + (std::intptr_t)disp;
        if (!ue::Reader::SafePtr(items_addr, &items) || items == 0) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    g_gobjects_items = items;
    char b[96];
    std::snprintf(b, sizeof(b), "ue: gobjects items=0x%llX",
                  (unsigned long long)g_gobjects_items);
    PushLog(b);
    return true;
}

static void* ObjectAtLayout(std::uint32_t index) {
    if (g_gobjects_items == 0 && !ResolveGObjectsItems()) return nullptr;
    const std::uint32_t page = index >> 16;
    const std::uint32_t slot = index & 0xFFFFu;
    std::uintptr_t chunk = 0;
    if (!ue::Reader::SafePtr(g_gobjects_items + static_cast<std::uintptr_t>(page) * 8, &chunk) ||
        chunk == 0)
        return nullptr;
    const std::uintptr_t item = chunk + static_cast<std::uintptr_t>(slot) * 0x18;
    std::uintptr_t object = 0;
    if (!ue::Reader::SafePtr(item, &object)) return nullptr;
    return reinterpret_cast<void*>(object);
}

static std::uintptr_t ActorFromDamageHandle(AnomalyGenerationHandleV1 handle) {
    if (handle.id == 0) return 0;
    const std::uint32_t raw = static_cast<std::uint32_t>(handle.id & 0xFFFFFFFFull);
    if (raw == 0) return 0;
    std::uintptr_t actor = reinterpret_cast<std::uintptr_t>(ObjectAtLayout(raw - 1u));
    if (actor < 0x10000)
        actor = reinterpret_cast<std::uintptr_t>(g_ue.ObjectAt(raw - 1u));
    return actor >= 0x10000 ? actor : 0;
}

// 事件驱动武装（KB 11.10 的第 1、2 条）：这一击打中谁，就取谁的
// LastDamageGEDef(0x2378) → +0x10 的 UClass 作为回放 GE，连同当前 HP 记进回放表。
// 自喂（回放自己造成的伤害）由 ReplayArm 的「同一 victim + 同一 GE」短路挡掉。
static void ArmFromDamageEvent(const AnomalyNteDamageEventV1& ev) {
    if (ev.final_damage <= 0) return;
    // 高倍速下回放自己每秒产生上百个伤害事件：victim 句柄与我们记录的一致就直接
    // 认作自喂，不解析对象/名字（O(1)），否则每秒上百次 DecodeName 会拖慢 UI 线程。
    bool known_target = false;
    for (int i = 0; i < g_rt_n; ++i) {
        if (g_rt[i].victim_id != 0 && g_rt[i].victim_id == ev.victim.id) {
            g_rt[i].last_hit_ms = NowMs();
            known_target = true;
            break;
        }
    }
    const std::uintptr_t victim = ActorFromDamageHandle(ev.victim);
    // 诊断（前 10 发）：句柄 id、布局解码结果、最终 actor、ASC、GE、HP。
    static int ev_diag = 0;
    if (ev_diag < 10 && ev.victim.id != 0) {
        ++ev_diag;
        const std::uint32_t idx = static_cast<std::uint32_t>(ev.victim.id & 0xFFFFFFFFull);
        const std::uintptr_t lay = idx != 0
            ? reinterpret_cast<std::uintptr_t>(ObjectAtLayout(idx - 1u)) : 0;
        std::uintptr_t dasc = 0, dge = 0;
        float dhp = 0.0f;
        if (victim >= 0x10000) {
            if (!ue::Reader::SafePtr(victim + 0x940, &dasc)) dasc = 0;
            if (dasc >= 0x10000) dge = LastDamageGeClass(dasc);
            if (dasc >= 0x10000) ReadAscHP(dasc, &dhp);
        }
        char db[224];
        std::snprintf(db, sizeof(db),
                      "ev[%d]: seq=%llu fin=%lld vid=0x%llX items=0x%llX lay=0x%llX v=0x%llX "
                      "asc=0x%llX ge=0x%llX hp=%.1f",
                      ev_diag, (unsigned long long)ev.sequence,
                      static_cast<long long>(ev.final_damage),
                      (unsigned long long)ev.victim.id,
                      (unsigned long long)g_gobjects_items, (unsigned long long)lay,
                      (unsigned long long)victim, (unsigned long long)dasc,
                      (unsigned long long)dge, dhp);
        PushLog(db);
    }
    if (victim < 0x10000) return;
    if (victim == g_player_pawn || victim == g_player_pawn_ref) return;
    std::uintptr_t asc = 0;
    if (!ue::Reader::SafePtr(victim + 0x940, &asc) || !AscLooksLikeAsc(asc))
        asc = FindAscInActor(victim);
    if (asc < 0x10000) return;
    const std::uintptr_t ge = LastDamageGeClass(asc);
    if (ge < 0x10000) return;
    float hp = 0.0f;
    if (!ReadAscHP(asc, &hp) || hp <= 0.0f) return;
    char name[48] = {0};
    if (!known_target) ObjectClassName(victim, name, sizeof(name));
    // 玩家自己不能进表：受伤事件里玩家也是 victim，进表后 MonTrack 会把它当目标回放。
    {
        const std::uintptr_t pasc = PlayerAscForApply();
        if (pasc >= 0x10000 && asc == pasc) return;
        if ((name[0] == 'p' || name[0] == 'P') && std::strstr(name, "layer") != nullptr) return;
    }
    // 和挑战 boss 走同一条路：把事件解析出的受害者也放进扫描表，由 MonTrackTick
    // 在 HP 掉血时刻重新读 LastDamageGEDef 再武装（GE 取的是命中当时的那个）。
    {
        bool known = false;
        for (int k = 0; k < g_mon_list_n; ++k) {
            if (g_mon_list[k] == asc) { known = true; break; }
        }
        if (!known && g_mon_list_n < kMonList) {
            g_mon_list[g_mon_list_n] = asc;
            std::snprintf(g_mon_list_name[g_mon_list_n], sizeof(g_mon_list_name[0]), "%s",
                          name[0] ? name : "-");
            g_mon_list_hp[g_mon_list_n] = hp;
            ++g_mon_list_n;
        }
    }
    ReplayArm(asc, name, ge, hp, ev.victim.id, static_cast<float>(ev.final_damage));
}

struct PeNameCount { char name[64]; char obj[48]; int count; };

// 高倍速下（例如 100 发/秒）每帧要清掉积压的伤害事件，否则宿主 512 条的环会溢出、
// next_damage_event 直接返回 NOT_FOUND，武装就永久停摆。expired 时用 latest 重新对齐。
static void DrainDamageEvents(const AnomalyNteCombatServiceV1* combat, int limit) {
    for (int n = 0; n < limit; ++n) {
        AnomalyNteDamageEventV1 ev{};
        ev.struct_size = sizeof(ev);
        if (combat->next_damage_event(combat->user, g_dmg_seq, &ev).code ==
            ANOMALY_STATUS_V1_OK) {
            g_dmg_seq = ev.sequence;
            ArmFromDamageEvent(ev);
            continue;
        }
        if (combat->latest_damage_sequence != nullptr) {
            const std::uint64_t latest = combat->latest_damage_sequence(combat->user);
            if (latest > g_dmg_seq) {
                static std::uint64_t last_log_ms = 0;
                const std::uint64_t now = NowMs();
                if (now - last_log_ms >= 1000) {
                    last_log_ms = now;
                    char b[112];
                    std::snprintf(b, sizeof(b), "hit: resync cursor %llu -> %llu",
                                  (unsigned long long)g_dmg_seq,
                                  (unsigned long long)latest);
                    PushLog(b);
                }
                g_dmg_seq = latest;
            }
        }
        return;
    }
}

static void CensusTick() {
    if (g_cap_on == 0) return;
    for (int i = 0; i < kPeelMax; ++i) {
        if (g_peel_calls[i] == 0 || g_peel_logged[i] != 0) continue;
        g_peel_logged[i] = 1;
        char tb[176];
        std::snprintf(tb, sizeof(tb), "thunk[%d]: FIRST call n=%llu fn=%s",
                      i, g_peel_calls[i], g_peel_name[i]);
        PushLog(tb);
    }
    if (!g_ctx || !g_ctx->nte_combat) {
        if (g_census_reported == 0) { g_census_reported = 1; PushLog("hit: no combat service"); }
        return;
    }
    const auto* combat = static_cast<const AnomalyNteCombatServiceV1*>(g_ctx->nte_combat);
    if (combat->next_damage_event == nullptr) return;
    if (g_dmg_seq_init == 0) {
        g_dmg_seq_init = 1;
        g_dmg_seq = combat->latest_damage_sequence != nullptr
            ? combat->latest_damage_sequence(combat->user) : 0;
        char b[96];
        std::snprintf(b, sizeof(b), "hit: watching after seq=%llu", (unsigned long long)g_dmg_seq);
        PushLog(b);
        return;
    }
    if (g_census_hits >= 2) {
        g_census_reported = 2;
        DrainDamageEvents(combat, 16);   // 普查结束后只做武装，每帧最多清 16 条
        return;
    }
    AnomalyNteDamageEventV1 ev{};
    ev.struct_size = sizeof(ev);
    if (combat->next_damage_event(combat->user, g_dmg_seq, &ev).code != ANOMALY_STATUS_V1_OK) return;
    g_dmg_seq = ev.sequence;
    // 每一发真实伤害都尝试武装；下面的普查只打印前两发，不能挡住武装。
    ArmFromDamageEvent(ev);
    ++g_census_hits;
    char b[256];
    std::snprintf(b, sizeof(b), "hit[%d]: seq=%llu dmg=%lld basic=%lld final=%lld flags=0x%X",
                  g_census_hits, (unsigned long long)ev.sequence,
                  static_cast<long long>(ev.display_damage),
                  static_cast<long long>(ev.basic_damage),
                  static_cast<long long>(ev.final_damage), ev.flags);
    PushLog(b);
    LogHitTarget();
    {
        char dn[64] = {0};
        for (int k = 0; k < 2; ++k) {
            const std::uintptr_t asc = (k == 0) ? g_monster_asc : PlayerAscForApply();
            const std::uintptr_t cls = LastDamageGeClass(asc);
            dn[0] = 0;
            if (cls != 0) ObjectNameUtf8(cls, dn, sizeof(dn));
            char gb[176];
            std::snprintf(gb, sizeof(gb), "hit[ge]: side=%s asc=0x%llX defclass=%s(0x%llX)",
                          k == 0 ? "victim" : "player",
                          (unsigned long long)asc, dn[0] ? dn : "-",
                          (unsigned long long)cls);
            PushLog(gb);
        }
    }

    static PeNameCount counts[96];
    int count_n = 0;
    int dropped = 0;
    int total_events = 0;
    const unsigned long long total = g_pe_ring_seq;
    const unsigned long long start = total > (unsigned long long)kPeRing
        ? total - (unsigned long long)kPeRing : 0;
    char fname[64] = {0};
    char oname[48] = {0};
    // 鍙粺璁°€屽彲鑳藉拰浼ゅ缁撶畻鏈夊叧銆嶇殑鍚嶅瓧锛岄伩鍏嶈鍔ㄧ敾/杞藉叿 tick 娣规病銆?
    static const char* const kInteresting[] = {
        "Apply", "Gameplay", "Ability", "Event", "Damage", "Buff", "Hit",
        "Attack", "Skill", "Weapon", "Tag", "Exec", "Montage", "Spec", "Effect", "ASC",
    };
    for (unsigned long long s = start; s < total; ++s) {
        const int slot = static_cast<int>(s % kPeRing);
        const std::uintptr_t fn = g_pe_ring_fn[slot];
        const std::uintptr_t obj = g_pe_ring_obj[slot];
        if (fn < 0x10000) continue;
        ++total_events;
        fname[0] = 0;
        unsigned int nid = 0;
        if (ue::Reader::SafeU32(fn + 0x18, &nid)) DecodeName(nid, fname, sizeof(fname));
        if (fname[0] == 0) continue;
        bool interesting = false;
        for (const char* key : kInteresting) {
            if (std::strstr(fname, key) != nullptr) { interesting = true; break; }
        }
        if (!interesting) continue;
        int found = -1;
        for (int i = 0; i < count_n; ++i) {
            if (std::strcmp(counts[i].name, fname) == 0) { found = i; break; }
        }
        if (found >= 0) { ++counts[found].count; continue; }
        if (count_n >= 96) { ++dropped; continue; }
        std::snprintf(counts[count_n].name, sizeof(counts[0].name), "%s", fname);
        oname[0] = 0;
        ObjectClassName(obj, oname, sizeof(oname));
        std::snprintf(counts[count_n].obj, sizeof(counts[0].obj), "%s", oname);
        counts[count_n].count = 1;
        ++count_n;
    }
    for (int i = 0; i < count_n; ++i) {
        std::snprintf(b, sizeof(b), "hitwin[%d]: n=%-4d fn=%s obj=%s",
                      g_census_hits, counts[i].count, counts[i].name, counts[i].obj);
        PushLog(b);
    }
    std::snprintf(b, sizeof(b), "hitwin[%d]: window=%llu events=%d names=%d dropped=%d",
                  g_census_hits, (unsigned long long)(total - start), total_events,
                  count_n, dropped);
    PushLog(b);
    // 鍐嶈创鍑虹獥鍙ｆ渶鍚?12 鏉★紙涓嶇鍚嶅瓧锛夛紝杩欐牱鍗充娇杩囨护璇嶆病瑕嗙洊鍒颁篃鑳界湅鍒扮揣閭荤殑璋冪敤銆?
    {
        int shown = 0;
        const unsigned long long tail_start = total > 12 ? total - 12 : 0;
        for (unsigned long long s = tail_start; s < total && shown < 12; ++s, ++shown) {
            const int slot = static_cast<int>(s % kPeRing);
            fname[0] = 0;
            unsigned int nid = 0;
            if (ue::Reader::SafeU32(g_pe_ring_fn[slot] + 0x18, &nid)) DecodeName(nid, fname, sizeof(fname));
            oname[0] = 0;
            ObjectClassName(g_pe_ring_obj[slot], oname, sizeof(oname));
            std::snprintf(b, sizeof(b), "hittail[%d]: fn=%s obj=%s",
                          g_census_hits, fname[0] ? fname : "?", oname);
            PushLog(b);
        }
    }
}

// ===== 鎬墿鏋氫妇 =====
// 澶栭儴 KillAura 婧愮爜閲屽彧鏈変袱鐐逛笌鏈?build 鐨?dump 瀵瑰緱涓婏紝鍙栬繖涓ょ偣锛?
//   1) 闄?UWorld::PersistentLevel(0x30) 澶栬繕瑕侀亶鍘?UWorld::Levels(0x01D0, TArray<ULevel*>)锛?
//      鎬墿鍙兘娲诲湪瀛愬叧鍗￠噷锛堟湰鎻掍欢鐨勬棫鎵弿鍙湅 PersistentLevel锛屾墍浠?monsterAsc 鑰佹槸 0锛夛紱
//   2) 鐢ㄧ被鍚嶅墠缂€ mon_/boss_ 璁ゆ晫浜猴紝骞舵帓闄?bullet/Effect銆?
// **涓嶅彇**瀹冪殑 HP 鍋忕Щ锛歚attrComp+0x1A00` 鍦ㄦ湰 build 鏄?ShareDamageToOwnerMul(FScalableFloat)锛?
// HPCurrent 鍦?0x1A38銆丮axHP 鍦?0x1A3C銆丄tk 鍦?0x1A50锛坉ump-鍏抽敭绫?md 涓庡疄娴嬩竴鑷达級銆?
// 鐜╁ pawn 鐨勭嫭绔嬭В鏋愰摼锛歸orld 鈫?OwningGameInstance(0x230) 鈫?LocalPlayers(TArray@0x38)
// 鈫?ULocalPlayer鈫扨layerController(0x30) 鈫?AController::Pawn(0x308)銆?
// 涓嶄緷璧?g_pc_inst / g_player_asc锛堣繖涓や釜鍦ㄩ儴鍒嗕細璇濋噷鏄┖锛屽鑷?token 涓庢柦鍔犻兘澶辫触锛夈€?
static std::uintptr_t ResolvePlayerPawn() {
    std::uintptr_t world = g_gworld_ptr;
    if (world < 0x10000) world = (g_world_fixed >= 0x10000) ? g_world_fixed : ReadWorldFresh();
    if (world < 0x10000) return 0;
    __try {
        const std::uintptr_t gi = *reinterpret_cast<std::uintptr_t*>(world + 0x230);
        if (gi < 0x10000) return 0;
        const std::uintptr_t data = *reinterpret_cast<std::uintptr_t*>(gi + 0x38);
        const int num = *reinterpret_cast<const int*>(gi + 0x40);
        if (data < 0x10000 || num <= 0) return 0;
        const std::uintptr_t lp = *reinterpret_cast<std::uintptr_t*>(data);
        if (lp < 0x10000) return 0;
        const std::uintptr_t pc = *reinterpret_cast<std::uintptr_t*>(lp + 0x30);
        if (pc < 0x10000) return 0;
        if (g_pc_inst == nullptr) g_pc_inst = reinterpret_cast<void*>(pc);
        const std::uintptr_t pawn = *reinterpret_cast<std::uintptr_t*>(pc + 0x308);
        return pawn >= 0x10000 ? pawn : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static bool AscLooksLikeAsc(std::uintptr_t asc) {
    if (asc < 0x10000) return false;
    std::uintptr_t cls = 0;
    if (!ue::Reader::SafePtr(asc + 0x10, &cls) || cls < 0x10000) return false;
    if (g_cls_asc >= 0x10000) return cls == static_cast<std::uintptr_t>(g_cls_asc);
    float hp = 0.0f, mx = 0.0f;
    if (!SafeF32(asc + 0x1A38, &hp) || !SafeF32(asc + 0x1A3C, &mx)) return false;
    return hp > 0.0f && mx >= hp && mx < 1.0e9f;
}

// 缓存指针是否还有效：换场景/重生后旧 ASC 可能已经释放，必须先验类指针再验 MaxHP，
// 否则回放会对着一个已经死掉的对象空放（表现就是“重进后怎么打都不掉血”）。
static bool AscStillValid(std::uintptr_t asc) {
    if (asc < 0x10000) return false;
    std::uintptr_t cls = 0;
    if (!ue::Reader::SafePtr(asc + 0x10, &cls) || cls < 0x10000) return false;
    if (g_cls_asc >= 0x10000 && cls != static_cast<std::uintptr_t>(g_cls_asc)) return false;
    float mx = 0.0f;
    if (!SafeF32(asc + 0x1A3C, &mx) || !(mx > 0.0f) || mx > 1.0e9f) return false;
    return true;
}

// 绫绘寚閽堢獥鍙ｆ壂鎻忥細鎬墿鐨?ASC 瀛楁涓嶄竴瀹氬湪 +0x940锛堥偅鏄?AHTAbilityCharacter 鐨勫竷灞€锛夈€?
static std::uintptr_t FindAscInActor(std::uintptr_t act) {
    if (act < 0x10000 || g_cls_asc < 0x10000) return 0;
    for (std::uintptr_t off = 0x400; off <= 0xC00; off += 8) {
        std::uintptr_t p = 0;
        if (!ue::Reader::SafePtr(act + off, &p) || p < 0x10000) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(p + 0x10, &cls)) continue;
        if (cls == static_cast<std::uintptr_t>(g_cls_asc)) return p;
    }
    return 0;
}

// 鍒嗗抚鎵弿锛埪?.1锛氭寜閽洖璋冮噷鍋氬叏琛ㄩ亶鍘嗕細鎶婃父鎴忓崱姝伙級銆傛瘡甯ф渶澶氬鐞?kMonBudget 涓?actor銆?
static constexpr int kMonBudget = 128;
static int g_mon_on = 0;
static int g_mon_level_n = 0;
static int g_mon_level_i = 0;
static unsigned int g_mon_actor_i = 0;
static unsigned int g_mon_actor_n = 0;
static std::uintptr_t g_mon_levels[64] = {};
static std::uintptr_t g_mon_arr = 0;
static int g_mon_found = 0;
static int g_mon_scanned = 0;
static std::uintptr_t g_mon_world = 0;   // 本轮扫描用的 UWorld，换了世界/副本就重扫
// 涓囦竴 mon_/boss_ 杩欏鍛藉悕鍦ㄦ湰 build 涓嶆垚绔嬶細鎶婃壂鍒扮殑鍏朵粬绫诲悕鍘婚噸鐣欏嚑涓紝
// found==0 鏃舵墦鍑烘潵锛屽氨鑳界洿鎺ョ湅鍒版€墿鐪熸鐨勫懡鍚嶈鍒欍€?
static char g_mon_seen[16][48] = {};
static int g_mon_seen_n = 0;

static void RememberClassName(const char* nm) {
    if (nm == nullptr || nm[0] == 0 || g_mon_seen_n >= 16) return;
    for (int i = 0; i < g_mon_seen_n; ++i) {
        if (std::strcmp(g_mon_seen[i], nm) == 0) return;
    }
    std::snprintf(g_mon_seen[g_mon_seen_n], sizeof(g_mon_seen[0]), "%s", nm);
    ++g_mon_seen_n;
}

void RequestMonsterScan() {
    g_mon_on = 1;
    g_mon_level_n = 0;
    g_mon_level_i = 0;
    g_mon_actor_i = 0;
    g_mon_actor_n = 0;
    g_mon_arr = 0;
    g_mon_found = 0;
    g_mon_scanned = 0;
    g_mon_seen_n = 0;
    g_monster_asc = 0;
    g_monster_actor = 0;
    g_mon_list_n = 0;
    PushLog("mon: scan start");
}

static bool BuildMonsterLevelList(std::uintptr_t world) {
    g_mon_level_n = 0;
    std::uintptr_t persistent = 0;
    if (ue::Reader::SafePtr(world + 0x30, &persistent) && persistent >= 0x10000)
        g_mon_levels[g_mon_level_n++] = persistent;
    std::uintptr_t data = 0;
    unsigned int num = 0;
    if (ue::Reader::SafePtr(world + 0x1D0, &data) &&
        ue::Reader::SafeU32(world + 0x1D8, &num) && data >= 0x10000) {
        const unsigned int cap = num < 63u ? num : 63u;
        for (unsigned int i = 0; i < cap && g_mon_level_n < 64; ++i) {
            std::uintptr_t lv = 0;
            if (!ue::Reader::SafePtr(data + 8ull * i, &lv) || lv < 0x10000) continue;
            bool dup = false;
            for (int k = 0; k < g_mon_level_n; ++k) {
                if (g_mon_levels[k] == lv) { dup = true; break; }
            }
            if (!dup) g_mon_levels[g_mon_level_n++] = lv;
        }
    }
    char hb[128];
    std::snprintf(hb, sizeof(hb), "mon: levels=%d world=0x%llX",
                  g_mon_level_n, (unsigned long long)world);
    PushLog(hb);
    g_mon_world = world;
    return g_mon_level_n > 0;
}

// 按继承链判断 UClass 是否派生自某个基类（UStruct::SuperStruct @0x40）。
static bool ClassDerivesFrom(std::uintptr_t cls, std::uintptr_t base) {
    if (cls < 0x10000 || base < 0x10000) return false;
    for (int depth = 0; depth < 16 && cls >= 0x10000; ++depth) {
        if (cls == base) return true;
        std::uintptr_t super = 0;
        if (!ue::Reader::SafePtr(cls + 0x40, &super)) return false;
        cls = super;
    }
    return false;
}

void TickMonsterScan() {
    if (g_mon_on == 0) return;
    if (g_gworld_ptr == 0) { g_mon_on = 0; PushLog("mon: no GWorld"); return; }
    const std::uintptr_t world = (g_world_fixed >= 0x10000) ? g_world_fixed : ReadWorldFresh();
    if (world < 0x10000) { g_mon_on = 0; PushLog("mon: world unresolved"); return; }
    if (g_mon_level_n == 0) {
        if (!BuildMonsterLevelList(world)) { g_mon_on = 0; return; }
        return;
    }
    int budget = kMonBudget;
    char nm[72] = {0};
    while (budget > 0 && g_mon_level_i < g_mon_level_n) {
        if (g_mon_arr == 0) {
            std::uintptr_t arr = 0;
            unsigned int num = 0;
            if (!ue::Reader::SafePtr(g_mon_levels[g_mon_level_i] + 0xA0, &arr) ||
                !ue::Reader::SafeU32(g_mon_levels[g_mon_level_i] + 0xA8, &num) ||
                arr < 0x10000 || num == 0) {
                ++g_mon_level_i;
                g_mon_actor_i = 0;
                g_mon_actor_n = 0;
                continue;
            }
            g_mon_arr = arr;
            g_mon_actor_i = 0;
            g_mon_actor_n = num < 6000u ? num : 6000u;
        }
        if (g_mon_actor_i >= g_mon_actor_n) {
            ++g_mon_level_i;
            g_mon_arr = 0;
            g_mon_actor_i = 0;
            g_mon_actor_n = 0;
            continue;
        }
        std::uintptr_t act = 0;
        const unsigned int i = g_mon_actor_i++;
        --budget;
        if (g_mon_found >= 12) { g_mon_actor_i = g_mon_actor_n; continue; }
        if (!ue::Reader::SafePtr(g_mon_arr + 8ull * i, &act) || act < 0x10000) continue;
        std::uintptr_t cls = 0;
        if (!ue::Reader::SafePtr(act + 0x10, &cls) || cls < 0x10000) continue;
        ++g_mon_scanned;
        unsigned int nid = 0;
        if (!ue::Reader::SafeU32(cls + 0x18, &nid)) continue;
        nm[0] = 0;
        DecodeName(nid, nm, sizeof(nm));
        if (nm[0] == 0) continue;
        // 椤烘墜鎹炵帺瀹?pawn/ASC锛歸orld鈫扜ameInstance鈫扡ocalPlayer鈫扨C 閭ｆ潯閾惧湪鏈?build 涓?
        // 鍙栦笉鍒帮紙pawn=0x0锛夛紝浣嗚繖娆?Actor 鏋氫妇鏄彲闈犵殑銆?
        if ((nm[0] == 'p' || nm[0] == 'P') && std::strstr(nm, "layer") != nullptr) {
            std::uintptr_t pasc = 0;
            if (ue::Reader::SafePtr(act + 0x940, &pasc) && AscLooksLikeAsc(pasc)) {
                const bool changed_player = (g_player_asc != pasc);
                g_player_asc = pasc;
                g_player_pawn = act;
                if (changed_player) {
                    char pb[160];
                    std::snprintf(pb, sizeof(pb), "mon: player %s actor=0x%llX asc=0x%llX",
                                  nm, (unsigned long long)act, (unsigned long long)pasc);
                    PushLog(pb);
                }
            }
        }
        const bool hostile = std::strstr(nm, "mon_") != nullptr ||
                             std::strstr(nm, "boss_") != nullptr ||
                             std::strstr(nm, "Mon_") != nullptr ||
                             std::strstr(nm, "Boss_") != nullptr ||
                             ClassDerivesFrom(cls, (std::uintptr_t)g_cls_ai) ||
                             ClassDerivesFrom(cls, (std::uintptr_t)g_cls_mon);
        if (!hostile) { RememberClassName(nm); continue; }
        if (std::strstr(nm, "bullet") != nullptr || std::strstr(nm, "Effect") != nullptr) continue;
        std::uintptr_t asc = 0;
        if (!ue::Reader::SafePtr(act + 0x940, &asc) || !AscLooksLikeAsc(asc))
            asc = FindAscInActor(act);
        float hp = 0.0f, maxhp = 0.0f, atk = 0.0f;
        const bool hp_ok = SafeF32(asc + 0x1A38, &hp);
        SafeF32(asc + 0x1A3C, &maxhp);
        SafeF32(asc + 0x1A50, &atk);
        char b[224];
        std::snprintf(b, sizeof(b),
                      "mon[%d]: %s actor=0x%llX asc=0x%llX hp=%.1f/%.1f atk=%.1f hpok=%d",
                      g_mon_found, nm, (unsigned long long)act, (unsigned long long)asc,
                      hp, maxhp, atk, hp_ok ? 1 : 0);
        PushLog(b);
        ++g_mon_found;
        if (asc >= 0x10000 && hp > 0.0f) {
            // 记进候选表：自动流程靠"谁的 HP 掉了"来认受害者，而不是靠先扫到的第一只。
            bool known = false;
            for (int k = 0; k < g_mon_list_n; ++k) {
                if (g_mon_list[k] == asc) { known = true; break; }
            }
            if (!known && g_mon_list_n < kMonList) {
                g_mon_list[g_mon_list_n] = asc;
                std::snprintf(g_mon_list_name[g_mon_list_n], sizeof(g_mon_list_name[0]), "%s", nm);
                g_mon_list_hp[g_mon_list_n] = hp;
                ++g_mon_list_n;
            }
            if (g_monster_asc == 0) {
                g_monster_asc = asc;
                g_monster_actor = act;
            }
        }
    }
    if (g_mon_level_i >= g_mon_level_n) {
        g_mon_on = 0;
        char sb[128];
        std::snprintf(sb, sizeof(sb), "mon: done found=%d scanned=%d picked=0x%llX",
                      g_mon_found, g_mon_scanned, (unsigned long long)g_monster_asc);
        PushLog(sb);
        if (g_mon_found == 0) {
            for (int i = 0; i < g_mon_seen_n; ++i) {
                std::snprintf(sb, sizeof(sb), "mon: seen cls[%d]=%s", i, g_mon_seen[i]);
                PushLog(sb);
            }
        }
    }
}

// 鍛戒腑鐬棿锛氱帺瀹?ASC 鐨?m_AttackTarget(0x2388) + 瀹冪殑 HPCurrent锛岀敤鏉ョ‘璁?HP 鍋忕Щ涓庣洰鏍囥€?
static void LogHitTarget() {
    std::uintptr_t player_asc = g_player_asc;
    if (player_asc < 0x10000) {
        std::uintptr_t pawn = 0, asc = 0;
        pawn = ResolvePlayerPawn();
        if (pawn >= 0x10000) ue::Reader::SafePtr(pawn + 0x940, &asc);
        player_asc = asc;
    }
    if (player_asc < 0x10000) { PushLog("hit[tgt]: no player asc"); return; }
    std::uintptr_t actor = 0;
    if (!ue::Reader::SafePtr(player_asc + 0x2388, &actor) || actor < 0x10000) {
        PushLog("hit[tgt]: no m_AttackTarget");
        return;
    }
    std::uintptr_t asc = 0;
    if (!ue::Reader::SafePtr(actor + 0x940, &asc) || !AscLooksLikeAsc(asc)) asc = FindAscInActor(actor);
    char cname[64] = {0};
    ObjectClassName(actor, cname, sizeof(cname));
    float hp = 0.0f, maxhp = 0.0f, atk = 0.0f;
    const bool ok = SafeF32(asc + 0x1A38, &hp);
    SafeF32(asc + 0x1A3C, &maxhp);
    SafeF32(asc + 0x1A50, &atk);
    char b[224];
    std::snprintf(b, sizeof(b), "hit[tgt]: %s actor=0x%llX asc=0x%llX hp=%.1f/%.1f atk=%.1f ok=%d",
                  cname, (unsigned long long)actor, (unsigned long long)asc,
                  hp, maxhp, atk, ok ? 1 : 0);
    PushLog(b);
}

// ===== 娑堣垂 ApplyEffectContainerSpec 鐨勬崟鑾凤紙鍙湪 on_draw 閲岃В鏋愶級=====
// FGameplayEffectSpec锛欴ef(GE)@0x00銆乨uration@0x50銆丳eriod@0x54銆?
// Modifiers(TArray)@0x1B0銆丼tackCount@0x1C0锛圙ameplayAbilities_structs.hpp锛?
static void ConsumeEcsCapture() {
    if (g_ecs_fresh == 0) return;
    g_ecs_fresh = 0;
    unsigned char raw[0x38];
    std::memcpy(raw, g_ecs_raw, sizeof(raw));
    std::uintptr_t self = g_ecs_self;
    std::uintptr_t target_data = 0, specs = 0, spec0 = 0, def = 0, def_cls = 0;
    int spec_n = 0, mod_n = 0, stack = 0;
    float dur = 0.0f, period = 0.0f;
    __try {
        std::memcpy(&target_data, raw + 0x00, 8);
        std::memcpy(&specs, raw + 0x28, 8);
        std::memcpy(&spec_n, raw + 0x30, 4);
        if (specs >= 0x10000 && spec_n > 0) std::memcpy(&spec0, reinterpret_cast<const void*>(specs), 8);
        if (spec0 >= 0x10000) {
            std::memcpy(&def, reinterpret_cast<const void*>(spec0), 8);
            std::memcpy(&dur, reinterpret_cast<const void*>(spec0 + 0x50), 4);
            std::memcpy(&period, reinterpret_cast<const void*>(spec0 + 0x54), 4);
            std::memcpy(&mod_n, reinterpret_cast<const void*>(spec0 + 0x1B8), 4);
            std::memcpy(&stack, reinterpret_cast<const void*>(spec0 + 0x1C0), 4);
            if (def >= 0x10000) ue::Reader::SafePtr(def + 0x10, &def_cls);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }

    char dn[64] = {0};
    if (def >= 0x10000) ObjectClassName(def, dn, sizeof(dn));
    char objname[48] = {0};
    ObjectClassName(self, objname, sizeof(objname));
    char b[256];
    std::snprintf(b, sizeof(b),
                  "ecs[%llu]: self=%s(0x%llX) tgtData=0x%llX specN=%d spec0=0x%llX",
                  g_ecs_calls, objname, (unsigned long long)self,
                  (unsigned long long)target_data, spec_n, (unsigned long long)spec0);
    PushLog(b);
    std::snprintf(b, sizeof(b),
                  "ecs[%llu]: GE=%s(0x%llX) dur=%.3f per=%.3f mods=%d stack=%d",
                  g_ecs_calls, dn, (unsigned long long)def, dur, period, mod_n, stack);
    PushLog(b);
    for (int row = 0; row < 2; ++row) {
        char hx[224];
        int n = std::snprintf(hx, sizeof(hx), "ecs[%llu]hex%d:", g_ecs_calls, row);
        for (int i = 0; i < 0x1C; ++i) {
            n += std::snprintf(hx + n, sizeof(hx) - static_cast<std::size_t>(n),
                               " %02X", raw[row * 0x1C + i]);
        }
        PushLog(hx);
    }
}

// ===== 璺嚎 B锛氱洿鎺ラ挬浣?exec thunk锛圲Function+0xD8锛?====
// 鍙傛暟鍧?= FFrame::Locals = *(void**)(stack + 0x20)銆傚亸绉讳笉鏄寽鐨勶細閮ㄧ讲 Profile
// nte-current.json 閲?"fstack.locals": 32銆?ufunction.execFunction": 216锛?
// 瀹夸富鑷繁鐨?combat exec 鎹曡幏涔熸槸鐢ㄥ悓涓€涓亸绉昏鍙傛暟銆?
using ExecThunkFn = void (*)(void*, void*, void*);
// 8 涓?Apply* UFunction 鐨?exec thunk 鍏ㄦ寕涓婏紝鍙负鍥炵瓟涓€涓棶棰橈細娓告垙鍒板簳璧板摢涓€涓€?
// 鍙湁 K2_ApplyGameplayEffectToTargetWithClass 鐨勫弬鏁板潡甯冨眬鏄凡鐭ョ殑锛?x78锛岃 Dump锛夛紝
// 鎵€浠ュ彧鏈夊畠鎶勫弬鏁帮紝鍏朵綑 7 涓彧璁拌皟鐢ㄦ鏁般€?
static ExecThunkFn g_peel_orig[kPeelMax] = {};static AnomalyGenerationHandleV1 g_peel_handle[kPeelMax] = {};
static int g_peel_installed[kPeelMax] = {};
static int g_peel_capture[kPeelMax] = {};
static std::uintptr_t g_peel_ufn[kPeelMax] = {};
static const void* g_thunk_svc = nullptr;
static int g_thunk_state = 0;          // 宸插畨瑁呯殑 thunk 鏁?
static unsigned long long g_thunk_calls = 0;
static int g_thunk_bad = 0;

// ApplyEffectContainerSpec锛坔ost 鐪熸鐢ㄦ潵鎸備激瀹崇殑涓€灞傦級鐨勫弬鏁板潡锛?
//   FHTGameplayEffectContainerSpec { TargetData @0x00(0x28); TargetGameplayEffectSpecs @0x28(0x10) }
// 鎶撳埌瀹冨氨鑳介『鐫€ spec handle 璇诲埌 GE 绫汇€乨uration/period銆乵odifier 鏁伴噺銆乻tack锛?
// 杩欎簺鏄噸鏀句激瀹崇湡姝ｉ渶瑕佺殑涓滆タ锛堜笉鏄?FBufferData 閭ｅ buff 鍙傛暟锛夈€?
template <int N>
static void PeelDetour(void* self, void* stack, void* result) {
    ++g_peel_calls[N];
    ++g_thunk_calls;
    if (g_peel_capture[N] != 0 && g_cap_on != 0 && g_cap_suppress == 0 && stack != nullptr) {
        std::uintptr_t params = 0;
        __try {
            params = *reinterpret_cast<std::uintptr_t*>(
                reinterpret_cast<unsigned char*>(stack) + 0x20);
        } __except (EXCEPTION_EXECUTE_HANDLER) { params = 0; }
        if (params >= 0x10000) {
            if (g_peel_capture[N] == 2) {
                __try { std::memcpy(g_ecs_raw, reinterpret_cast<const void*>(params), sizeof(g_ecs_raw)); }
                __except (EXCEPTION_EXECUTE_HANDLER) { g_ecs_raw[0] = 0; }
                g_ecs_self = reinterpret_cast<std::uintptr_t>(self);
                g_ecs_ufn = g_peel_ufn[N];
                g_ecs_fresh = 1;
                ++g_ecs_calls;
            } else {
                ++g_cap_hits;
                PendCapture(reinterpret_cast<const unsigned char*>(params),
                            reinterpret_cast<std::uintptr_t>(self),
                            g_peel_ufn[N], g_peel_name[N]);
            }
        } else {
            ++g_thunk_bad;
        }
    }
    if (g_peel_orig[N] != nullptr) g_peel_orig[N](self, stack, result);
}

static void* PeelDetourPtr(int index) {
    switch (index) {
    case 0: return reinterpret_cast<void*>(&PeelDetour<0>);
    case 1: return reinterpret_cast<void*>(&PeelDetour<1>);
    case 2: return reinterpret_cast<void*>(&PeelDetour<2>);
    case 3: return reinterpret_cast<void*>(&PeelDetour<3>);
    case 4: return reinterpret_cast<void*>(&PeelDetour<4>);
    case 5: return reinterpret_cast<void*>(&PeelDetour<5>);
    case 6: return reinterpret_cast<void*>(&PeelDetour<6>);
    case 7: return reinterpret_cast<void*>(&PeelDetour<7>);
    default: return nullptr;
    }
}

void ReleaseExecThunk() {
    if (g_thunk_state <= 0) return;
    g_thunk_state = 0;
    const auto* svc = g_thunk_svc != nullptr
        ? static_cast<const AnomalyHookServiceV1*>(g_thunk_svc) : nullptr;
    for (int i = 0; i < kPeelMax; ++i) {
        if (g_peel_installed[i] == 0) continue;
        if (svc != nullptr && svc->release) svc->release(svc->user, g_peel_handle[i]);
        g_peel_installed[i] = 0;
        g_peel_orig[i] = nullptr;
    }
}

void HookExecThunk() {
    if (!g_ctx) return;
    if (g_au_n == 0) { PushLog("thunk: apply ufns not enumerated yet"); return; }
    if (g_thunk_svc == nullptr) {
        g_thunk_svc = LazyService(&g_ctx->hook, "anomaly.interop.hook", 1);
    }
    if (g_thunk_svc == nullptr) { PushLog("thunk: hook service unavailable"); return; }
    const auto* svc = static_cast<const AnomalyHookServiceV1*>(g_thunk_svc);
    if (!svc->create) { PushLog("thunk: no create"); return; }
    int installed = 0;
    for (int i = 0; i < g_au_n && i < kPeelMax; ++i) {
        if (g_peel_installed[i] != 0) continue;
        std::uintptr_t thunk = 0;
        if (!ue::Reader::SafePtr(g_au_fn[i] + 0xD8, &thunk) || thunk < 0x10000) continue;
        AnomalyHookRequestV1 req{};
        req.struct_size = sizeof(req);
        req.kind = ANOMALY_HOOK_V1_FUNCTION;
        req.target = thunk;
        req.detour = PeelDetourPtr(i);
        char label[24];
        std::snprintf(label, sizeof(label), "kx.thunk.%d", i);
        req.label = AnomalyStringViewV1{label, std::strlen(label)};
        AnomalyGenerationHandleV1 h{};
        std::uintptr_t orig = 0;
        const AnomalyStatusV1 st = svc->create(svc->user, &req, &orig, &h);
        std::snprintf(g_peel_name[i], sizeof(g_peel_name[0]), "%s", g_au_name[i]);
        g_peel_ufn[i] = g_au_fn[i];
        if (std::strcmp(g_au_name[i], "K2_ApplyGameplayEffectToTargetWithClass") == 0)
            g_peel_capture[i] = 1;                       // 0x78 FBufferData 閭ｅ潡
        else if (std::strcmp(g_au_name[i], "ApplyEffectContainerSpec") == 0)
            g_peel_capture[i] = 2;                       // 0x38 container spec
        else
            g_peel_capture[i] = 0;
        char b[176];
        std::snprintf(b, sizeof(b), "thunk[%d]: cap=%d fn=%s target=0x%llX rc=%u orig=0x%llX",
                      i, g_peel_capture[i], g_au_name[i], (unsigned long long)thunk,
                      static_cast<unsigned>(st.code), (unsigned long long)orig);
        PushLog(b);
        if (st.code == ANOMALY_STATUS_V1_OK) {
            g_peel_orig[i] = reinterpret_cast<ExecThunkFn>(orig);
            g_peel_handle[i] = h;
            g_peel_installed[i] = 1;
            ++installed;
        }
    }
    g_thunk_state = installed;
    char b[112];
    std::snprintf(b, sizeof(b), "thunk: installed=%d/%d", installed, g_au_n < kPeelMax ? g_au_n : kPeelMax);
    PushLog(b);
}

// ===== 鐢ㄦ崟鑾峰埌鐨勭湡瀹炲弬鏁板鐜版柦鍔?=====
static int g_rp_left = 0;
static int g_rp_total = 0;
static int g_rp_interval = 60;
static int g_rp_rc = 0;
static float g_rp_sum = 0.0f;
static std::uint32_t g_rp_due = 0;

void RequestReplay(int count) {
    if (g_cap_last < 0) { g_rp_rc = -1; PushLog("rp: no capture yet"); return; }
    if (count <= 0) count = 1;
    if (count > 256) count = 256;
    g_rp_left = count;
    g_rp_total = count;
    g_rp_sum = 0.0f;
    g_rp_rc = 0;
    g_rp_due = static_cast<std::uint32_t>(NowMs() + 200);
    char b[144];
    std::snprintf(b, sizeof(b), "rp: arm n=%d idx=%d fn=%s buff=%s tag=%s",
                  count, g_cap_last, g_cap[g_cap_last].fn, g_cap[g_cap_last].buff,
                  g_cap[g_cap_last].tags[0]);
    PushLog(b);
}

void TickApplyCaptureReplay() {
    if (g_rp_left <= 0) return;
    if (g_cap_last < 0) { g_rp_left = 0; g_rp_rc = -1; return; }
    if (NowMs() < g_rp_due) return;
    if (!g_ctx || !g_ctx->ue5_framework) return;
    const auto* fw = static_cast<const AnomalyUe5FrameworkServiceV1*>(g_ctx->ue5_framework);
    const bool game_thread = (fw->is_game_thread == nullptr) || fw->is_game_thread(fw->user) != 0;
    if (!game_thread) {                       // 涓嶅湪 Game 绾跨▼灏卞埆纰?ProcessEvent
        if (g_rp_rc != -5) { g_rp_rc = -5; PushLog("rp: not game thread, waiting"); }
        return;
    }
    const ApplyCap& c = g_cap[g_cap_last];
    if (c.function < 0x10000 || c.receiver < 0x10000 || c.target_asc < 0x10000) {
        g_rp_left = 0; g_rp_rc = -2; PushLog("rp: bad capture pointers"); return;
    }
    std::uintptr_t pe = static_cast<std::uintptr_t>(g_pe_addr);
    if (pe < 0x10000) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
        if (base != 0) pe = base + 0x3E41360;
    }
    if (pe < 0x10000) { g_rp_left = 0; g_rp_rc = -4; PushLog("rp: no ProcessEvent"); return; }
    const std::uintptr_t flags = c.function + 0xB0;
    unsigned int saved = 0;
    if (!ue::Reader::SafeU32(flags, &saved)) {
        g_rp_left = 0; g_rp_rc = -3; PushLog("rp: function flags unreadable"); return;
    }

    unsigned char local[0x78];
    std::memcpy(local, c.raw, sizeof(local));
    std::memset(local + 0x6C, 0, 8);          // ReturnValue 浜ょ粰鏈璋冪敤鑷繁濉?
    // DynamicGrantedTags/ParentTags 鐨?TArray 鎸囬拡鎸囧悜璋冪敤鏂癸紙钃濆浘鏍堝抚锛夌殑鍫嗗唴瀛橈紝
    // 鎹曡幏鏃堕偅浠藉唴瀛樺凡缁忛噴鏀撅紱閲嶆斁蹇呴』鎹㈡垚鎴戜滑鑷繁鐨?FName 鍓湰锛屽惁鍒欐槸鎮瀭鎸囬拡銆?
    std::memset(local + 0x28, 0, 0x10);       // ParentTags
    if (c.tag_raw_n > 0) {
        void* tag_data = const_cast<unsigned char*>(c.tag_raw);
        std::memcpy(local + 0x18, &tag_data, 8);
        std::memcpy(local + 0x20, &c.tag_raw_n, 4);
        std::memcpy(local + 0x24, &c.tag_raw_n, 4);
    } else {
        std::memset(local + 0x18, 0, 0x10);
    }
    float before = 0.0f, after = 0.0f;
    const bool ok_before = ReadAscHP(c.target_asc, &before);
    const bool patch = (saved & 0x400u) == 0; // FUNC_Native锛歅rocessEvent 鎵嶈兘鐩磋皟
    if (patch) {
        DWORD op = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000,
                           PAGE_READWRITE, &op) != 0) {
            *reinterpret_cast<unsigned int*>(flags) = saved | 0x400u;
            VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op, &op);
        }
    }
    g_cap_suppress = 1;
    reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(c.receiver),
                               reinterpret_cast<void*>(c.function), local);
    g_cap_suppress = 0;
    if (patch) {
        DWORD op2 = 0;
        if (VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000,
                           PAGE_READWRITE, &op2) != 0) {
            *reinterpret_cast<unsigned int*>(flags) = saved;
            VirtualProtect(reinterpret_cast<LPVOID>(flags & ~0xFFFULL), 0x2000, op2, &op2);
        }
    }
    const bool ok_after = ReadAscHP(c.target_asc, &after);
    unsigned int handle = 0;
    std::memcpy(&handle, local + 0x6C, 4);
    const unsigned char passed = local[0x70];
    --g_rp_left;
    if (ok_before && ok_after) g_rp_sum += (before - after);
    char b[224];
    std::snprintf(b, sizeof(b), "rp[%d/%d]: hp=%.1f->%.1f d=%.1f pass=%u h=%08X left=%d",
                  g_rp_total - g_rp_left, g_rp_total, before, after, before - after,
                  static_cast<unsigned>(passed), handle, g_rp_left);
    PushLog(b);
    if (ok_after && after <= 0.0f) { g_rp_left = 0; PushLog("rp: target hp<=0, stop"); }
    if (g_rp_left <= 0) {
        std::snprintf(b, sizeof(b), "rp: done n=%d sum=%.1f rc=%d",
                      g_rp_total, g_rp_sum, g_rp_rc);
        PushLog(b);
    } else {
        g_rp_due = static_cast<std::uint32_t>(NowMs() + g_rp_interval);
    }
}

// 鍚敤 = 涓€涓紑鍏崇鍏ㄩ儴锛氭墦寮€ PE 璁㈤槄锛堟柦鍔犲繀椤诲湪璇ュ煙锛夈€佹寔缁嚜鍔ㄦ壂鐩爣銆?
// 盯候选怪的 HP：谁的 HP 掉了，谁就是"你刚打中的那只"，它的 LastDamageGEDef 才是那一击的 GE。
// 自动回放就挂在这里，不再依赖 MonScan 的手动选择。
// Watches every candidate monster's HP: whoever just lost HP is the target that
// was hit, and its own LastDamageGEDef is the GE that hit used.
static void DropMonsterEntry(int index) {
    if (index < 0 || index >= g_mon_list_n) return;
    for (int k = index; k + 1 < g_mon_list_n; ++k) {
        g_mon_list[k] = g_mon_list[k + 1];
        g_mon_list_hp[k] = g_mon_list_hp[k + 1];
        std::snprintf(g_mon_list_name[k], sizeof(g_mon_list_name[0]), "%s",
                      g_mon_list_name[k + 1]);
    }
    --g_mon_list_n;
}

void MonTrackTick() {
    if (g_ui_state.damage == 0 || g_mon_list_n == 0) return;
    static std::uint64_t next = 0;
    const std::uint64_t now = NowMs();
    if (now < next) return;
    next = now + 150;
    for (int i = 0; i < g_mon_list_n; ) {
        float hp = 0.0f;
        if (!ReadAscHP(g_mon_list[i], &hp) || hp <= 0.0f) {
            // 目标已死 / 对象已释放：从候选表移除。旧代码把 prev 恢复成 >0，
            // 会变成每 150ms 一条 "track: no GE ... hp=0.0" 的死循环。
            DropMonsterEntry(i);
            continue;
        }
        const float prev = g_mon_list_hp[i];
        g_mon_list_hp[i] = hp;
        if (prev <= 0.0f || hp >= prev - 0.5f) { ++i; continue; }
        const std::uintptr_t ge = LastDamageGeClass(g_mon_list[i]);
        if (ge == 0) {
            // 读不到 GE（对象刚死/正在切换）：保留新 hp 当基线等下一发，
            // 不再恢复 prev，避免同一条记录无限重试。
            ++i;
            continue;
        }
        g_monster_asc = g_mon_list[i];
        ReplayArm(g_mon_list[i], g_mon_list_name[i], ge, hp);
        ++i;
    }
}

void AutoTargetTick() {
    if (g_ui_state.damage == 0) { g_cap_on = 0; return; }
    g_cap_on = 1;
    static std::uint64_t next_scan_allowed = 0;
    static std::uint64_t scan_started_ms = 0;
    static std::uint64_t next_refresh_ms = 0;
    const std::uint64_t now = NowMs();
    if (g_mon_on != 0) {
        // 扫描卡死看门狗：分帧扫描正常 <1s 结束，超过 5s 一定是状态坏了。
        if (scan_started_ms == 0) {
            scan_started_ms = now;
        } else if (now - scan_started_ms > 5000) {
            g_mon_on = 0;
            scan_started_ms = 0;
            PushLog("mon: scan watchdog reset");
        }
        return;
    }
    scan_started_ms = 0;
    if (next_refresh_ms == 0) next_refresh_ms = now + 15000;
    bool need = (g_mon_list_n == 0);
    if (!need) {
        int alive = 0;
        for (int i = 0; i < g_mon_list_n; ++i) {
            float hp = 0.0f;
            if (ReadAscHP(g_mon_list[i], &hp) && hp > 0.0f) ++alive;
        }
        if (alive == 0) need = true;
    }
    bool world_changed = false;
    if (!need) {
        const std::uintptr_t world =
            (g_world_fixed >= 0x10000) ? g_world_fixed : ReadWorldFresh();
        if (world >= 0x10000 && g_mon_world >= 0x10000 && world != g_mon_world) {
            need = true;                 // 换场景/副本：旧列表里的 ASC 全部作废
            world_changed = true;
        }
    }
    if (!need && now >= next_refresh_ms) need = true;   // 兜底：每 15s 刷新一次
    if (!need) return;
    if (now < next_scan_allowed) return;
    if (world_changed) PushLog("mon: world changed, rescan");
    if (world_changed) {
        // 旧世界的目标是悬空指针：清空回放表和玩家缓存，避免重进世界后继续对已释放的 ASC 施加。
        g_rt_n = 0;
        g_shot_clock_ms = 0;
        g_ge_shots = 0;
        g_monster_asc = 0;
        g_player_asc = 0;
        g_player_pawn = 0;
        g_player_pawn_ref = 0;
        PushLog("mon: world changed, cleared targets");
    }
    RequestMonsterScan();
    next_scan_allowed = now + 3000;   // 扫不到东西也别每帧重扫（旧代码会 300ms 一轮）
    next_refresh_ms = now + 15000;
}

static int g_rep_dmg = 0;
static int g_rep_fail = 0;
static int g_rep_shots = 0;
static int g_rep_ok = 0;   // 成功重放（真正打掉血）的次数

void DrawCaptureUi(const AnomalyUiServiceV1* ui) {
    if (ui == nullptr) return;
    if (ui->checkbox) {
        AnomalyStringViewV1 chain{"\xE5\x80\x8D\xE6\x94\xBB\xE8\xBF\x9E\xE9\x94\x81\xE5\x8F\x8D\xE5\xBA\x94", 18};
        ui->checkbox(ui->user, chain, &g_ui_state.chain);
    }
    if (ui->checkbox) {
        AnomalyStringViewV1 en{"\xE5\x90\xAF\xE7\x94\xA8", 6};
        ui->checkbox(ui->user, en, &g_ui_state.damage);
    }
    if (ui->input_uint32) {
        unsigned int value = static_cast<unsigned int>(g_ui_state.replay);
        AnomalyStringViewV1 id{"##replay_count", 14};
        if (ui->input_uint32(ui->user, id, &value, 1, 1024))
            g_ui_state.replay = static_cast<float>(value);
        if (ui->same_line && ui->button) {
            ui->same_line(ui->user, 0.0f, -1.0f);
            AnomalyStringViewV1 minus{"-", 1};
            if (ui->button(ui->user, minus, 0.0f, 0.0f)) {
                const int v = static_cast<int>(g_ui_state.replay);
                if (v > 1) g_ui_state.replay = static_cast<float>(v - 1);
            }
            ui->same_line(ui->user, 0.0f, -1.0f);
            AnomalyStringViewV1 plus{"+", 1};
            if (ui->button(ui->user, plus, 0.0f, 0.0f)) {
                const int v = static_cast<int>(g_ui_state.replay);
                if (v < 1024) g_ui_state.replay = static_cast<float>(v + 1);
            }
            ui->same_line(ui->user, 0.0f, -1.0f);
            if (ui->text) {
                AnomalyStringViewV1 cap{
                    "\xE4\xBC\xA4\xE5\xAE\xB3\xE9\x87\x8D\xE5\xA4\x8D\xE6\xAC\xA1\xE6\x95\xB0", 18};
                ui->text(ui->user, cap);
            }
        }
    }
    if (ui->text) {
        const bool bound = (g_rt_n > 0) && (g_mon_list_n > 0);
        const bool fighting = (g_rt_n > 0);
        char l1[96];
        const int n1 = std::snprintf(
            l1, sizeof(l1), "%s=%s %s=%s",
            "\xE7\xBB\x91\xE5\xAE\x9A", bound ? "\xE5\xB0\xB1\xE7\xBB\xAA" : "\xE6\x9C\xAA\xE5\xB0\xB1\xE7\xBB\xAA",
            "\xE8\xA7\xA6\xE5\x8F\x91", fighting ? "\xE6\x88\x98\xE6\x96\x97\xE4\xB8\xAD" : "\xE5\xBE\x85\xE6\x9C\xBA");
        if (n1 > 0) {
            AnomalyStringViewV1 v{l1, static_cast<std::size_t>(n1)};
            ui->text(ui->user, v);
        }
        char l2[128];
        // 语义：伤害=总次数（所有施加），失败=失败次数（不变），重放=成功重放的次数。
        const int n2 = std::snprintf(
            l2, sizeof(l2), "%s=%d %s=%d %s=%d",
            "\xE4\xBC\xA4\xE5\xAE\xB3", g_rep_shots,
            "\xE5\xA4\xB1\xE8\xB4\xA5", g_rep_fail,
            "\xE9\x87\x8D\xE6\x94\xBE", g_rep_ok);
        if (n2 > 0) {
            AnomalyStringViewV1 v{l2, static_cast<std::size_t>(n2)};
            ui->text(ui->user, v);
        }
    }
}

static std::uintptr_t FindFuncInherited(void* uclass, const char* name) {
    std::uintptr_t cls = reinterpret_cast<std::uintptr_t>(uclass);
    for (int depth = 0; depth < 8 && cls >= 0x10000; ++depth) {
        if (void* f = FindFuncByName(reinterpret_cast<void*>(cls), name))
            return reinterpret_cast<std::uintptr_t>(f);
        std::uintptr_t super = 0;
        if (!ue::Reader::SafePtr(cls + 0x40, &super) || super < 0x10000) break;
        cls = super;
    }
    return 0;
}

static std::uintptr_t PlayerAscForApply() {
    if (g_player_asc >= 0x10000 && AscStillValid(g_player_asc)) return g_player_asc;
    g_player_asc = 0;                          // 失效：换场景/重生后重新解析
    std::uintptr_t pawn = ResolvePlayerPawn();
    if (pawn < 0x10000) pawn = g_player_pawn;
    if (pawn < 0x10000) return 0;
    std::uintptr_t asc = 0;
    if (!ue::Reader::SafePtr(pawn + 0x940, &asc) || !AscLooksLikeAsc(asc)) return 0;
    g_player_asc = asc;
    g_player_pawn = pawn;
    return asc;
}

static HANDLE g_hb_thread = nullptr;
static volatile LONG g_hb_stop = 0;
static DWORD WINAPI HbThread(LPVOID) {
    int n = 0;
    for (;;) {
        Sleep(2000);
        if (g_hb_stop != 0) break;
        char b[160];
        std::snprintf(b, sizeof(b), "hb[%d]: pe=%llu req=%d left=%d fired=%d rc=%d",
                      ++n, g_pe_total, g_ge_req, g_rt_n, g_ge_apply_rc, 0);
        PushLog(b);
    }
    return 0;
}

static void StartHeartbeat() {
    if (g_hb_thread != nullptr) return;
    g_hb_stop = 0;
    g_hb_thread = CreateThread(nullptr, 0, HbThread, nullptr, 0, nullptr);
    PushLog("hb: started");
}

static void StopHeartbeat() {
    if (g_hb_thread == nullptr) return;
    g_hb_stop = 1;
    WaitForSingleObject(g_hb_thread, 5000);
    CloseHandle(g_hb_thread);
    g_hb_thread = nullptr;
    PushLog("hb: stopped");
}

// UFunction 瑙ｆ瀽蹇呴』鍦?UI 绾跨▼鍋氾細瀹冧細璋冪敤瀹夸富鐨?resolve_utf8锛圫tate::mutex锛夛紝
// 鑰屾父鎴忕嚎绋嬫鍒绘澶勫湪瀹夸富鐨?ProcessEvent 娲惧彂鍩熼噷锛岄偅涓攣琚崰鐫€ 鈫?蹇呮閿併€?
// 杩欐潯瑙勫緥鍦ㄦ湰浼氳瘽閲屽洓娆″叏閮ㄥ惢鍚堬紙瑙?KB 搂11.10锛夈€?
static bool ResolveGeHelpersUiThread() {
    if (g_gec_ufn != 0 && g_gea_ufn != 0) return true;
    const std::uintptr_t src = PlayerAscForApply();
    if (src < 0x10000) { PushLog("geapply: helpers not resolved (no asc)"); return false; }
    std::uintptr_t cls = g_cls_asc;
    if (cls < 0x10000) {
        std::uintptr_t c = 0;
        if (ue::Reader::SafePtr(src + 0x10, &c)) cls = c;
    }
    if (cls >= 0x10000) {
        g_gec_ufn = FindFuncInherited(reinterpret_cast<void*>(cls), "MakeEffectContext");
        g_gea_ufn = FindFuncInherited(reinterpret_cast<void*>(cls), "BP_ApplyGameplayEffectToTarget");
    }
    char hb[176];
    std::snprintf(hb, sizeof(hb), "geapply: cls=0x%llX ctxUfn=0x%llX applyUfn=0x%llX",
                  (unsigned long long)cls, (unsigned long long)g_gec_ufn,
                  (unsigned long long)g_gea_ufn);
    PushLog(hb);
    if (g_gec_ufn == 0 || g_gea_ufn == 0) {
        static int dumped = 0;
        if (dumped == 0 && cls >= 0x10000) {
            dumped = 1;
            int shown = 0;
            char nm[64] = {0};
            std::uintptr_t walk = cls;
            for (int depth = 0; depth < 8 && walk >= 0x10000 && shown < 16; ++depth) {
                std::uintptr_t child = 0;
                if (ue::Reader::SafePtr(walk + 0x48, &child)) {
                    for (int g = 0; g < 4096 && child != 0 && shown < 16; ++g) {
                        unsigned int nid = 0;
                        if (ue::Reader::SafeU32(child + 0x18, &nid)) {
                            nm[0] = 0;
                            DecodeName(nid, nm, sizeof(nm));
                            if (nm[0] != 0 &&
                                (std::strstr(nm, "Apply") != nullptr ||
                                 std::strstr(nm, "Effect") != nullptr ||
                                 std::strstr(nm, "Context") != nullptr)) {
                                char fb[128];
                                std::snprintf(fb, sizeof(fb), "geapply fn[%d]: %s 0x%llX",
                                              shown, nm, (unsigned long long)child);
                                PushLog(fb);
                                ++shown;
                            }
                        }
                        std::uintptr_t next = 0;
                        if (!ue::Reader::SafePtr(child + 0x28, &next)) break;
                        child = next;
                    }
                }
                std::uintptr_t super = 0;
                if (!ue::Reader::SafePtr(walk + 0x40, &super) || super < 0x10000) break;
                walk = super;
            }
        }
        return false;
    }
    return true;
}

// 瀵硅薄鑷韩鐨?FName锛堜笉鏄畠鐨勭被鍚嶏級銆俇I 绾跨▼鍙敤銆?
static void ObjectNameUtf8(std::uintptr_t object, char* out, std::size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = 0;
    unsigned int nid = 0;
    if (!ue::Reader::SafeU32(object + 0x18, &nid)) return;
    DecodeName(nid, out, cap);
}

// HTAbilitySystemComponent::LastDamageGEDef @0x2378 鈥斺€?鍙楀鏂?ASC 缂撳瓨鐨勪笂涓€娆′激瀹?GE銆?
// 瀹冪殑 UClass 灏辨槸 BP_ApplyGameplayEffectToTarget 闇€瑕佺殑 TSubclassOf銆?
static std::uintptr_t LastDamageGeClass(std::uintptr_t asc) {
    if (asc < 0x10000) return 0;
    std::uintptr_t def = 0;
    if (!ue::Reader::SafePtr(asc + 0x2378, &def) || def < 0x10000) return 0;
    std::uintptr_t cls = 0;
    if (!ue::Reader::SafePtr(def + 0x10, &cls) || cls < 0x10000) return 0;
    return cls;
}

// 闈㈡澘閲岀殑銆屼激瀹抽噸澶嶆鏁般€嶆槸**姣忕娆℃暟**锛坮ate锛夛紝闂撮殧 = 1000/rate锛屾渶灏?16ms銆?
static int GeRateIntervalMs() {
    int rate = static_cast<int>(g_ui_state.replay);
    if (rate < 1) rate = 1;
    if (rate > 512) rate = 512;
    int ms = 1000 / rate;
    if (ms < 8) ms = 8;      // 上限 125 发/秒；超出的按帧补发，见 ReplayTick 的 shot clock
    return ms;
}

// ---- multi-target replay engine -------------------------------------------
// One activation turn = one context + one application per active target.
// Each target keeps its own GE (taken from its own LastDamageGEDef), so the
// replayed damage always matches the character that is currently attacking and
// the target that is currently being hit.
void ReplayDropTarget(int index) {
    if (index < 0 || index >= g_rt_n) return;
    for (int i = index; i + 1 < g_rt_n; ++i) g_rt[i] = g_rt[i + 1];
    --g_rt_n;
}

void ReplayArm(std::uintptr_t asc, const char* name, std::uintptr_t ge_class, float hp,
               std::uint64_t victim_id, float ref_dmg) {
    if (asc < 0x10000 || ge_class < 0x10000) return;
    int idx = -1;
    for (int i = 0; i < g_rt_n; ++i) {
        if (g_rt[i].asc == asc) { idx = i; break; }
    }
    if (idx < 0) {
        if (g_rt_n >= kMaxReplayTargets) return;
        idx = g_rt_n++;
        g_rt[idx] = ReplayTarget{};
        g_rt[idx].asc = asc;
        g_rt[idx].hp = hp;
        std::snprintf(g_rt[idx].name, sizeof(g_rt[idx].name), "%s", name ? name : "?");
    }
    ReplayTarget& t = g_rt[idx];
    if (victim_id != 0) t.victim_id = victim_id;
    if (ref_dmg > 0.0f) t.ref_dmg = ref_dmg;
    const bool changed = (t.ge_class != ge_class);
    t.last_hit_ms = NowMs();
    if (!changed) {
        // 同一 victim + 同一 GE：这一发很可能就是回放自己打出去的（KB 11.10 的自喂抑制）。
        // 只续命，不能刷新 t.hp，否则 ReplayTick 的 async 归因会永远等于 0。
        return;
    }
    t.ge_class = ge_class;
    t.hp = hp;
    t.blocked = 0;
    t.dmg_sum = 0.0;
    t.applied_sum = 0;
    if (t.paused != 0) { t.paused = 0; PushLog("replay: resumed"); }
    {
        char b[192];
        std::snprintf(b, sizeof(b), "replay: track %s asc=0x%llX hp=%.1f ge=0x%llX n=%d",
                      t.name, (unsigned long long)asc, hp,
                      (unsigned long long)ge_class, g_rt_n);
        PushLog(b);
    }
}

// UI-thread cadence: samples each target HP (damage lands asynchronously, so the
// delta seen here belongs to the previous turn), drops dead/stale targets, and
// hands one activation turn to the game thread.
void ReplayTick() {
    if (g_ui_state.damage == 0) {
        if (g_rt_n != 0) { g_rt_n = 0; PushLog("replay: cleared"); }
        g_shot_clock_ms = 0;
        g_apply_clock_ms = 0;
        g_apply_budget = 0.0;
        g_ge_shots = 0;
        return;
    }
    const std::uint64_t now = NowMs();
    // 先采样：HP 与发放解耦。掉血（哪怕来自玩家自己的攻击）就解除 pause，
    // 否则暂停后不再施加 → 永不掉血 → 永久 pause。
    for (int i = 0; i < g_rt_n; ) {
        ReplayTarget& t = g_rt[i];
        float hp = 0.0f;
        const bool ok = ReadAscHP(t.asc, &hp);
        if (!ok || hp <= 0.0f || !AscStillValid(t.asc)) {
            char b[128];
            std::snprintf(b, sizeof(b), "replay: drop %s (dead/lost/retired hp=%.1f)", t.name, hp);
            PushLog(b);
            ReplayDropTarget(i);
            continue;
        }
        if (now - t.last_hit_ms > 15000) { ReplayDropTarget(i); continue; }
        const float delta = t.hp - hp;
        if (delta > 0.5f) {
            ++t.landed;
            g_rep_dmg += static_cast<int>(delta);
            t.dmg_sum += delta;          // 估算“每次 apply 打出多少”
            t.applied_sum += t.applied;
            t.blocked = 0;
            if (t.paused != 0) { t.paused = 0; PushLog("replay: resumed (damage landed)"); }
        } else if (t.applied > 0) {
            ++t.blocked;
            if (t.blocked >= 4 && t.paused == 0) {
                t.paused = 1;
                char b[160];
                std::snprintf(b, sizeof(b),
                              "replay: pause %s (invincible/untargetable, retry every 500ms)",
                              t.name);
                PushLog(b);
            }
        }
        t.applied = 0;
        t.hp = hp;
        ++i;
    }
    // 间隔过短服务器不认：小于 30Hz 服务器 tick（约 33ms）的间隔没有意义，
    // 本帧余数直接丢弃，不跨帧累积补发；每帧最多施加一次。
    int rate = static_cast<int>(g_ui_state.replay);
    if (rate < 1) rate = 1;
    if (rate > 512) rate = 512;
    std::uint64_t interval = static_cast<std::uint64_t>(1000 / rate > 0 ? 1000 / rate : 1);
    constexpr std::uint64_t kServerTickMs = 33;   // 30Hz 服务器 tick
    if (interval < kServerTickMs) interval = kServerTickMs;
    if (g_apply_clock_ms == 0) { g_apply_clock_ms = now; return; }
    if (now - g_apply_clock_ms < interval) return;
    g_apply_clock_ms = now;          // 丢弃余数
    const int applies = 1;           // 单帧单发
    if (g_gec_ufn == 0 || g_gea_ufn == 0) ResolveGeHelpersUiThread();
    if (g_rt_n == 0) return;
    if (g_gec_ufn == 0 || g_gea_ufn == 0) return;
    static std::uint64_t last_report = 0;
    if (now - last_report >= 5000) {
        last_report = now;
        for (int i = 0; i < g_rt_n; ++i) {
            char b[192];
            std::snprintf(b, sizeof(b),
                          "replay: [%d] %s hp=%.1f shots=%d landed=%d dmg=%d fail=%d",
                          i, g_rt[i].name, g_rt[i].hp, g_rep_shots,
                          g_rt[i].landed, g_rep_dmg, g_rep_fail);
            PushLog(b);
        }
    }
    g_ge_shots = applies;
    g_ge_req = 1;
}

// Game-thread turn: one context, then one application per live target, so every
// target of this turn is hit on the same tick.
void ReplayApplyStep() {
    g_ge_req = 0;
    const int applies = g_ge_shots > 0 ? g_ge_shots : 0;
    g_ge_shots = 0;
    if (g_ui_state.damage == 0 || g_rt_n == 0 || applies <= 0) return;
    if (g_gec_ufn == 0 || g_gea_ufn == 0) return;
    const std::uintptr_t src = PlayerAscForApply();
    if (src < 0x10000) return;
    std::uintptr_t pe = static_cast<std::uintptr_t>(g_pe_addr);
    if (pe < 0x10000) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("HTGame.exe"));
        if (base != 0) pe = base + 0x3E41360;
    }
    if (pe < 0x10000) return;
    g_cap_suppress = 1;
    for (int k = 0; k < applies; ++k) {
        // 每一发都用新的 effect context：同一个 ctx 复用到多发表演上，语义没验证过。
        unsigned char ctx[0x18] = {0};
        reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(src),
                                   reinterpret_cast<void*>(g_gec_ufn), ctx);
        for (int i = 0; i < g_rt_n; ++i) {
            ReplayTarget& t = g_rt[i];
            if (t.paused != 0) continue;   // 暂停目标不计数也不施加，等采样判定恢复
            ++g_rep_shots;   // 计划施加一次（发送计数，与 HP 采样无关）
            if (t.asc < 0x10000 || t.ge_class < 0x10000 || !AscStillValid(t.asc)) {
                ++g_rep_fail;   // 目标无效：这次算失败
                continue;
            }
            unsigned char params[0x38] = {0};
            const float level = 1.0f;
            std::memcpy(params + 0x00, &t.ge_class, 8);
            std::memcpy(params + 0x08, &t.asc, 8);
            std::memcpy(params + 0x10, &level, 4);
            std::memcpy(params + 0x18, ctx, 0x18);
            reinterpret_cast<PE_FN>(pe)(reinterpret_cast<void*>(src),
                                       reinterpret_cast<void*>(g_gea_ufn), params);
            ++t.applied;
            ++g_rep_ok;     // 已发送：这次算成功重放
        }
    }
    g_cap_suppress = 0;
}

void ANOMALY_CALL on_draw(void* ctx, const AnomalyUiServiceV1* ui) {
    static bool boot_logged = false;
    if (!boot_logged) {
        boot_logged = true;
        char bb[128];
        std::snprintf(bb, sizeof(bb), "boot: build=%s %s", __DATE__, __TIME__);
        PushLog(bb);
    }
    if (ctx == nullptr || ui == nullptr || ui->begin_window == nullptr) return;
    ++g_draw_count;
    {
        const int cfg_damage = g_ui_state.damage;
        const int cfg_chain = g_ui_state.chain;
        const int cfg_replay = static_cast<int>(g_ui_state.replay);
        if (g_cfg_seen_damage < 0) {
            g_cfg_seen_damage = cfg_damage;
            g_cfg_seen_chain = cfg_chain;
            g_cfg_seen_replay = cfg_replay;
        } else if (cfg_damage != g_cfg_seen_damage || cfg_chain != g_cfg_seen_chain ||
                   cfg_replay != g_cfg_seen_replay) {
            g_cfg_save_due_ms = NowMs() + 400;
        }
    }
    if (g_apply_hook_enabled) InstallApplyHook();
    else ReleaseHooks();
    // Gate 闂?Apply 闂佸憡鍔曢幊姗€宕曠€靛憡瀚柛鎰典簼閺嗗繘鏌?x7682F55闂佹寧绋戦¨鈧紒杈ㄧ箖缁嬪寮捄銊︽緰濠电偞娼欓鍡樼閼恒儳鈻?true闂佹寧绋戦張顒€螞閵堝鍋戞い鎺戝€昏ぐ灞角庨崶锝呭⒉濞寸厧鎳橀弻宀冪疀閵壯冭€挎繛?Apply 闂佸憡鑹鹃悧鍡涙嚐閻斿摜鈻旈柛婵嗗缁€瀣槈閹绢垰浜鹃梺鍦焾閸婂潡骞?
    ProbeFunctionsService();
    DumpDamageChain512();
    DumpApplyCallees();
    TickReplayQueue();
    TickOnDamagedReplay();
    TickSendEventReplay();
    TickSgeNativeReplay();
    TickApplyGeReplay();
    TickTargetApplyReplay();
    TickReplayQueue();
    if (GmToggleOn()) ApplyGmPatch();
    else RemoveGmPatch();
    BuildGmCommandList();
    EnsureHotkeys();
    TickHotkeyCapture();
    ProbeHook();
    ProbeExtraServices();
    if (g_probe_on) ProbeScanStep();
    TickFeatures();
    if (g_cap_on != 0 && !g_pe_subscribed && (g_draw_count % 30) == 0) EnsureSubscription();
    if (g_cap_on == 0 && g_pe_subscribed != 0) ReleaseCaptureSubscription();
    CollectApplyUfns();
    ConsumePendingCapture();
    ConsumeEcsCapture();
    CensusTick();
    AutoTargetTick();
    TickMonsterScan();
    MonTrackTick();      // 兜底：伤害事件没解码出 victim 时，按候选怪 HP 下降补一次
    ReplayTick();
    if (g_ui_state.damage != 0 && (g_draw_count % 300) == 0) {
        char gb[160];
        std::snprintf(gb, sizeof(gb), "gate: cap=%d sub=%d found=%d list=%d rt=%d ufn=%d/%d",
                      g_cap_on, g_pe_subscribed, g_mon_found, g_mon_list_n, g_rt_n,
                      g_gec_ufn != 0 ? 1 : 0, g_gea_ufn != 0 ? 1 : 0);
        PushLog(gb);
    }
    TickApplyCaptureReplay();
    if (g_cap_on != 0 && (g_draw_count % 120) == 0) {
        char peel[128];
        int pn = 0;
        peel[0] = 0;
        for (int i = 0; i < kPeelMax && pn < static_cast<int>(sizeof(peel)) - 24; ++i) {
            if (g_peel_calls[i] == 0) continue;
            pn += std::snprintf(peel + pn, sizeof(peel) - static_cast<std::size_t>(pn),
                                "%s%d:%llu", pn > 0 ? "," : "", i, g_peel_calls[i]);
        }
        char pb[288];
        std::snprintf(pb, sizeof(pb),
                      "pe: total=%llu caphit=%d dup=%d th=%d calls=%llu bad=%d rp=%d hit=%d "
                      "ring=%llu peel=%s",
                      g_pe_total, g_cap_hits, g_cap_dup, g_thunk_state, g_thunk_calls,
                      g_thunk_bad, g_rp_left, g_census_hits, g_pe_ring_seq,
                      peel[0] ? peel : "-");
        PushLog(pb);
    }

    static int open = 1;
    AnomalyStringViewV1 title{kWindowTitle, std::strlen(kWindowTitle)};
    if (ui->begin_window(ui->user, title, &open, 0) != 0) {
        DrawTabs(ui);
    }
    ui->end_window(ui->user);
}

const AnomalyPluginDescriptorV1 kDescriptor = {
    sizeof(AnomalyPluginDescriptorV1), 1, 0,
    {kId, 20}, {kName, 13}, {kAuthor, 7}, {kVersion, 5},
    on_load, on_start, on_stop, on_unload, on_update, on_draw,
};


// ---------------------------------------------------------------------------
// 闂佺硶鏅炲銊ц姳?5.6.1 CppSDK dump 闂?UFunction 闂佸憡鐟ラ崐褰掑汲閻旂儤鏆滈柛鎰╁妿濠€浼存煥濞戞ɑ绠絋Game_parameters.hpp 闂備緡鍋呴崝鏇㈡偤瑜嶉埢鎾诲垂椤愩倕顥愰柣搴ｆ暩濞呫垻妲?
// ---------------------------------------------------------------------------
// FBufferData 闂備緡鍋呴崝鏇㈡偤瑜嶉埢鎾绘濞戞鎮奸梺?HTGame_structs.hpp闂佹寧绋掔粙鎰不閿濆绀堢€广儱鎳庡☉褏鎲搁悧鍫熷矮缂?0x58 闁诲孩绋掗〃澶嬩繆椤撱垹閿ゆ俊銈咁儑缁€澶愭倵娴ｅ啫顥嶉柛銇卞洤妫橀柦妯侯槸椤綁鏌?
// 閻庢鍠栭幖顐も偓?闁诲繒鍋涢崐褰掑汲?闂佸憡绋忛崝宥咃耿閿熺姴绀傞柕濠忛檮绗?0闂佹寧绋戦悺妤?闂佸湱鐟抽崱鈺傛杸缂備胶濮甸〃鍛村吹?0 婵炲顑曢崐鏇㈩敊婵犲洤违?
struct FBufData {
    unsigned char BuffName[0x08];            // 0x00 FName
    unsigned char DynamicGrantedTags[0x20];  // 0x08 FGameplayTagContainer
    int           nLevel;                    // 0x28
    float         fDuration;                 // 0x2C
    float         ModifyDuration;            // 0x30
    float         fPeriod;                   // 0x34
    int           nStackCount;               // 0x38
    float         fStrengthMult;             // 0x3C
    float         fStrengthAdd;              // 0x40
    unsigned char pad44[0x04];               // 0x44
    long long     nStartTime;                // 0x48
    unsigned char pad50[0x08];               // 0x50 Dumper-7 did not export these 8 bytes
};
static_assert(sizeof(FBufData) == 0x58, "FBufferData is 0x58 in the dump");
struct FActiveGEHandle { unsigned char raw[0x08]; };

struct Params_ApplyGEToTarget {                               // sizeof 0x78
    void*  GameplayEffect;        // 0x00 TSubclassOf<UGameplayEffect>
    void*  TargetAbilityCmp;      // 0x08 UHTAbilitySystemComponent*
    FBufData ModifyData;          // 0x10 FBufferData
    unsigned char BuffEventType;  // 0x68 EBuffEventType
    unsigned char bUsePredictionKey; // 0x69
    unsigned char pad0[2];        // 0x6A
    FActiveGEHandle ReturnValue;  // 0x6C
    unsigned char pad1[4];        // 0x74
};
static_assert(sizeof(Params_ApplyGEToTarget) == 0x78, "dump says 0x78");

struct Params_GetAbilitySystemComponent { void* ReturnValue; };  // 0x00

// UObject::ProcessEvent 婵炶揪绲界粔宕囪姳椤掑倵鍋撻悽娈挎敯闁?vtable 闂?0x4C 濠碘€冲级閺嬭崵妲愬▎鎾冲偍闁绘柨顨庡妤呮煙缂佹ê绗傛繛鍡愬灲瀹曘儳浠︾紒銏犲妼闂佹寧绋戦惌渚€鎯佹径鎰妞ゆ梹顑欏宀勬煙闊彃鍔氶柟?闁?.4闂?
using ProcessEventFn = PE_FN;

ProcessEventFn ReadProcessEvent(void* object) {
    if (object == nullptr) return nullptr;
    void* vtable = nullptr;
    __try { vtable = *reinterpret_cast<void**>(object); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if (vtable == nullptr) return nullptr;
    void* fn = nullptr;
    __try { fn = *reinterpret_cast<void**>(static_cast<unsigned char*>(vtable) + 0x4C); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return reinterpret_cast<ProcessEventFn>(fn);
}

// 闂佸憡顨嗗ú姗€顢曟總绋挎闁芥ê顦～?GE闂佹寧绋掓穱娲儍閻斿吋鍋ㄩ柕濠忛檮閻撴瑩鏌熺紒妯哄缂佸爼娼ч蹇涙嚑缂傚孩甯″鎼佸箛椤旂晫鏆?UFunction 婵?GE 缂?
bool ApplyGameplayEffect(void* asc, void* ufunction_apply, void* ge_class) {
    auto process = ReadProcessEvent(asc);
    if (process == nullptr || ufunction_apply == nullptr) return false;
    Params_ApplyGEToTarget params{};
    params.GameplayEffect = ge_class;
    params.TargetAbilityCmp = asc;
    params.BuffEventType = 0;         // 婵帗绋掗…鍫ヮ敇缂佹顩查悗锝傛櫆椤愮晫绱掗銉殭闁诲海鍏橀弫宥呯暆閸愵亝顔嶉梺娲绘娇閸斿秹寮婚悢鐓庣濠㈣埖鍔曠敮宕囩磽閸愭儳鏋︽い鈺婂亰閹?
    params.bUsePredictionKey = 0;
    process(asc, ufunction_apply, &params);
    return true;
}


// 婵炲濮伴崕鎵箔閸涙潙鐭楅柛灞剧⊕濞堝爼鎮介姘殭闁活亶鍓熼弻鍛村箛椤掑倹鎲诲┑鐐茬墦濞佳嗐亹閸ヮ剚鍤?5.6.1/CppSDK/SDK/HTGame_parameters.hpp
struct Params_GetRallyState { unsigned char ReturnValue; };            // 0x01
struct Params_BoolResult    { unsigned char ReturnValue; };            // IsGM / CanShowGMConsole
struct Params_AwardPoint {                                             // 0x08
    int           TeamIndex;    // 0x00
    unsigned char ScoreType;    // 0x04 EHTVolleyballScoreType
    unsigned char pad[3];       // 0x05
};

// 缂傚倷鑳堕崰宥囩博?UFunction 闁荤姴顑呴崯浼村极閵堝鏅慨姗嗗墻閸ょ娀鎮?+ UFunction + 闂佸憡鐟ラ崐褰掑汲閻旂厧閿ゆ俊銈咁儑缁€鍕偣?ProcessEvent slot 0x4C闂?
bool CallUF(void* object, void* ufunction, void* params) {
    auto process = ReadProcessEvent(object);
    if (process == nullptr || object == nullptr || ufunction == nullptr) return false;
    process(object, ufunction, params);
    return true;
}

// 闂佸湱顭堥ˇ闈浳涢崸妤佲挄闁哄啠鍋撳┑?闂佺懓鐏氶幐鐑芥嚈閹达附鐒介柨婵嗗椤忛亶鏌涢幋锝呭惞婵?GE"闂備焦褰冪粔鐢稿蓟婵犲洤妫橀柦妯侯槸椤綁鏌涢幒鎾寸凡闁诡喗鎸荤粙澶愬焵椤掍胶鈻旀い蹇撴缁愭鏌″鍛煑缂佹鍊挎俊?
// 閻庢鍠栭幖顐も偓?1.0 / 闁诲繒鍋涢崐褰掑汲?1 闁荤偞绋忛崝搴ㄥΦ濮橆兘鏌﹂悹鎭掑妽閺?GE 闂佺厧顨庢禍锝夋閳哄懏鍎嶉柛鏇ㄥ墯濞堝爼鏌涙繝鍕┾偓瀣濠靛鐐婇柣妯诲墯閸斿啯鎱ㄩ敐鍛妞ゆ劘鍋愮槐鎺楀箻閸撲焦姣堟繛鎴炴尭妤犲摜鏁锝嗗弿閻庯綆鍠栨禍婵嗏槈閹绢垰浜鹃梺鍛婂灱椤曆囧箖閹惧墎灏甸悹鍥皺閳ь剛鍏樺畷銉ョ暆閸曨偅顏￠梺?
static void TickTargetApplyReplay() {
    if (g_ta_remaining <= 0 || g_dmg_fn == nullptr) return;
    if (g_ta_ge_class < 0x10000 || g_ta_target_asc < 0x10000) return;
    if (NowMs() < g_ta_due) return;
    if (g_player_pawn_ref < 0x10000) { g_ta_remaining = 0; return; }
    std::uintptr_t source_asc = 0;
    if (!ue::Reader::SafePtr(g_player_pawn_ref + 0x940, &source_asc) || source_asc < 0x10000) {
        g_ta_remaining = 0;
        PushLog("ta: source asc lost, stop");
        return;
    }
    Params_ApplyGEToTarget params{};
    params.GameplayEffect = reinterpret_cast<void*>(g_ta_ge_class);
    params.TargetAbilityCmp = reinterpret_cast<void*>(g_ta_target_asc);
    params.ModifyData.nLevel = 1;
    params.ModifyData.fDuration = 5.0f;
    params.ModifyData.fPeriod = 0.0f;
    params.ModifyData.nStackCount = 1;
    params.ModifyData.fStrengthMult = 1.0f;  // 1.0 = 婵炴垶鎸告鍝ユ暜椤愶絾鍙忛悗锝庡枛娴滄繂鈽夐幘顖氫壕闂佸憡鍨奸褔骞冮幘璇叉瀬闁哄绨遍崑?
    params.ModifyData.fStrengthAdd = 0.0f;
    params.BuffEventType = 1;                // EBuffEventType::BUFF_EVENT_BEGIN
    params.bUsePredictionKey = 0;
    CallUF(reinterpret_cast<void*>(source_asc), g_dmg_fn, &params);
    --g_ta_remaining;
    ++g_ta_fired;
    g_ta_due = static_cast<std::uint32_t>(NowMs() + g_ta_interval);
    if (g_ta_remaining == 0) {
        char tb[96];
        std::snprintf(tb, sizeof(tb), "ta: done fired=%llu",
                      (unsigned long long)g_ta_fired);
        PushLog(tb);
    }
}


// GObjects 闁荤姳绀佸鈥澄涢崼鏇為棷闁靛ě鍐炬閻庣敻顣﹂懗鍫曟偉濠婂牊鏅慨姗嗗幗闂勫秹鏌ゆ總澶夌凹鐟滄澘鍟粋鎺旀崉閸濆嫮顔屽┑鐑囩秮閺€杈╃矚椤栨壕鍋撳☉娆樻畼妞ゆ垳鐒︾粙澶屾嫚瑜忕粈澶娾槈閹惧瓨鐓ユ繝鈧?HTGame_dump_20260929 婵炴垶鎼╅崢濂稿极椤旂晫鈻旈柍褜鍓熷畷銊╁箣閹烘洖骞€ 0x18CEEC9
constexpr const char* kGObjectsSig =
    "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8B 04 D1 C3";
constexpr const char* kGameModule = "HTGame.exe";
constexpr const char* kGameSection = ".text";
// 闂佺濮ら…鍫ュ吹濠婂嫭浜ら柟閭﹀灱閺€钘夆槈?Profile 闂?ue5.GWorld闂佹寧绋戝涔猻olve: rip-rel32, offset 3, size 7闂?
// Profile ue5.ProcessEvent闂佹寧绋戝涔猻olve: direct闂?
constexpr const char* kProcessEventSig =
    "40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 00 01 00 00 48 8D 6C 24 30 "
    "48 89 9D 28 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C5 48 89 85 C0 00 00 00";
constexpr const char* kGWorldSig = "48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 41 B0 01";

// g_ue defined earlier

bool InitUe(const void* signature_service, const void* names_service) {
    g_sig_svc = signature_service ? 1 : 0;
    if (!signature_service || !names_service) { g_sig_rc = -2; return false; }
    const auto* sig = static_cast<const AnomalySignatureServiceV1*>(signature_service);
    if (!sig->resolve) return false;
    std::uintptr_t address = 0;
    AnomalyStringViewV1 mod{kGameModule, std::strlen(kGameModule)};
    AnomalyStringViewV1 sec{kGameSection, std::strlen(kGameSection)};
    AnomalyStringViewV1 pat{kGObjectsSig, std::strlen(kGObjectsSig)};
    g_sig_rc = (int)sig->resolve(sig->user, mod, sec, pat, &address).code;
    if (g_sig_rc != ANOMALY_STATUS_V1_OK) return false;
    g_sig_addr = (unsigned long long)address;
    g_ue.names = static_cast<const AnomalyUe5NamesServiceV1*>(names_service);
    g_ue.get_object = reinterpret_cast<ue::GetObjFn>(address);
    // GObjects 的 items 数组指针：访问器首指令 mov rax,[rip+disp] 的 RIP 目标里存着它
    // （profile: rip-rel32, instructionSize 7, addend -16，即 items 在 FUObjectArray+16）。
    {
        __try {
            const int disp = *reinterpret_cast<const int*>(address + 3);
            const std::uintptr_t items_addr = address + 7 + (std::intptr_t)disp;
            std::uintptr_t items = 0;
            if (ue::Reader::SafePtr(items_addr, &items) && items != 0) {
                g_gobjects_items = items;
                char b[96];
                std::snprintf(b, sizeof(b), "ue: gobjects items=0x%llX",
                              (unsigned long long)g_gobjects_items);
                PushLog(b);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_gobjects_items = 0;
        }
    }
    {
        std::uintptr_t gaddr = 0;
        AnomalyStringViewV1 gpat{kGWorldSig, std::strlen(kGWorldSig)};
        if (sig->resolve(sig->user, mod, sec, gpat, &gaddr).code == ANOMALY_STATUS_V1_OK)
            g_gworld_instr = (unsigned long long)gaddr;
        std::uintptr_t peaddr = 0;
        AnomalyStringViewV1 pep{kProcessEventSig, std::strlen(kProcessEventSig)};
        if (sig->resolve(sig->user, mod, sec, pep, &peaddr).code == ANOMALY_STATUS_V1_OK)
            g_pe_addr = (unsigned long long)peaddr;
    }
    g_sig_ok = 1;
    g_names_ok = 1;
    return true;
}

}  // namespace

ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(
    AnomalyPluginDescriptorV1* descriptor) {
    if (!descriptor) return fail(ANOMALY_STATUS_V1_INVALID_ARGUMENT, "descriptor is invalid");
    if (descriptor->struct_size < sizeof(AnomalyPluginDescriptorV1)) {
        descriptor->struct_size = sizeof(AnomalyPluginDescriptorV1);
        descriptor->api_major = 1;
        descriptor->api_minor = 0;
        return fail(ANOMALY_STATUS_V1_BUFFER_TOO_SMALL, "descriptor is too small");
    }
    *descriptor = kDescriptor;
    return ok();
}

