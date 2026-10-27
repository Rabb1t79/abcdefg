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
// AnomalyUltimate 闁?濞撴碍绻冨畵渚€寮甸渚囧仹闂侇偄妫楅幃婊呮嫚娴ｇ懓绁﹂梺鎻掔Т缂傛捇鏁嶉崼婊呯憹婵炲矁娉曢弫銈夊籍瑜嶉悿鍕偝鐢喚绀?
// 閻犲洣鐒﹀畵渚€寮堕妷锔剧埍闁挎稒鐡╮e/闁哄牃鍋撶紓浣哥墦閻涙瑩寮ㄩ懜闈浶撻柛?md闁靛棔鑰縭e/闁搞儲绋戞慨娑㈡嚄閼恒儳鐖遍柣顔绘祰缁绘洟宕?md
//  - 闁硅绻楅崼顏嗙箔閿曗偓閻⊙呯箔閿旇儻顩柨娑欒壘鐢偅鎱?DLL 閺夆晜鍔橀、鎴﹀嫉閻旇崵娈堕柣?AnomalyPluginEntryV1 闁汇劌瀚换鎴﹀炊閻愭祴鍋撶涵椋庣RVA 0x495F0/0x49588/0x49608/0x49610闁?
//  - 闁哄牆绉存慨鐔煎蓟閵夘煈鍤勫銈呮惈缁參鏁嶅纾倀_trace 闁硅埖鎸搁崺宀勬儍?on_load 閹兼潙绻愰崹?
//  - 闁告梻鍠曢崗?id/闁绘鐗婂﹢浼存晬濮橆剙鏂у┑顔碱儐瀵寧娼婚幍顕呭剨濠靛鍋勯崢鏍晬閸︻湝A 0x22E30/0x29C50/0x32380/0x3C0A0闁?
//  - 闂佹澘绉堕悿?schema / 闂佹鍣槐?rdata 闁哄嫬瀛╅弸?+ 闁告绮拹鈺伹庨崱妤冩憻缂佹缂氱憰?
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

// 闂佹澘绉堕悿?schema闁挎稑鐗婂Σ鎴﹀棘閸ャ劍闄嶉柤?.rdata闁?x4C660 / 0x4D200 / 0x4D6D0 / 0x4F2C8闁?
constexpr const char* kChronosSchema =
    R"({"type":"object","additionalProperties":false,"required":["durationSeconds"],)"
    R"("properties":{"durationSeconds":{"type":"integer","minimum":1},)"
    R"("timedPauseHotkeyVirtualKey":{"type":"integer"},"timedPauseHotkeyModifiers":{"type":"integer"},)"
    R"("infinitePauseToggleHotkeyVirtualKey":{"type":"integer"},)"
    R"("infinitePauseToggleHotkeyModifiers":{"type":"integer"}}})";
constexpr const char* kDamageSchema =
    R"({"type":"object","additionalProperties":false,"required":["replayCount"],)"
    R"("properties":{"replayCount":{"type":"integer","minimum":0,"maximum":1024},)"
    R"("chainReaction":{"type":"boolean"}}})";
constexpr const char* kVolleyballSchema =
    R"({"type":"object","additionalProperties":false,"properties":)"
    R"({"hotkey":{"type":"string"},"anomaly-ultimate-tabs":{"type":"string"}}})";
constexpr const char* kGmSchema =
    R"({"type":"object","additionalProperties":false,"properties":)"
    R"({"hotkey":{"type":"string"}}})";

// 婵絽绻嬮柌婊堝礉閻旇鍘撮柛?on_load 濞戞搩鍘奸崢娑㈠冀閿熺姷宕ｅ〒姘箚缁傚棝鏁嶅畝鈧杈ㄥ緞鏉堛劍顦уù鐘劚閻㈩偊宕㈤悢閿嬬闁?UNAVAILABLE 闂傚嫬绉舵鍥晬閸粎鐟濋柣姘矋缁佸绱掗幋婵堟毎闁?
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
void ANOMALY_CALL OnProcessEvent(void*, uintptr_t, uintptr_t, void*);
static int g_pe_subscribed = 0;
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
static unsigned long long g_cls_pc = 0;
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
void TryInvokeApplyGE();   // Game 缂佹崘娉曢埢濂稿春閻旀椿鍤㈠ù绗哄€曢濠冪鐎ｂ晜顐介柨娑樼墛鐢即鏌﹂崼婵堢；闁哄啳顔愮槐?

void ChronosLoad(const AnomalyHostApiV1*, void** out) {
    if (!g_ctx || !g_ctx->signature) { *out = nullptr; return; }
    *out = g_ctx;  // 闁烩晩鍠楅悥锝囨喆閿濆棛鈧粙鏁嶉崷顧sedGameExcludePlayerByTimer闁挎稑顦靛〒鑸垫交閹邦垼鏀介柡鍐ㄥ椤掔兘宕ュ蹇曠閻?manifest
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
    // 闁告鍠庨～?on_load 闁汇劌瀚﹢鍥礉閳╁啰鍙€閻犲洢鍨介妴搴㈡償韫囥儳绀剆et_trace 閻庡湱鍋炵粊鎾晬?
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
    ReleaseHooks();
    if (ctx == g_ctx) g_ctx = nullptr;
    delete static_cast<Ctx*>(ctx);
}
void ANOMALY_CALL on_update(void*, double) {
    static int n = 0;
    TickCombatEvents();                 // 濞寸姴鎳庡﹢?Game 缂佹崘娉曢埢濂稿春閻旀椿鍤㈠ù绗哄€曢濠冪鐎ｂ晜顐介柨娑樼墛鐢即鏌﹂崼婵堢；闁哄啳顔愮槐?
    TickSweep();
    TickAscScan();
    TickReverseScan();
    TickWorldScan();
    TickRepeat();
    if ((++n % 30) == 0) TickFeatures();
}
// 缂佹劖顨呰ぐ娑欑▔鎼淬垹浠樺ù鐘插婵悂骞€娓氬﹦绀勯柛娆樺灡鐎垫梹绋婇崨顓烆嚙闁?anomaly.config闁?
struct UiState {
    int chronos = 0;
    int damage = 0;
    int chain = 0;
    int volley = 0;
    int gm = 0;
    float replay = 32.0f;
};
UiState g_ui_state;
int GmToggleOn() { return g_ui_state.gm; }
bool g_ui_dirty = false;

constexpr const char* kWindowTitle = "AnomalyUltimate##";
constexpr const char* kDamageSchemaId = "anomaly.ultimate.damage-replay.settings";

