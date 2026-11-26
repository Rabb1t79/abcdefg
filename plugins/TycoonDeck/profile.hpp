#pragma once
#include <anomaly/sdk/profile.hpp>
#include <array>
#include <cstdint>

namespace tycoon::profile {
struct Layout {
    std::uint32_t ObjectFlags{}, ObjectIndex{}, ObjectClass{}, ObjectName{}, ObjectOuter{}, Super{};
    std::uint32_t PropertyLink{}, FieldName{}, PropertySize{}, PropertyOffset{}, PropertyNext{};
    std::uint32_t FunctionFlags{}, FunctionCount{}, FunctionSize{};
    std::uint32_t RegistryCount{}, RegistryItems{}, ItemStride{}, Serial{}, ChunkSize{};
    std::uint32_t ProcessEventSlot{}, RegistryLoad{}, RegistrySubtract{};
    std::uint32_t FieldClass{}, FieldClassName{};
    std::uint32_t MapEntryStride{}, MapValue{}, MapInlineBits{}, MapBitsData{}, MapBitCount{}, MapBitCapacity{};
};

struct Snapshot {
    Layout layout;
    anomaly::sdk::ProfileSnapshot compatibility;
    std::uintptr_t registry{}, world{};
};
inline Snapshot snapshot;

// 只解码受支持的 x64 `MOV r64,[RIP+rel32]`，命令式指令位置来自 Profile。
inline std::uintptr_t RipTarget(const AnomalyCoreServiceV1* core,
        std::uintptr_t instruction, std::uint8_t modrm, std::uint32_t subtract) {
    std::array<std::uint8_t, 7> bytes{};
    if (instruction < 0x10000 || instruction > 0x00007FFFFFFFFFF8ull ||
        !anomaly::sdk::Succeeded(core->read_memory(core->user, instruction, {bytes.data(), bytes.size()})) ||
        bytes[0] != 0x48 || bytes[1] != 0x8B || bytes[2] != modrm) return 0;
    std::int32_t displacement{};
    std::memcpy(&displacement, bytes.data() + 3, sizeof(displacement));
    const auto value = static_cast<std::int64_t>(instruction) +
        static_cast<std::int64_t>(bytes.size()) + displacement - subtract;
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFF8ll && value % alignof(std::uintptr_t) == 0
        ? static_cast<std::uintptr_t>(value) : 0;
}

inline bool Load(const AnomalyHostApiV1* api) noexcept {
    snapshot = {};
    try {
        const anomaly::sdk::Host host(api);
        Snapshot parsed;
        if (!parsed.compatibility.Load(api, "compatibility.profile.json",
                {"nte", "HTGame.exe", ".text", "tycoondeck-v1"},
                {"ProcessEvent.entry"}, true)) return false;
        const auto& doc = parsed.compatibility.Document();
        const auto value = [&](std::string_view key) { return doc.Value(key, 65536); };
        parsed.layout.ObjectFlags = value("ObjectFlags");
        parsed.layout.ObjectIndex = value("ObjectIndex");
        parsed.layout.ObjectClass = value("ObjectClass");
        parsed.layout.ObjectName = value("ObjectName");
        parsed.layout.ObjectOuter = value("ObjectOuter");
        parsed.layout.Super = value("Super");
        parsed.layout.PropertyLink = value("PropertyLink");
        parsed.layout.FieldName = value("FieldName");
        parsed.layout.PropertySize = value("PropertySize");
        parsed.layout.PropertyOffset = value("PropertyOffset");
        parsed.layout.PropertyNext = value("PropertyNext");
        parsed.layout.FunctionFlags = value("FunctionFlags");
        parsed.layout.FunctionCount = value("FunctionCount");
        parsed.layout.FunctionSize = value("FunctionSize");
        parsed.layout.RegistryCount = value("RegistryCount");
        parsed.layout.RegistryItems = value("RegistryItems");
        parsed.layout.ItemStride = value("ItemStride");
        parsed.layout.Serial = value("Serial");
        parsed.layout.ChunkSize = value("ChunkSize");
        parsed.layout.ProcessEventSlot = value("ProcessEventSlot");
        parsed.layout.RegistryLoad = value("RegistryLoad");
        parsed.layout.RegistrySubtract = value("RegistrySubtract");
        parsed.layout.FieldClass = value("FieldClass");
        parsed.layout.FieldClassName = value("FieldClassName");
        parsed.layout.MapEntryStride = value("MapEntryStride");
        parsed.layout.MapValue = value("MapValue");
        parsed.layout.MapInlineBits = value("MapInlineBits");
        parsed.layout.MapBitsData = value("MapBitsData");
        parsed.layout.MapBitCount = value("MapBitCount");
        parsed.layout.MapBitCapacity = value("MapBitCapacity");
        const auto& v = parsed.layout;
        if (!v.ChunkSize || v.ItemStride < sizeof(std::uintptr_t) ||
            v.Serial + sizeof(std::uint32_t) > v.ItemStride ||
            !v.ProcessEventSlot || v.ProcessEventSlot % alignof(std::uintptr_t) ||
            v.FunctionCount + sizeof(std::uint8_t) > v.FunctionSize) return false;
        if (doc.symbol("ProcessEvent.entry").candidates.empty()) return false;
        const auto core = host.Query<AnomalyCoreServiceV1>(ANOMALY_CORE_SERVICE_V1_ID);
        if (!core || core->struct_size < offsetof(AnomalyCoreServiceV1, read_memory) + sizeof(core->read_memory) ||
            !core->read_memory) return false;
        const auto objects = parsed.compatibility.Address("Objects");
        const auto world = parsed.compatibility.Address("World");
        if (!objects || !world) return false;
        parsed.registry = RipTarget(core.get(), objects + v.RegistryLoad, 0x05, v.RegistrySubtract);
        parsed.world = RipTarget(core.get(), world, 0x1D, 0);
        if (!parsed.registry || !parsed.world) return false;
        snapshot = std::move(parsed);
        return true;
    } catch (...) { return false; }
}

inline bool ProcessEventEntry(const AnomalyCoreServiceV1* core, std::uintptr_t address) noexcept {
    return snapshot.compatibility.MatchesEntry(core, "ProcessEvent.entry", address, true);
}
}