void SaveDamageConfig() {
    if (!g_ctx || !g_ctx->config) return;
    const auto* cfg = static_cast<const AnomalyConfigServiceV1*>(g_ctx->config);
    if (!cfg->write_atomic) return;
    char doc[192];
    int n = snprintf(doc, sizeof(doc), "{\"replayCount\":%d,\"chainReaction\":%s}",
                     static_cast<int>(g_ui_state.replay),
                     g_ui_state.chain ? "true" : "false");
    if (n <= 0) return;
    AnomalyStringViewV1 id{kDamageSchemaId, std::strlen(kDamageSchemaId)};
    AnomalyByteSpanV1 span{reinterpret_cast<const std::uint8_t*>(doc),
                           static_cast<std::size_t>(n)};
    cfg->write_atomic(cfg->user, id, 1, span);
    g_ui_dirty = false;
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

// 婵絽绻愰幎姘辨喆閿濆棛鈧粙宕跺☉妤呭殝闁告梻鍠曢崗姗€鎯冮崟顓熺獥闁?UClass/UFunction闁挎稑鐗忕划銊╁几濠婂啫鍐€闁哄嫮濮撮崺?UI 闁绘鍩栭埀顑挎祰椤㈡垿鏁嶆径鍫氬亾?
// 閻熸瑱绲鹃悗鑺ユ媴鐠恒劍鏆?GObjects 閻犱礁娼″Λ鍫曞闯椤帞绀勭紒娑欏劤閹洟鏁? anomaly.ue5.names 闁?name_id 婵絾鏌ㄩ顕€濡?
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


// 闁活潿鍔岄鏍ㄧ▔?objects 闁哄牆绉存慨鐔煎箰婢跺鍤犻悹鐏恒倗鐔呯€垫澘瀚ぐ?name_id闁挎稑鐗呯粭澶嬬瑹濠靛﹦顩?find_utf8闁?
unsigned int FindNameIdByPath(const char* path) {
    if (!g_ctx || !g_ctx->ue5_objects || !path) return 0;
    const auto* objs = static_cast<const AnomalyUe5ObjectsServiceV1*>(g_ctx->ue5_objects);
    if (!objs->find_exact || !objs->snapshot_by_handle) return 0;
    // BetterPose 闁告艾鏈娆撴晬濮橆剙甯ラ柡宥忕節閻涙瑧绱掗幘瀵糕偓顖涙媴閹惧灈鈧鈧湱鍋涚€垫﹢宕ラ銉х濞存粍绋戦悺褍鈻撶喊澶岀闂侇剙鐏濋崢銈夊籍瑜忔晶妤呭嫉瀹ュ懎顫ょ紓浣规尰閻庮垳鎼炬繝鍕珪闁?
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


// 闁?names 闁哄牆绉存慨鐔兼儍?resolve_utf8 闁?FName id 閻熸瑱绲鹃悗浠嬪箣閹邦厽鐎柡鍫墾缁辨繈鎮介妸銈囪壘闁哄秶顭堥顔剧磽閺嶎偆鍨?
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


// 闂侇剙绉村?UClass 闁?Children闁挎稑婀禨truct+0x48 / UField+0x28闁挎稑顧€缁辨繈鎮?resolve_utf8 閻熸瑱绲块悥婊堝触瀹ュ懐鎽熸慨锝嗘煥椤曨噣鏁?
// 濞戞挸绉崇欢椋庢導?find_utf8闁挎稑鐗愰姘跺箳閵夈儱缍撻柛锔哄妽濠€鎵偓鐟般仒鐎靛本绋夋繝浣虹憹闁告瑯鍨抽弫銈夋晬婢跺牃鍋?
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


// 闁?GObjects 濞戞搩鍘界€?Class 闁圭娲幏锟犲箥閹呮澖濞撴艾顑戠槐娆戞崉鐎圭姷绠?CDO "Default__*"闁?
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

// 鐎殿喗娲滈弫銈夋煣閹惧懐绀勯柛瀣箳浜涢柡澶堝劥閸?5.6.1/CppSDK/SDK/Engine_classes.hpp闁挎稑顧€缁?
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


// Chronos闁挎稒鐭粭姗€鎮剧仦鐐槯闂傚倹娼欓埀顒€绉跺濂告晬閸繀鐒婄紒澶岀帛濞肩敻鎳?5.6.1/CppSDK/SDK/Engine_classes.hpp闁?
//   UWorld::PersistentLevel 0x30 闁?ULevel::WorldSettings 0x2B0 闁?HTWorldSettings +0x5E0 (float)
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
    if (pe->subscribe(pe->user, OnProcessEvent, nullptr, &h).code == ANOMALY_STATUS_V1_OK)
        g_pe_subscribed = 1;
}

void TickChronos() {
    static int applied = 0;
    float* slot = WorldDilationSlot();
    if (slot == nullptr) return;
    if (g_ui_state.chronos && !applied) {
        float current = 1.0f;
        __try { current = *slot; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        if (current > 0.001f && current < 10.0f) {
            __try { *slot = 0.0001f; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
            applied = 1;
        }
    } else if (!g_ui_state.chronos && applied) {
        __try { *slot = 1.0f; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
        applied = 0;
    }
}


// GM闁挎稒纰嶆俊?IsGM 闁汇劌瀚敮顐︽偨?ExecFunction闁挎稑婀禙unction::ExecFunction 0xD8闁挎稑顦伴弫濂稿箣?"mov al,1; ret"
int g_gm_patched = 0;
AnomalyGenerationHandleV1 g_gm_handle{};

// on_load 闂傚啳鍩栭宀勫嫉瀹ュ懎顫ら柛娆樺灥閸忔浜稿顓熷紦婵炲鍔岄崬浠嬫晬瀹€鍐闂佹彃鏈€垫粎鏁閸庡ジ骞€瑜戣棢闁?
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


// GM 闁告稒鍨濋幎銈呫€掗崨顓炵闁挎稒顒猘tch 闁瑰瓨鍔曟慨娑㈠触鎼搭垳绀夊ù?HTCheatManager 缂侇偉宕靛▓?Children 闂佸墽鍋撻悘鍥ㄧ▔?UFunction 闁?
static char g_gm_cmd_names[32][64];
static int  g_gm_cmd_count = 0;
static int  g_gm_selected = -1;
static unsigned int g_gm_cls_id = 0;
static unsigned long long g_gm_cls_ptr = 0;
static unsigned long long g_gm_first_child = 0;
static unsigned long long g_gm_cheat = 0;

// 闁告帒妫楅幎姘跺箥椤愶絽浼庨柨娑欑鐎垫粎鐚剧拠鍙夊€抽悷娆欑悼閻栨粓宕犺ぐ鎺戝赋 "HTCheatManager"闁挎稑濂旂粭澶嬬瑹濠靛﹦顩ù鐘侯唺缂嶅秶绱掗幘瀵糕偓顖涙媴閹惧厖鐒婄紒?
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


// Chronos闁挎稒鑹鹃崣蹇曚沪閳ь剟寮崼鏇燂紵闁艰翰鍔忛崕?+ 闁绘壕鏅涢宥囨喆閹烘洖顥忛煫鍥╂櫕閺嗘劙宕楅妸銉ф拱闁艰翰鍔忛崕?
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
static void* g_pause_fn = nullptr;   // HTPlayerController::PausedGameExcludePlayerByTimer

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
    const std::uintptr_t dilation_addr = settings + 0x5E0;   // HTWorldSettings 闁煎浜滈悾鐐▕婢跺骸鏆欓柤鏂ュ亾闁挎稑婀礽meAccel 鐎瑰憡鐓￠悰娆戞嫚娓氬﹦绀?

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
        float frozen = 0.0001f;   // 闁哄啫鐖煎Λ鍧楀磻濠婂嫷鍓鹃柨娑欒壘瑜板洤銆掗崨濠傜亞闁煎浜ｉ棅鈺呮煢閸忓懐绉村☉鎾愁儔濡?
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


// 闁告瑦鐗楃粔鐑藉礋妤ｅ啠鍋撴径瀣槯闁逛勘鍊濋弨銏㈡偘閵夈倗顏查柨娑樼摜sGM 闁诡厹鍨归ˇ鏌ュ储閻旈鏉介柣?
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

// Damage闁挎稒鑹惧﹢?process-event 闁搞儳鍋犻惃鐔兼晬閸喓鍩楅柟鏉戠箳閸ゅ海绮欑€ｅ墎绀嗛梺鎻掔焸閸ｆ悂寮ㄩ幆褎鍊卞☉鎾亾閻犲鍟伴弫?
void* g_dmg_fn = nullptr;
int   g_replay_budget = 0;
static void PushLog(const char* text);
static void OnDamagedCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);
static void ApplyGeCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);
static void SendEventCapture(std::uintptr_t object, std::uintptr_t function, void* parameters);

void ANOMALY_CALL OnProcessEvent(void* /*user*/, uintptr_t object, uintptr_t function, void* parameters) {
    OnDamagedCapture(object, function, parameters);
    SendEventCapture(object, function, parameters);
    ApplyGeCapture(object, function, parameters);
    if (!g_ui_state.damage || function == 0 || function != reinterpret_cast<uintptr_t>(g_dmg_fn)) return;
    int count = static_cast<int>(g_ui_state.replay);
    if (count <= 0 || count > 1024) return;
    if (g_replay_budget > 200) return;              // 婵絽绻愰幎姘▔婵犲洦顎欓柨娑樼焸娴尖晠宕楀澶嬬┛鐎?
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
    // 閻庣懓顦崣蹇涙⒓閳ь剟鏁嶅鎵挎帡寮搁幇顒傛殮闁瑰瓨鍔掔粭鏍ь啅閸欏鐟忛柛鎺撴緲閻ゅ嫭绗熺€ｎ亝鍊电憸鎷岊嚙缁ㄦ娊宕戝鍕靛壘闁规鍋呭鍧楁晬瀹€鍕級闁稿繐绉甸惁锛勬暜瑜嶉崣蹇涙煂韫囨稐鎲鹃柛?
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
    if ((++tick % 30) != 0) return;   // 婵?30 閻㈩垎鍥ф閻犲洦娲戠粩鏉戔枎閳藉懐绀夐梺顒€鐏濋崢銈夊籍閳哄倹鍩傚鎯扮簿鐟欙箓宕ユ惔銏☆攳濞戞柨鎳忛弬浣割嚕?
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
}

static int g_gm_selected2 = -1;
static unsigned int g_replay_count = 0;


// 濞戞挸顦板顖濈疀椤愶絽绁归梺娆惧櫙缁辨┏imeAccel 闁告艾鏈娆愮閵堝嫮闉嶉柨娑橆檧缁变即寮捄杞扮不濞戞挴鍋撴繛?/ 濞戞挴鍋撻柣鈺佺摠濡炲倿宕?/ 闁诡厹鍨归ˇ鏌ユ焻閻斿嘲顔?
static const char* kHotkeyLabels[3] = {
    "\xE6\x97\xB6\xE5\x81\x9C\xE4\xB8\x80\xE6\xAC\xA1",              // 闁哄啳娉涙禒鐘崇▔閳ь剙鈻?
    "\xE4\xB8\x80\xE7\x9B\xB4\xE6\x97\xB6\xE5\x81\x9C",              // 濞戞挴鍋撻柣鈺佺摠濡炲倿宕?
    "\xE6\x81\xA2\xE5\xA4\x8D\xE9\x80\x9F\xE5\xBA\xA6",              // 闁诡厹鍨归ˇ鏌ユ焻閻斿嘲顔?
};

void ANOMALY_CALL OnHotkey(void* user, AnomalyGenerationHandleV1, const AnomalyInputSnapshotV1*) {
    const int index = static_cast<int>(reinterpret_cast<std::uintptr_t>(user));
    if (index == 3) {
        g_hot_restore = 1;
        if (g_pc_inst && g_pause_fn) {
            PE_FN process = nullptr;   // direct ProcessEvent crashes the game; disabled
            if (process) {
                struct { float seconds; } params{0.0f};
                process(g_pc_inst, g_pause_fn, &params);
            }
        }
        return;
    }
    if (index == 1) {
        g_hot_once = 1;
        if (g_pc_inst && g_pause_fn) {
            PE_FN process = nullptr;   // direct ProcessEvent crashes the game; disabled
            if (process) {
                struct { float seconds; } params{3600.0f};
                process(g_pc_inst, g_pause_fn, &params);
            }
        }
        return;
    }
    else if (index == 2) g_hot_hold = !g_hot_hold;
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
    }
    g_hot_registered = 1;
}



// ===== 闁稿﹤绉甸弫楣冩晬濞嗙殐ply 闂侇偄绻嬬槐鍫曞箰閸岀偞灏?+ 婵絽绻掗～?N 婵炲枴銈囩闂佸じ娴囬惃鐔告償?=====
using ApplyFn = void* (*)(void*, void*, void*, void*, void*, void*, void*, void*);
static ApplyFn g_apply_original = nullptr;
static AnomalyGenerationHandleV1 g_apply_handle{};
static int g_apply_state = 0;
static int g_apply_hook_enabled = 0;   // 濮掓稒顭堥濠氬礂閹惰姤锛旈柨娑欑煯缁?Tokky 闁?burst 闁圭鍊块幐顒勫礃閼碱剛宕?
static int g_apply_rc = -1;
static int g_apply_hits = 0;
// 闁告瑦鐗為惁澶屾媼閳╁啯娈堕柨娑樼墔缁楀宕ｉ崒娆戠憿闁告帇鍊曢悾楣冩晬婢舵稓绐桝pply 閺夆晜鏌ㄥú鏍磹閸忓吋笑闁告熬缂氱拹?0闁靛棔绀佸顒勫极閻楀牊笑闁告熬缂氱拹鐔虹矚?
static int g_ret_null = 0;
static int g_ret_nonnull = 0;
static int g_null_args = 0;
// 闂佹彃绉甸弬浣诡渶濡鍚囬柛蹇斿▕濡挳鏁嶅顒€娑х紓浣哄枙椤撴悂鏁嶇仦鑲╃憹閻犲鍟伴弫銈夋晬瀹€鍕級闁稿繐绉电憰鍡涘蓟閹捐泛娈犵紒瀣儔閸ｆ悂寮ㄩ幆褜鍤ら柤宄邦嚟濞堟垵鐣烽埡鍌滅毦
static int g_replay_enabled = 0;
// 闁告瑦鐗為惁澶愬箥椤愶絽浼庨柨娑欒壘椤?a1/a2/a3 闁?0x100 閻庢稒顨夋俊顓㈠箥?0xFFFFFFFFFFFFFFFF 閻?
static int g_neg1_calls = 0;      // 闁煎嘲鍟块惃顖涚▔閳ь剚绋夐鍕闁轰焦婢橀顔炬寬閿熺姴娅￠柛?-1 閻庢稒顨堝▓鎴犳嫬閸愵亝鏆忛柡?
static int g_clean_calls = 0;     // 濞戞挸顦柌婊堝矗閸屾稒娈堕悗鐢殿攰閽栧嫰鏌堥幋鎺旂憹闁?-1 閻庢稒顨堝▓鎴犳嫬閸愵亝鏆忛柡?
static int g_neg1_budget = 128;   // 闁告瑯浜濇竟鍌炲箵韫囨挸顤?128 婵炲棌妲勭槐婵嬫焼閸喖甯抽柟闀愯兌閻㈣顕ｉ埀顒勬煥閳?

// ===== Gate 閻熸瑥鍊圭粊?=====
// dc_targets512 闁告瑥绉甸惇鍦磽閺嶇數绀凴VA 0x7682F55~0x7682F5C闁挎稑顧€缁辩檰pply 閻?Gate 闁?test al,al; je ret闁?
// 闁圭鍋撳ù?Gate 闁?AL 閻忓繗椴稿Σ?闁哄牜鍓氶濂稿及椤栨碍鍎婇柣顏嗗枑椤掓粎绱掗幘鍓佹毈濞寸鍊曢?闁汇劌瀚崹浠嬪箲椤旇　鍋撻崒婵堢闂佹彃鑻ぐ褏鎷犻弰蹇ｅ敹鐟滅増娲╃槐婵囩▔瀹ュ棙鏆柛娆愵焾缁绘垿宕堕悙娴嬪亾缁楄　鍋?
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

// 闁告鐡曞ù鍥籍閸洖娅為柡鈧崣銉ㄢ拡濞?hook闁挎稑鐭傛导鈺呭礂瀹ュ洤鍔伴梺鎻掔Х濞村洭宕?detour 闁圭娲ら幃婊冾啅閹绘帒绁婚弶鐐村灊閸烆剟鎯?
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
// 闁烩晩鍠楅悥锝夊棘閽樺顫ｉ梺鎻掔У閺備線鏁嶉崼婵冨亾瀹ュ棙鏆伴柨娑橆檧缁辩増绂掓惔锝嗗焸閻?OnDamaged 闂佹彃鑻ぐ?闁瑰灚鎸烽懙鎴︽焽閿濆嫮顏遍柛鎴ｅ吹閺併倝鎯?GE 缂?闁挎稑濂旂粻锝夊触鎼淬垹鐦婚梻鍌涙尦濞堁囨偨?
// HTAbilitySystemComponent:K2_ApplyGameplayEffectToTargetWithClass 濞戞捁顕ф慨鈺呭棘閽樺顫ｉ柕?
// 濞戞挸瀛╁Λ锕傚嫉婢跺鐟忛柡澶涚節閸ｆ悂寮ㄩ崣銉х憹闁告艾鐭夌槐婵囨交濞嗘挸娅″☉鎾崇Ч閸ｆ悂寮ㄩ悙顒€绀嬮柤鎯у槻閸╁矂鎯冮崟顐ゆ憻闁煎搫鍊稿锟犳晬瀹€鍐ｅ亾鐏炵偓笑闂佹彃绉甸弻濠囧几閸曨垪鍋撻悩鎻掓闁轰焦澹嬮埀顒佹煛閳?
// 濞戞柨顑呮晶鐘冲緞鏉堫偉袝闁汇劌瀚悧鎾炊閻樿櫕笑 ModifyData 闁稿繈鍔戝ù鍌炴晬閸ф壌trengthMult/nStackCount 闂侇喛濮ゅΣ?0闁挎稑顧€缁辨繄绮垫径澶岃壘 0 闁稿﹤绉村杈ㄦ償閿旇　鍋?
static std::uintptr_t g_ta_ge_class = 0;     // TSubclassOf<UGameplayEffect>
static std::uintptr_t g_ta_target_asc = 0;   // 闁告瑦顨呴濠囨嚀?ASC
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
// 闂佹彃绉甸弬渚€骞嶈椤㈡垵顔忛幓鎺嶇不闁汇埄鐓夌槐鎵偓鍦仦缁?replay 濠⒀呭仱閺嗚鲸鎷呴崱妯煎煑闁规潙绻愰崬鏉戔柦閳╁啯绠掑Λ鐗堢箓椤︾粯瀵奸妶鍜佸敽闁挎稑濂旂粭鏍⒕韫囨挻鍊电€规洍鏅滅花婵嬪Υ?
// 濞ｅ洦绻勯弳鈧ù鐙呯悼閻栨粍绋夋惔銈庡悁闁轰礁搴滅槐婵囩▔瀹ュ懎鏅欓悹瀣暟閺併倖绂掔拋宕囩Э闁告鍠撻弫鎾诲礄閼恒儲娈堕柕?
static constexpr bool kReplayExec = false;


// ===== OnDamaged 闁硅娲濋獮?+ 闁圭顦板鍌炴⒒閹绢喖娅㈤柡鈧幘鍛闁圭鍋撻柡鍫濐槺鐢櫣鈧鍩栧闈涒攦閹邦亝绺洪悗鍦劜缁辨繃绋夊鍫㈢畺婵犲鍊楃悮顐﹀礆椤愵剛绀?====
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
    // OnDamaged 闁告瑥鍊归弳鐔烘暜閸愩劎婀伴柨娑樻箽TGame_parameters.hpp闁?x148闁挎稑顧€缁?
    //   0x000 DamageAmount | 0x008 FHitResult(0x108) | 0x110 DamageTags | 0x130 DamageGameplayEffect
    //   0x138 InstigatorCharacter | 0x140 DamageCauser
    // 闁?GE 閻庣數顢婇挅鍕儍閸曨叀顫﹂柨娑樻苟Object::Class @0x10闁挎稑顦紞鏃€绋?TSubclassOf闁挎稑鑻ぐ鍫⑩偓鐟扮枃閳?ASC = object+0x940闁?
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


// ===== 闁哄倽顫夐、宥嗙瀹€瀣獥闁硅娲濋獮?K2_ApplyGameEffectWithClassByActor 闁汇劌瀚﹢锛勨偓鍦仜瀵剟寮弶鑳珯闂佹彃绉甸弬?=====
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


// ===== 闁硅娲濋獮蹇涚嵁閼搁潧鐦婚柡鍐ㄧ埣閸ｆ悂寮?SendGameplayEventToActor闁挎稑鐗忓﹢锛勨偓鍦仒缁ㄣ劍绂掗崹娑樻櫔闁告鍠撻弫鎾村閵堝拋鍞虹紒鐙呯磿閸ゅ酣鏁?====
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

// 闁告瑯浜ｉ浼村箥椤愶絽浼庨柨娑欒壘瀵剟寮弶娆惧殸閻犵偐鈧啿顤?0x100 閻庢稒顨夋俊顓㈡煂鐏炵偓笑闁告熬绠戦悺銊╁捶?0xFFFFFFFFFFFFFFFF 閻庢稒顨愮槐娆忕暦閳哄倻鐨鹃柛锔芥緲濞煎啰绱掑▎蹇撴瘔闁汇劌瀚崵搴ｆ椤喚绀?
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

// ===== A1/A2 闁规亽鍨介幏锟犳晬閸儳甯涢悹浣靛€曢崣褔姊婚銊х=====
// dump: class UHTAbilitySystemComponent final : public UHTAttributeComponent
//       UHTAttributeComponent::HPCurrent = 0x1A38
static int g_probe_on = 0;
static std::uintptr_t g_asc[64] = {0};
static int g_asc_n = 0;
static int g_hp_drop = 0, g_hp_same = 0, g_hp_budget = 64;
static char g_asc_cls[32] = {0};
// 閻犱警鍨扮欢?闁挎稒顑揈 缂侇偆绮粩濠氬础閺囶亞绀勯柟鎭掑灲閹凤繝骞嶉锝呬紟闁哄啳鍩栭弫褰掓⒖閸☆厾绀夊〒姘⊕閻栧爼骞嬭箛鎾虫暥闂侇偅鍔栧顖炴焻婢跺顏ラ柨?
static char g_ge_names[256][48];
static std::uintptr_t g_ge_ptrs[256];
static int g_ge_count = 0;
static int g_ge_filter = 0;
static const char* const kGeFilters[5] = {"", "Nanally", "Lacrimosa", "Shinku", "Zankou"};
static char g_asc_names[64][48];
static int g_asc_sel = -1;
static int g_ge_sel = -1;
// A2 闁活亞鍠庨埀顒傘€嬬槐鎵偓鐟般仒鐎靛本瀵奸妶鍜佸敽濞存粌顑勫▎銏犆规笟濠勭Game 缂佹崘娉曢埢濂稿春閻曞倻绀?
static int g_combat_events = 0;
static std::uint64_t g_combat_seq = 0;
// 閺夆晝鍋ら弨锝夊矗椤忓嫭韬?闁活亞鍠庨悿鍕閵堝拋鍞哄ù婊冾儎濞?+ 闁哄牃鍋撻弶鈺傚灣缁旀潙鈻?Apply"闁哄啳娉涚紓鎾剁博?
static std::uint64_t g_apply_ms = 0;
static int g_armed_for_apply = 0;

void TickCombatEvents() {
    if (!g_ctx || !g_ctx->nte_combat) return;
    if (!g_probe_on && !(g_ui_state.damage && g_replay_enabled)) return;   // 濞戞挸绉堕弫銈夊籍閺堢數鐟濋弶鐑嗗枦椤?
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

// GObjects 閻犱礁娼″Λ鍫曞闯閵婏附寮撻柛瀣煯缁绘岸骞庨妶蹇曠獥閻℃帒锕﹂弲顐ｅ濮樻剚鍤㈤柛鎺撳閳?chunk 闁圭娲幏锟犳晬閸繄鈹涙繝褍鍟﹢鎾锤閳?0x0闁挎稑顧€缁辨繃娼诲▎鎾虫闁?SEH 闁告牕鎳撻敍?
static void* SafeObjectAt(unsigned int index) {
    __try {
        return g_ue.ObjectAt(index);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static int   g_hp_ok = 0;    // 闁瑰瓨鍔曟慨娑氭嫚鐠囨彃鐓?HPCurrent 闁汇劌瀚悿鍕瑹鐎ｎ偅娈?
static int   g_hp_nz = 0;    // 闁稿繑婀归懙鎴︽閻愮儤绁╅柣銊ュ閻ゅ嫭绗熺€ｎ偅娈?
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

// 濠⒀呭仱閸ｆ椽骞嶉锝呬紟 GObjects闁挎稑鐗婇惁锛勬暜?2 濞戞挸娴勭槐姘舵晬鐏炵偓鏆梻?UHTAbilitySystemComponent 閻庡湱鍋樼欢銉╂晬濞戞顏遍弶鐑嗗枟婢瑰倻鈧懓鏈晶鐘诲箵閹邦亝鍞夌紓浣规尰閻?
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

// 闁圭粯甯婂▎銏ゅ礃閸涱喗锛夐煫鍥殣缁辨瑩鎮欒ぐ鎺嗗亾?GE 闁哄啯鍎奸崵婊堝礉閵娿劍瀚归柛鏃傚С缁斿鎮板畝瀣
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
    // 闁告艾鏈鍌涙交閽樺顫ｉ柛鎺斿閻栧爼骞嬭箛鏇熺獥鐟?kx_log.txt闁挎稑鐗呯粚鑸电鎼达綆鐎茬紒鎹愭硶缁ㄨ法娑甸娆惧殺闁告瑦鐗槐?
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

// 濞戞挴鍋撻梺娆惧櫍娴滃爼宕㈤崱顓犵獥婵絽绻愰幎姘緞閸曨厽鍊炲☉鎾亾闁哄妲勭槐娆撳捶?on_update / Game 缂佹崘娉曢埢濂稿春閻旂厧娅￠悹鐑樺煀缁?
// 闁?UClass::Children(0x48) 闂侇剙绉村鑽ゆ喆閿濆棛鈧?UFunction闁挎稑鑻懟鐔兼偨?ExecFunction(0xD8) 闁哄秴娲ょ换鏃€鎷呭鍫㈢畺婵?
// 闁挎稑鐗呯欢鐑藉箲?Tokky-NTE 闂傚牊鐟﹂埀顑跨閸ㄥ酣寮搁幇顔界暠閻熸瑱绲鹃悗浠嬪棘閻熸壆纭€闁挎稒绋戦幃鏇犫偓娑欍仦鐠?dump 濞戞搩鍘奸崵顓㈡偝閹殿喗鐣?K2_ApplyGameEffectWithClassByActor闁?
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



// ===== ProcessEvent 閻熸瑥鍊搁惂鍌炴晬閸繂娑ч悹鍥︾串缁?====
// 閻犱降鍨藉Σ鍕偓鐟般仒鐎?anomaly.ue5.process-event闁挎稑鐭侀鍥亹閺囩偞鍊抽悗娑欘殔鐏忣噣鏌婂鍥ㄧ暠闁告垼濮ら弳鐔烘嫬閸愵亝鏆忛柨娑樼灱閺併倖绂嶆惔銈庢綆閻?Tokky 闁汇劌瀚﹢锛勨偓鍦仩閻ㄧ喖鎮介妸锝傚亾?
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


// R2闁挎稒姘ㄥú鍧楀箳閵夛箑顥?GObjects闁挎稑鏈€垫粎鍒掗崜褉鈧﹢宕ュ鍛憻闁?K2_ApplyGameEffectWithClassByActor
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


// ===== 闁哄倽妫勬慨鐐哄礄閼恒儲娈?hook闁挎稑鐗呯粭澶屽枈?ProcessEvent闁挎稑鑻ú婊冾潰閵堝嫮鐟濈憸鏉垮船閹?Tokky闁?====
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


// ===== 闁煎浜滅紓?ProcessEvent hook闁挎稑婢妕able+0x260闁挎稑濂旂粭澶嬬瑹濠靛﹦顩悗鐟般仒鐎靛瞼绮甸幆褎鍊抽柨?====
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
    // 闁告瑦鐗旂粩瀛樼▔椤忓懏绠掗柡浣哥墕椤曨喚鎸掗埥鍛閻?[obj] -> vtable -> +0x260
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


// ===== 濞寸鍊曢濠冪▔婵犲啫袚閻熸瑥鍊搁惂鍌炴晬濞嗙睒AbilityCharacter.BroadcastCharacterOnDamaged (RVA 0x7CB1710) =====
// 闁告瑯浜ｉ浼存焻韫囧海鐐婇柨娑樺缁楀鍠?Apply / ProcessEvent闁挎稑鑻ú婊冾潰閵堝嫮鐟濆☉?Tokky 闁告劘灏欓悰濠囧Υ?
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


// ===== 閻?Tokky 闁煎浜滅换渚€鎯?detour闁挎稒顑媝ply 缂佹梹鐟ч崑锝嗙▔婵犲嫭鐣?JMP 闁烩晩鍠楅悥锝夋晬閸繂娑ч悹鍥︾串缁辨繃绋夊鍫 hook闁?====
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

// ===== Tokky 闁告瑯浜ｉ浼村箳閵忋倖瀚?=====
// 闂侇偅淇虹换鍐偓鐟板暢閸ゆ粌顔忔潏鈺傜暠闁哄啨鍎辩换鏃傗偓娑欘殘椤戜焦绋夐幓鎺旀毎濞?闂傗偓濠婂啫鍓奸柛瀣箳浜?闁?婵☆垪鈧櫕鍋ラ柛瀣箳浜?闁汇劌瀚Ο濠囧磹绾绀?
// 闁告劕绉靛畵浣割潰閵堝牜鍤㈤柛娆愮墪閻ｇ姷绱撻幘宕囨憼闁?UFunction 闁圭娲幏锟犳晬閸儲娈旈柛?0x3CE78闁挎稑鏈鐢告嚊?0x77C8 濠?mov [rip+0x356a9]闁挎稑顦埀?
static std::uintptr_t g_tokky_base = 0;
static std::uintptr_t g_tokky_ufn = 0;
static int g_tokky_state = 0;

void ProbeTokkyDump() {
    // 1) 闁稿繐鐗婄€垫粌螣閳ヨ櫕鍋ラ柡灞诲劜婢?
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
        // 闁圭儤甯″▍搴ㄦ嚊椤忓洭鐓╂俊顖椻偓铏仴闁挎稑鐭傛导鈺呭礂瀹ュ懏鍤掑☉鎿冨幗閸ㄦ粍绂掗鍐ㄦ鐎规瓕浜▓鎴︽偋閻熸壆绐欏☉鎾村絻閻⊙囨閵忋倕娅?
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
                    // 闂傚洠鍋撻悷?閸?4KB 闁告劕鎳庨悺銊╁捶?Tokky 闁绘瑯鍓氬﹢渚€鎯?RTTI 闁告艾绋勭槐婵嬪箳閹烘鐝熼柛瀣喘濡插ジ骞€?
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
    // 闁革负鍔岄崙锛勨偓瑙勭煯缂嶅懘鎯?Tokky 闂傗偓濠婂啫鍓奸梺鎻掓湰鐎垫粍绂掗敐鍥╁灣闁绘鎳撶欢娑㈠箥閹呮殜闁汇劌瀚槐锔锯偓娑櫭崯鎾诲礂閵夘垳绐梞ov [rip+disp], rbx  (48 89 1D ...)
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
    g_probe_on = 1;                                  // 濞戞挴鍋撻梺娆惧櫍娴滃爼宕㈤崱姘闁告柣鍔岀槐鎴﹀触椤栨稑顥囬柟璇查獜缁辨繈寮悩缁樹粯闁告劕绉垫晶婊堝礉閵娿儱鐟庨柟鎭掑灲閹?
    if (!g_fn_ok || !g_ctx || !g_ctx->functions) { g_sweep_on = 0; PushLog("sweep abort: no service"); return; }
    if (g_ge_count == 0) return;                     // 缂佹稑顦甸々缁樻姜椤旇棄顥囬柟璇茬箰閸?GE 婵炴挸鎳庡畷?
    // 濞村吋锚閸樻盯骞?Tokky 闁汇劌瀚弻鐔奉嚕韫囥儳绀勭紒?Children + 闁告艾绉撮悺褔鏁嶆径骞帡寮?K2_ApplyGameEffectWithClassByActor闁挎稑鑻懟鐔烘偘閵夈劎绠婚柛濠冪懇閳?
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
    // UFunction 闁汇垼椴哥敮浼存煢閸喎顥囬柟璇茬箲鐎垫粎鈧數顢婇挅鍕触瀹ュ洦绾柟鎭掑劜婵嫰宕ｉ弽鐢靛耿婵縿鍊曢ˇ鈺傜▔瀹ュ懎鏅欓悹褎澹嗙悮?Children 闁哄被鍎叉竟?
    if (g_applyge_fn == 0) { g_invoke_rc = -4; return; }
    const auto* fn = static_cast<const AnomalyUe5FunctionsServiceV1*>(g_ctx->functions);
    if (!fn->invoke) { g_invoke_rc = -5; return; }
    // HTAbilitySystemComponent_K2_ApplyGameEffectWithClassByActor闁挎稑娼恥mp 0x0078闁挎稑顧€缁?
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
    if (cooldown > 0) { --cooldown; return; }   // 濞戞挴鍋撻弶鐑嗗枟婢瑰倻鈧懓鑻幃妤呭礃瀹勬澘绁遍柨娑樼焸娴尖晠宕楀鍡楃槷缂備緡鍘藉褔鏌呴悢绋款棁闁?
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
        // 闁哄啰濮靛顖涚閼搁潧绀嬮柤?K2_ApplyGameEffectWithClassByActor闁挎稑鐗呯粭澶嬬瑹濠靛﹦顩紒顐ヮ嚙閹?闁告挸绉剁槐鎴炴交閸ャ劍濮㈤柨?
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
            if (std::strncmp(oname, "Default__", 9) == 0) continue;   // 缂侇偄顭风划顖滄媼閵堝拋鍤犻悹鐐┾偓宕囨憻婵炲牓娼ч崣?0闁挎稑鐭侀悜锔芥交?
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
        cooldown = 300;   // 闁规鍋勯悾顒佺▔閳ь剚娼鍓ф惣 5 缂佸甯掗崯鈧柟?
    }
}

// 閻犱焦婢樼紞宥夊嫉閳ь剚娼婚幋婊咁伇婵炲枴銈囨闁活潿鍔屾晶鐘崇▔婢跺鍤嬮柛娆忓€归弳鐔衡偓鐢殿攰閽栧嫰鎯冮崟顓☆潶闁告艾绋勭槐娆撳矗閺嶎剛妲堥柣銏╃厜缁辨繃绋夊鍛濞戞挸楠搁崹鐣屸偓瑙勭啲缁?
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
        // 闁告瑯浜炲▍銉ф媼閺夊灝妫橀柡渚€顣︾粭宀勫籍鐠哄搫鐓濋柨娑欑⊕濡叉悂宕ラ敂鐐焸闁汇劌瀚紓鎾剁博鐎ｎ厾绠鹃梺澶哥筏缁辨繈鎮?TickCombatEvents 闁衡偓鐠哄搫鐓傞柣顏嗗枎閻ゅ嫭瀵奸妶鍜佸敽濞存粌顑勫▎銏ゅ触鎼粹€虫瀫閻?
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
    // 閻庨潧缍婄紞鍫ュ礆閹殿喗鍩傞悗鍦仜閸ら亶寮弶鍨汲闁告瑱缍囩槐妾杛ofile 闁绘鎳撶欢娑㈠矗椤栨繂鍘撮柦鈧挊澶嬭含闁告垼濮ら弳鐔煎礃閸涙潙鍔ラ柡浣规緲閻⊙囨嚍閸屾瑧顓洪柛姘嚱缁?
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
    // 閻庣數鍘ч崵?apply 闁告瑥锕ら崣楣冨触鎼达絿鏁惧璺哄閹﹪宕欓懞銉︽闁挎稑濂旂欢鐢电矉閼姐倕娈犻柛娆忕У閻湱绱撻弽鐢电闁告瑯浜ｉ浼存晬?
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
        {"\xE6\xBF\x80\xE6\xB4\xBB", 6},        // 婵犵鍋撴繛?
        {"\xE6\x97\xB6\xE5\x81\x9C", 6},        // 闁哄啳娉涙禒?
        {"\xE5\x80\x8D\xE6\x94\xBB", 6},        // 闁稿﹤绉甸弫?
        {"\xE6\x8E\x92\xE7\x90\x83", 6},        // 闁圭儤甯為幃?
        {"GM", 2},
        {"\xE5\x80\x99\xE9\x80\x89", 6},   // 闁稿﹥鐟╅埀?
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
                static const char* kVkNames[3] = {"F1", "F2", "F3"};
                for (int i = 0; i < 3; ++i) {
                    if (ui->text) {
                        char row[128];
                        int len = snprintf(row, sizeof(row), "%s  \xE5\xBF\xAB\xE6\x8D\xB7\xEF\xBC\x9A%s", kHotkeyLabels[i],
                                          g_hot_vk[i] == 0x70u ? kVkNames[0] : (g_hot_vk[i] == 0x71u ? kVkNames[1] : kVkNames[2]));
                        if (len > 0) { AnomalyStringViewV1 v{row, static_cast<std::size_t>(len)}; ui->text(ui->user, v); }
                    }
                    if (ui->same_line) ui->same_line(ui->user, 0.0f, -1.0f);
                    if (ui->button) {
                        char rb[24];
                        int rn = snprintf(rb, sizeof(rb), "\xE6\x94\xB9\xE9\x94\xAE##hk%d", i);
                        AnomalyStringViewV1 rebind{rb, static_cast<std::size_t>(rn > 0 ? rn : 0)};
                        if (ui->button(ui->user, rebind, 48.0f, 0.0f)) {
                            g_hot_vk[i] = (g_hot_vk[i] == 0x70u) ? 0x71u : (g_hot_vk[i] == 0x71u ? 0x72u : 0x70u);
                        }
                    }
                }
                if (ui->text) {
                    char buf[160];
                    int n = snprintf(buf, sizeof(buf), "wn=%s pz=%llX pe=%llX rc=%d prc=%d", g_world_name, reinterpret_cast<unsigned long long>(g_pause_fn), g_pe_addr, g_chronos_rc, g_pawn_rc);
                    if (n > 0) { AnomalyStringViewV1 v{buf, static_cast<std::size_t>(n)}; ui->text(ui->user, v); }
                }
            } else if (t == 2) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x80\x8D\xE6\x94\xBB\xE8\xBF\x9E\xE9\x94\x81\xE5\x8F\x8D\xE5\xBA\x94", 15};
                    ui->checkbox(ui->user, c1, &g_ui_state.chain);
                    AnomalyStringViewV1 c2{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c2, &g_ui_state.damage);
                    AnomalyStringViewV1 c3{"\xE9\x87\x8D\xE6\x94\xBE", 6};   // 闂佹彃绉甸弬渚€鏁嶉崼婵堟澖濡ょ姴鐭夌槐婵囶渶濡鍚囬柛蹇曨劜缁?
                    ui->checkbox(ui->user, c3, &g_replay_enabled);
                    AnomalyStringViewV1 c4{"\xE6\x8E\xA2\xE9\x92\x88", 6};   // 闁规亽鍨介幏锟犳晬閸儳甯涢悹浣靛€曢崣褔鏁?
                    ui->checkbox(ui->user, c4, &g_probe_on);
                }
                if (ui->begin_child && ui->end_child && ui->button) {
                    AnomalyStringViewV1 lid{"##gelist", 8};
                    if (true) {
for (int gi = 0; gi < g_ge_count; ++gi) {
                            if (g_ge_filter != 0 && std::strstr(g_ge_names[gi], kGeFilters[g_ge_filter]) == nullptr) continue;
                        AnomalyStringViewV1 nm{g_ge_names[gi], std::strlen(g_ge_names[gi])};
                            if (ui->button(ui->user, nm, 0.0f, 0.0f)) {
                                g_ge_sel = gi;
                                TryInvokeApplyGE();
                                char lb[96];
                                std::snprintf(lb, sizeof(lb), "ge=%d/sel=%d iv=%d rcv=%s %s",
                                              g_ge_count, gi, g_invoke_rc, g_recv_cls, g_ge_names[gi]);
                                PushLog(lb);
                            }
                        }                    }
                }
                if (ui->input_uint32) {
                    unsigned int value = static_cast<unsigned int>(g_ui_state.replay);
                    AnomalyStringViewV1 id{"##count", 7};
                    if (ui->input_uint32(ui->user, id, &value, 1, 10))
                        g_ui_state.replay = static_cast<float>(value);
                    if (ui->same_line && ui->text) {
                        ui->same_line(ui->user, 0.0f, -1.0f);
                        AnomalyStringViewV1 cap{"\xE4\xBC\xA4\xE5\xAE\xB3\xE9\x87\x8D\xE5\xA4\x8D\xE6\xAC\xA1\xE6\x95\xB0", 12};
                        ui->text(ui->user, cap);
                    }
                }
                if (ui->separator) ui->separator(ui->user);
                if (ui->text) {
                    char l1[96];
                    int n1 = snprintf(l1, sizeof(l1), "sub=%d dmg=%d ap=%d/rc%d hits=%d dmp=%d", g_pe_subscribed, g_dmg_chain_ok, g_apply_state, g_apply_rc, g_apply_hits, 0);
                    if (n1 > 0) { AnomalyStringViewV1 v{l1, static_cast<std::size_t>(n1)}; ui->text(ui->user, v); }
                    char l2[96];
                    int n2 = snprintf(l2, sizeof(l2), "replay=%u rn=%d rz=%d na=%d n1=%d n0=%d g=%d/%d dm=%d/%d ge=%d/sel=%d fn=%d iv=%d", g_replay_count, g_ret_nonnull, g_ret_null, g_null_args, g_neg1_calls, g_clean_calls, g_gate_true, g_gate_false, g_dmg_true, g_dmg_false, g_ge_count, g_ge_sel, g_fn_ok, g_invoke_rc);
                    if (n2 > 0) { AnomalyStringViewV1 v{l2, static_cast<std::size_t>(n2)}; ui->text(ui->user, v); }
                    char lp[96];
                    float hp_now = SumHP();
                    int np = snprintf(lp, sizeof(lp), "asc=%d hp=%.1f ok=%d nz=%d mx=%.0f hpd=%d hps=%d ev=%d", g_asc_n, hp_now, g_hp_ok, g_hp_nz, g_hp_max, g_hp_drop, g_hp_same, g_combat_events);
                    if (np > 0) { AnomalyStringViewV1 v{lp, static_cast<std::size_t>(np)}; ui->text(ui->user, v); }
                    if (ui->begin_child && ui->end_child && ui->text) {
                        AnomalyStringViewV1 lid{"##gelog", 7};
                        if (true) {
for (int li = g_log_n - 1; li >= 0 && li >= g_log_n - 12; --li) {
                                AnomalyStringViewV1 ln{g_log[li], std::strlen(g_log[li])};
                                ui->text(ui->user, ln);
                            }                        }
                    }
                    if (ui->begin_child && ui->end_child && ui->button) {
                        AnomalyStringViewV1 aid{"##asclist", 8};
                        if (true) {
for (int ai = 0; false && ai < g_asc_n; ++ai) {
                                char row[64];
                                std::snprintf(row, sizeof(row), "%d %s", ai, g_asc_names[ai]);
                                AnomalyStringViewV1 rv{row, std::strlen(row)};
                                if (ui->button(ui->user, rv, 0.0f, 0.0f)) {
                                    g_asc_sel = ai;
                                    char ab[96];
                                    std::snprintf(ab, sizeof(ab), "asc=%d/sel=%d %s",
                                                  g_asc_n, ai, g_asc_names[ai]);
                                    PushLog(ab);
                                }
                            }                        }
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 sw{"\xE4\xB8\x80\xE9\x94\xAE\xE9\x81\x8D\xE5\x8E\x86", 12};   // 濞戞挴鍋撻梺娆惧櫍娴滃爼宕?
                        if (ui->button(ui->user, sw, 0.0f, 0.0f)) {
                            g_sweep_on = 1;
                            g_sweep_i = 0;
                            g_sweep_fn = 0;
                            g_sweep_ge = 0;
                            PushLog("sweep start");
                        }
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 r1{"R1 children", 11};
                        if (ui->button(ui->user, r1, 0.0f, 0.0f)) { g_applyge_fn = 0; ResolveApplyUfn(); }
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 r2{"R2 scan", 7};
                        if (ui->button(ui->user, r2, 0.0f, 0.0f)) ScanExactUfn();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 tk{"Tokky dump", 10};
                        if (ui->button(ui->user, tk, 0.0f, 0.0f)) ProbeTokkyDump();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 ha{"Hook apply", 10};
                        if (ui->button(ui->user, ha, 0.0f, 0.0f)) HookLastApply();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 p2{"Hook PE2", 8};
                        if (ui->button(ui->user, p2, 0.0f, 0.0f)) HookProcessEventDirect();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 gf{"GE filter", 9};
                        if (ui->button(ui->user, gf, 0.0f, 0.0f)) {
                            g_ge_filter = (g_ge_filter + 1) % 5;
                            char fb[64];
                            std::snprintf(fb, sizeof(fb), "gefilter: %s",
                                          g_ge_filter == 0 ? "all" : kGeFilters[g_ge_filter]);
                            PushLog(fb);
                        }
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 as{"Scan ASC", 8};
                        if (ui->button(ui->user, as, 0.0f, 0.0f)) RequestAscScan();
                    }
if (ui->button) {
                        AnomalyStringViewV1 ws{"World scan", 10};
                        if (ui->button(ui->user, ws, 0.0f, 0.0f)) RequestWorldScan();
                    }
if (ui->button) {
                        AnomalyStringViewV1 ch{"Chain->Pawn", 12};
                        if (ui->button(ui->user, ch, 0.0f, 0.0f)) ChainToPawn();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 la{"Level->Actors", 13};
                        if (ui->button(ui->user, la, 0.0f, 0.0f)) LevelToActors();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 md{"ASC mode", 8};
                        if (ui->button(ui->user, md, 0.0f, 0.0f)) CycleAscMode();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 ag{"ApplyGE once", 12};
                        if (ui->button(ui->user, ag, 0.0f, 0.0f)) ApplyGEDirect();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 hd{"Hook DMG", 8};
                        if (ui->button(ui->user, hd, 0.0f, 0.0f)) HookDamageBroadcast();
                    }
if (ui->button) {
                        AnomalyStringViewV1 rl{"Release all", 11};
                        if (ui->button(ui->user, rl, 0.0f, 0.0f)) ReleaseEverything();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 uw{"PE unwatch", 10};
                        if (ui->button(ui->user, uw, 0.0f, 0.0f)) UnwatchProcessEvent();
                    }
                    if (ui->button) {
                        AnomalyStringViewV1 pw{"PE watch", 8};
                        if (ui->button(ui->user, pw, 0.0f, 0.0f)) StartProcessEventWatch();
                    }
                    if (ui->checkbox) {
                        AnomalyStringViewV1 ah{"ApplyHook", 9};
                        ui->checkbox(ui->user, ah, &g_apply_hook_enabled);
                    }
                    char lt[96];
                    int nt = snprintf(lt, sizeof(lt), "ta=%s", g_ta_cls);
                    if (nt > 0) { AnomalyStringViewV1 v{lt, static_cast<std::size_t>(nt)}; ui->text(ui->user, v); }
                    char l3[128];
                    int n3 = snprintf(l3, sizeof(l3), "cls1=%s cls2=%s cls3=%s", g_last_cls[0], g_last_cls[1], g_last_cls[2]);
                    if (n3 > 0) { AnomalyStringViewV1 v{l3, static_cast<std::size_t>(n3)}; ui->text(ui->user, v); }
                    char l4[96];
                    int n4 = snprintf(l4, sizeof(l4), "es=%d as=%d cs=%d", g_ctx && g_ctx->entities ? 1 : 0, g_ctx && g_ctx->actors ? 1 : 0, g_ctx && g_ctx->nte_combat ? 1 : 0);
                    if (n4 > 0) { AnomalyStringViewV1 v{l4, static_cast<std::size_t>(n4)}; ui->text(ui->user, v); }
                }
            } else if (t == 3) {
                if (ui->checkbox) {
                    AnomalyStringViewV1 c1{"\xE5\x90\xAF\xE7\x94\xA8", 6};
                    ui->checkbox(ui->user, c1, &g_ui_state.volley);
                }
                if (ui->text) {
                    AnomalyStringViewV1 v{"resolved=1", 10};
                    ui->text(ui->user, v);
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

// 閺夆晜鍔橀、鎴﹀籍閼搁潧璧存繛?entities/actors 闁哄牆绉存慨鐔煎及椤栨碍鍎婇悗娑櫭﹢顏堟晬閸繃鍎欓柛鏂诲妽濡晞绠涘Δ鈧ぐ褔寮伴娆戠懐闁伙絽鏈﹢顓犱焊鏉堚晛宕曢柡鍐ㄥ濞堟垼绠涢銈呭季闁?
void ProbeExtraServices() {
    static int frame = 0;
    if (!g_ctx) return;
    if (g_ctx->entities && g_ctx->actors && g_ctx->nte_combat) return;
    if ((++frame % 30) != 0) return;
    LazyService(&g_ctx->nte_combat, "anomaly.nte.combat", 1);   // Demo 闁圭粯甯婂▎銏＄▕閻斿憡笑婵絽绻愰幎姘跺蓟閵夘垳绀夊☉鎾崇Т瑜把囧捶?on_load
    LazyService(&g_ctx->entities, "anomaly.nte.entities", 1);
    LazyService(&g_ctx->actors, "anomaly.nte.actors", 1);
}


// ===== 閻犱警鍨扮欢?闁挎稒淇哄ù鍡涘磼?Apply 闁告劕鎳橀崕瀛樼▔閵堝嫰鍤嬮悶姘煎亯閻ㄧ喖宕欓懞銉︽闁?x775AD60 / 0x7780290闁?====
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

// 濞戞挴鍋撴繛鍡忓墲閳ь儸鍡樼ギ闁稿矈鐓夌槐鐧畃ply 缂佹梹鐟ч崑?/ apply 闁告鍠庨崵閬嶅极閹峰瞼鍎查柡?/ gate 闁?512 閻庢稒顨夋俊?
// 閻㈩垰鍟惇顒勬晬濮濇竵se(8)+apply(8)+apply_orig(8)+gate(8) + 512*3 = 1568 閻庢稒顨夋俊?
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


// 闂佹彃锕ラ弬渚€宕楅妸鈺佸姤闁圭鍊块幐顒佺▔鎼淬値鍚傞梻鍐ㄦ嫅缁辨瑩宕ラ銈呭姲闂佹彃绉峰ù鍥р枔鐎ｎ剚娈岄柨?
void ReleaseEverything() {
    ReleaseHooks();                              // Apply / Gate
    if (g_pe_state == 1) {                       // 閻庣懓銇樼€?ProcessEvent 閻犱降鍨藉Σ?
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


// 闁烩晛鐡ㄧ敮瀵告嫚鐠囨彃绲块柨娑樼墣缁绘绮欑€ｎ亜鏁堕柨娑橆檧缁辨繃绋夊鍥╃梾閺夆晛娲ら鏍ㄧ▔?read_memory 闁汇劌瀚€垫牠宕堕幘顔筋€欓柛?
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


// ===== 闁烩晛鐡ㄧ敮鎾棘閽樺顫ｅ☉鎾亾婵炲棌妲勭槐姗硂kky 闁哄牆鎼崺妤佸緞瀹ュ懎鐓濋柨?====
// 闁活亞鍠庨悿?ProcessEvent 闁稿繈鍎辫ぐ娑㈡晬濞嗙睒Game.exe + 0x3E41360闁挎稑娼恥mp 濡ょ姴鐭侀惁澶愭晬鐏炵偓寮撻悶?hook闁?
// 閻犲鍟伴弫銈夊礈瀹ュ洨鑸?UFunction 缂?0x400闁挎稑婧俇Function+0xB0]闁挎稑顧€缁辨繄鎷崘顏呮殢闁告艾姘︾换鏇㈠储閻旂补鍋?
using PeRealFn = void (*)(void*, void*, void*);
static int g_apply_direct_rc = -999;

static std::uintptr_t FindAbiCompClass() {
    unsigned int cid = FindNameIdByPath("/Script/HTGame.HTAbilitySystemComponent");
    void* cls = cid ? g_ue.FindClassById(cid) : nullptr;
    if (cls != nullptr) return reinterpret_cast<std::uintptr_t>(cls);
    // 闁稿繑绮岀花鎶芥晬濮橆厼顥囬柟璇茬箳鐞氼偊宕?
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


// ===== 闂佹彃绉撮ˇ鏌ュ棘閽樺顫ｉ悹瀣暙鐎规娊鏁嶉崼婊嗏拡婵縿鍎遍崢瑙勬償閺囶亞绐楁慨锝呯箲椤愯偐鎷崘顏呮殢闂侇喛濮ら悧搴㈩殽瀹€鍐闁搞儳鍎戠槐婵囧緞鏉堫偉袝闁告鍟挎禒鐘绘晬?====
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
    if (recv < 0x10000 || ufn < 0x10000 || ge == nullptr) return false;   // 缂佸苯鎼崹?
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
    if (ret != 1) {                       // 濞戞挶鍊栭鐐哄礂濠婂啰淇洪柨娑欎亢缁绘垿宕堕悙鎵憹闁瑰瓨鍔曟慨娑氫焊閸楃偘绮?
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


// ===== 濠㈣埖姘ㄦ鍥礂濠婂啰淇洪柨娑樼墣缁诲秶鎮扮仦鐐焸闁告瑯鍨伴崹蹇涘箲椤喚绀夐柡鍐█濞撳爼鏌屽鍡樼厐缂傚倹鐗為惁褔鏁?====
// mode 0 = 缂侇偉顕ч幃鏇㈠箥椤愶絽浼庨柨娑樼墢椤戝洦绋夐埀顒傜棯瑜濈槐姘舵晬濞岀de 1 = 閻熸瑦甯熸竟?+0x940 闁告瑥绉甸悡锟犳晬閸埄鍎戝ù婊冪灱妤犲洭鏁?
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


// ===== 闂佺偓宕橀惌楣冨矗閺嵮屽殸閻犵偐妲勭槐妾瀘rld -> level/pc/pawn闁挎稑鑻搴ㄥ箰?dump 闁稿绻掍簺闁?====
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


// ===== 濞戞挻鐗滈弲顐﹀礂濠婂啰淇洪柨娑欑婢瑰倿骞?ULevel闁挎稑鐭侀?OwningWorld(0xC0) =====
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

// ===== 閻犲洤锕ラ弻鍥晬濮濆锭unction thunk(+0xD8) 闁告劕鎳愬▓?call 闁烩晩鍠楅悥锝夋晬閸繂娑ч悹鍥︾串缁辨繃绋夊鍫 hook闁?====
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


// ===== 闁告瑯浜ｉ鎼佸极閻楀牆璧撮梺钘夌墳缁辨瑦绋夊澶婃闁衡偓娣囨墎鍋撴担椋庣憹闁衡偓閻熸澘妫橀柡浣稿簻缁?====
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


// ===== SendGameplayEventToActor 闁告鍠撻弫鎾垛偓鍦仧楠炲洭骞戦弴鈥崇闁挎稑婢卙unk闁愁偅澧? 閻庤鐭紞鍛存晬?====
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
    if (v < 0x10000000ULL) return false;      // 閻忓繐绻愬﹢鎾锤閳ь剟鏁嶅顓犲灲/濞戞挸鐡ㄥ?
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
        // 濞戞挴鍋撴繛鍡忓墲閳ь儸鍕荡闂佽棄鐗炵槐鐗堟交濞嗘挸娅＄€规瓕灏欑划锟犲箵閿涘嫭绲?actor 闁?UClass闁挎稑娼弆s闁挎稑顦幏?actor 闁圭娲幏锟犳晬瀹€鈧ú鍧楀箳閵夛妇鍙€ OnDamaged闁?
        // 濞戞挸绉烽摂?FindNameIdByPath / FindClassById闁炽儲鏌￠埀顒佹閸嬪懏绋夐妶鍡╁妱闁哄嫷鍨伴崣蹇曟偘閵娾晙鎲鹃柛妯烘４缁辨繃瀵煎顒佽含婵炴挸鎲￠崹娆戠棯鐠恒劉鏌ゅ☉鎾筹工瀹曞崬顫㈠Ч鍥ｅ亾?
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
                // 闁告劕鍟块悾楣冨箑瑜岀粩鏉戭潰閵夘垳绐楅柣?ProcessEvent 閻犲鍟╃粩鏉戔枎?OnDamaged 妤犵偞鍎奸浼村礈瀹ュ懏鍊?HP闁?
                // 闁告ê妫楄ぐ鍓佹媼閺夎法绉块梺?闁哄啰濮冲┑鈧悗?闁汇劌瀚伴崑鍛枎閳藉懐绀塳ReplayExec=false 閻犱讲鏅炵换鏍р枔閸忓摜鐭ら柡鍫簽濠€鈥愁潰閿濆棗鈷旈悶娑樼焿缁辨獖_ondmg_ufn 濞戞挴鍋撻柣鈺佺摠濡?0闁挎稑顦埀?
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


// ===== GA 婵犵鍋撴繛鎻掝煼閳ь剚宀告禍楣冩晬閸粎鐟?GE 闂侇偅宀告禍楣冪嵁鐠鸿櫣鎽犻柨?====
// UAbilitySystemComponent::TryActivateAbilityByClass(TSubclassOf<UGameplayAbility>)
// 闁告瑥鍊归弳鐔烘暜閸愩劎婀伴柟?dump闁挎稒顒?TSubclassOf<UGameplayAbility> AbilityToActivate; bool ReturnValue; }
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

    // 4) 闁告瑥鍊归弳鐔煎锤?0x78
    unsigned char params[0x78] = {0};
    // recv dump闁挎稒姘ㄧ悮顐﹀触?+ ClassPrivate + 0x2300..0x2400 闁告牕鎼悡娆撴晬閸繂娑ч悹鍥︾串缁辨繈鎮介妸銈囪壘閻庤纰嶉埀顑秶绀?
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

    // 5) 缂?0x400 -> 閻犲鍟伴弫?-> 閺夆晜锚鐢?
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

    // 閺夆晞妫勬慨鐐烘晬濮橆厽鏆柣?dump 闂佹彃鐬煎▓?K2_ApplyGameplayEffectToTargetWithClass 闁哄倽妫勬慨鐐哄触鐏炶偐顏卞☉?GE闁?
    // 濞戞挸锕浼存焽閿濆嫰鍤?...ByActor 闁诡厽甯熺换鎴﹀炊?1 闁告绻濈粭澶屾媼閳哄啯绐楅柡宥呮处鐢偓閻炴稈鍋撻柨娑欑缁绘牗绋夐鍕瘣闁轰焦婢橀悽?FBufferData ModifyData闁?
    // 闁兼澘鑻濠氬矗閹绘帇浜奸悹鎰╁劤濞堟垿寮介悷鐗堢婵繐绲惧Σ?ModifyData 闁稿繈鍔戝ù鍌炴晬閸ф壌trengthMult=0 / nStackCount=0 闁?0 濞寸鍊曢濠囨晬婢跺牃鍋?
    if (g_dmg_fn != nullptr) {
        void* ta_ge = ge;
        void* ta_target = reinterpret_cast<void*>(recv);
        unsigned char tp[0x78] = {0};
        std::memcpy(tp + 0x00, &ta_ge, 8);      // TSubclassOf<UGameplayEffect>
        std::memcpy(tp + 0x08, &ta_target, 8);  // UHTAbilitySystemComponent* 闁烩晩鍠楅悥?
        int   n_level = 1;
        float dur = 5.0f;
        float period = 0.0f;
        int   stack = 1;
        float mult = 1.0f;                      // 1.0 = 婵炲矁娉曢弫?GE 闁煎浜ｉ棅鈺呭极閺夊簱鍋?
        float add = 0.0f;
        std::memcpy(tp + 0x10 + 0x28, &n_level, 4);
        std::memcpy(tp + 0x10 + 0x2C, &dur, 4);
        std::memcpy(tp + 0x10 + 0x34, &period, 4);
        std::memcpy(tp + 0x10 + 0x38, &stack, 4);
        std::memcpy(tp + 0x10 + 0x3C, &mult, 4);
        std::memcpy(tp + 0x10 + 0x40, &add, 4);
        tp[0x69] = 0;   // bUsePredictionKey
        std::uintptr_t src_asc = g_player_asc >= 0x10000 ? g_player_asc : recv;
        // 闁稿繐鐗忛垾妯兼媼?ge 闁告帗婢樼花鎶藉及椤栨瑧鐭嗗☉鏂跨墳缁辩櫉SubclassOf 閻熸洑鑳跺▓鎴﹀及?UClass闁挎稑鐭佺€氥垺娼诲▎鎾虫闁?CDO/閻庡湱鍋樼欢銉╂晬?
        // 闁哄倽妫勬慨鐐剁疀閸涱垰濮ч柟宄般仒缁楀宕氶悧鍫熺畳闁轰礁鐗嗚ぐ鐐哄蓟閸曗斁鍋撻崒姘€遍柡鍐煐婵?dump 閻犱焦婢樼紞宥夋儍閸曨偄鏂ч柣?RVA 闁瑰灚鎸搁崵顓㈠级閵夈儰绮甸悗闈涙贡閸欏酣濡?
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
                std::memset(tp + 0x6C, 0, 8);        // 婵炴挸鎳忕敮鈧☉鎾筹梗缁旀潙鈻庨敍鍕暠閺夆晜鏌ㄥú鏍矗閵夛妇鍔?
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
    if (params[0x70] != 1) {                       // 濞戞挶鍊栭鐐哄礂濠婂啰淇洪柨娑欎亢缁绘垿宕堕悙鏉戠亣闁告梻鍠愭晶鐘电磼瑜忛悽?
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
    if (g_apply_hook_enabled) InstallApplyHook();
    else ReleaseHooks();
    // Gate 闁?Apply 闁告劕鎳橀崕瀵告嫬閸愵亝鏆忛柨?x7682F55闁挎稑顧€缁辨繃绋夐弬璺ㄦ澖婵炴潙顑嗘禍鑺ョ▔?true闁挎稑鏈Λ銈夋偑椤掑倻褰屽ǎ鍥ｅ墲娴煎懘鏌岃箛銉у耿濞?Apply 闁告艾鐗嗛懟鐔哥▔閸濆嫬绀嬪☉鎾亾闁圭鍊块幐?
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
    ProbeHook();
    ProbeExtraServices();
    if (g_probe_on) ProbeScanStep();
    TickChronosDump();
    TickFeatures();

    static int open = 1;
    AnomalyStringViewV1 title{kWindowTitle, std::strlen(kWindowTitle)};
    if (ui->begin_window(ui->user, title, &open, 0) != 0) {
        DrawTabs(ui);
        if (ui->text) {
            char status[128];
            int n = snprintf(status, sizeof(status), "resolved c=%d d=%d v=%d gm=%d",
                             g_resolve.chronos, g_resolve.damage, g_resolve.volley, g_resolve.gm);
            if (n > 0) { AnomalyStringViewV1 v{status, static_cast<std::size_t>(n)}; ui->text(ui->user, v); }
        }
    }
    ui->end_window(ui->user);
}

const AnomalyPluginDescriptorV1 kDescriptor = {
    sizeof(AnomalyPluginDescriptorV1), 1, 0,
    {kId, 20}, {kName, 13}, {kAuthor, 7}, {kVersion, 5},
    on_load, on_start, on_stop, on_unload, on_update, on_draw,
};


// ---------------------------------------------------------------------------
// 闁糕晞妗ㄧ花?5.6.1 CppSDK dump 闁?UFunction 闁告瑥鍊归弳鐔烘暜閸愩劎婀伴柨娑樻箽TGame_parameters.hpp 闂侇偅鍔曢悺褍鈻撻崹顐ゅ閻庣數娅㈢槐?
// ---------------------------------------------------------------------------
// FBufferData 闂侇偅鍔曢悺褍鈻撻棃娑樼悼闁?HTGame_structs.hpp闁挎稒绋愮粻锝夊礈瀹ュ懎娑х憸鐗堟尫缂?0x58 閻庢稒顨夋俊顓㈠锤濡ゅ绀夐悗浣冨閸ぱ囧棘閽樺顫ｉ柡?
// 鐎殿喖鎼€?閻忕偛鍊归弳?闁告稏鍔嶅﹢锟犲礂閵婏附笑 0闁挎稑鐡楨 闁圭瑳鍡╂斀缂佺姵顨呴崵?0 濞寸鍊曢濠囧Υ?
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

// UObject::ProcessEvent 濞达絽绉崇花顒傗偓鐢殿攰閽?vtable 闁?0x4C 婵″弶鏋荤槐娆撳储閻斿娼楅柟缁樺笂濞嗐垽宕ョ仦缁㈠劌闁挎稑鐭侀惁澶愬箲椤旀娼岄柟韬插劚閹?閹?.4闁?
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

// 闁告娲橀濂稿棘閽樺顫?GE闁挎稒淇洪惃鐔兼偨閵婏附鐓欓柟缁樺姃缁堕潧顔忛懠缂庢帡寮搁幇顔界暠 UFunction 濞?GE 缂?
bool ApplyGameplayEffect(void* asc, void* ufunction_apply, void* ge_class) {
    auto process = ReadProcessEvent(asc);
    if (process == nullptr || ufunction_apply == nullptr) return false;
    Params_ApplyGEToTarget params{};
    params.GameplayEffect = ge_class;
    params.TargetAbilityCmp = asc;
    params.BuffEventType = 0;         // 濮掓稒顭堥缁樼鐎ｂ晜顐界紒顐ヮ嚙閻庣兘鏁嶅畝鍐闁活潿鍔嶉弻鐔煎箰婢舵劕甯崇紓鍐惧枦椤╊偊鎯?
    params.bUsePredictionKey = 0;
    process(asc, ufunction_apply, &params);
    return true;
}


// 濞寸姰鍎扮粭鍛村矗閸屾稒娈堕悽顖氬暙閻剟鏌呴幇顒傛憻婵炲牓娼цぐ鍥嚊?5.6.1/CppSDK/SDK/HTGame_parameters.hpp
struct Params_GetRallyState { unsigned char ReturnValue; };            // 0x01
struct Params_BoolResult    { unsigned char ReturnValue; };            // IsGM / CanShowGMConsole
struct Params_AwardPoint {                                             // 0x08
    int           TeamIndex;    // 0x00
    unsigned char ScoreType;    // 0x04 EHTVolleyballScoreType
    unsigned char pad[3];       // 0x05
};

// 缂備胶鍠嶇粩?UFunction 閻犲鍟伴弫銈夋晬濮橆剦鍤犻悹?+ UFunction + 闁告瑥鍊归弳鐔煎锤濡ゅ绀勯悹?ProcessEvent slot 0x4C闁?
bool CallUF(void* object, void* ufunction, void* params) {
    auto process = ReadProcessEvent(object);
    if (process == nullptr || object == nullptr || ufunction == nullptr) return false;
    process(object, ufunction, params);
    return true;
}

// 闁圭顦靛Λ鍧楁⒕閺冣偓婵?闁瑰灚鎸烽懙鎴︽焽閿濆嫮顏遍柛鎴ｅ吹濞?GE"闂佹彃绉甸弻濠囧棘閽樺顫ｉ柛鎺撴緲閹挻绋夐埀顒佺▔椤忓棙绐楅柡宥呮矗缁楀倿濡?
// 鐎殿喖鎼€?1.0 / 閻忕偛鍊归弳?1 閻炴稏鍔庨妵姘柦鐠恒劍鏆?GE 闁煎浜ｉ棅鈺呮儍閸曨剚娈堕柛濠勩€嬬槐婵嬪炊閻樻剚鍔冩慨锝呯箲椤愯偐绱掗幘鍓佹毈濞戞挸楠哥敮顐ｆ叏鐎ｎ喖浜濆☉鎾亾闁告垼顕ч幃鎾剁尵鐠囪尙鈧兘宕ュ畝鍕闁?
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
    params.ModifyData.fStrengthMult = 1.0f;  // 1.0 = 濞戞挸楠哥敮顐ｆ叏鐎ｎ喖浜濆☉鎾亾闁告垼顕ч幃鎾诲极閺夊簱鍋?
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


// GObjects 閻犱礁娼″Λ鍫曞闯閵娧冾棗鐎甸鑳堕悥婊堟晬濮橆厽闄嶉柤濂変簼瑜板啯绂掔捄鍝勭婵烇綁鏀辩粚顖溾偓娑欘殘椤戜焦绋夌拠褏绀夊☉鎾存煥濠€?HTGame_dump_20260929 濞戞搩鍘奸弫顔界▔閳ь剟宕ㄩ幋鎺曞幀 0x18CEEC9
constexpr const char* kGObjectsSig =
    "48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8B 04 D1 C3";
constexpr const char* kGameModule = "HTGame.exe";
constexpr const char* kGameSection = ".text";
// 闁硅姤顭堥崵婊勬交閹邦垼鏀藉☉?Profile 闁?ue5.GWorld闁挎稑婢乪solve: rip-rel32, offset 3, size 7闁?
// Profile ue5.ProcessEvent闁挎稑婢乪solve: direct闁?
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

