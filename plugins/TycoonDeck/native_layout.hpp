#pragma once

// Compatibility data for one game build family: the engine-level UObject layout and
// the object-registry signature. Everything that belongs to Tycoon's Deck itself is
// read from reflection at run time instead of being stored here, so a game update can
// only change the class shape, never silently reuse a stale game offset.

#include <anomaly/sdk/profile.hpp>

#include <cstdint>
#include <string_view>

namespace tycoon::profile {

struct Layout final {
    std::uint32_t ObjectClass{};
    std::uint32_t ObjectName{};
    std::uint32_t ObjectIndex{};
    std::uint32_t StructSuper{};
    std::uint32_t StructProperties{};
    std::uint32_t PropertyName{};
    std::uint32_t PropertyOffset{};
    std::uint32_t PropertySize{};
    std::uint32_t PropertyNext{};
    std::uint32_t FunctionCount{};
    std::uint32_t FunctionSize{};
    std::uint32_t RegistryCount{};
    std::uint32_t RegistryItems{};
    std::uint32_t ChunkSize{};
    std::uint32_t ItemStride{};
    std::uint32_t ItemSerial{};
    std::uint32_t RegistrySubtract{};
};

inline Layout native_layout;
inline anomaly::sdk::ProfileSnapshot compatibility;

// A layout key that is absent must not fall back to a guessed offset, so absence is
// reported as zero and LoadCompatibility rejects the whole document.
[[nodiscard]] inline std::uint32_t Number(const std::string_view key) {
    const auto& values = compatibility.Document().layout;
    const auto found = values.find(key);
    return found == values.end() ? 0U : found->second;
}

[[nodiscard]] inline bool LoadCompatibility(const AnomalyHostApiV1* host) noexcept {
    try {
        if (!compatibility.Load(host, "compatibility.profile.json",
                {"nte", "HTGame.exe", ".text", "tycoons-deck-v1"}, {"Objects"})) {
            return false;
        }
        Layout parsed;
        parsed.ObjectClass = Number("ObjectClass");
        parsed.ObjectName = Number("ObjectName");
        parsed.ObjectIndex = Number("ObjectIndex");
        parsed.StructSuper = Number("StructSuper");
        parsed.StructProperties = Number("StructProperties");
        parsed.PropertyName = Number("PropertyName");
        parsed.PropertyOffset = Number("PropertyOffset");
        parsed.PropertySize = Number("PropertySize");
        parsed.PropertyNext = Number("PropertyNext");
        parsed.FunctionCount = Number("FunctionCount");
        parsed.FunctionSize = Number("FunctionSize");
        parsed.RegistryCount = Number("RegistryCount");
        parsed.RegistryItems = Number("RegistryItems");
        parsed.ChunkSize = Number("ChunkSize");
        parsed.ItemStride = Number("ItemStride");
        parsed.ItemSerial = Number("ItemSerial");
        parsed.RegistrySubtract = Number("RegistrySubtract");
        // Every value is a real byte offset, so zero can only mean "key missing".
        if (!parsed.ObjectClass || !parsed.ObjectName || !parsed.ObjectIndex || !parsed.StructSuper ||
            !parsed.StructProperties || !parsed.PropertyName || !parsed.PropertyOffset ||
            !parsed.PropertySize || !parsed.PropertyNext || !parsed.FunctionCount ||
            !parsed.FunctionSize || !parsed.RegistryCount || !parsed.RegistryItems ||
            !parsed.ChunkSize || !parsed.ItemStride || !parsed.ItemSerial) {
            return false;
        }
        // The engine layout is only usable when the relationships between the fields
        // hold: a property link follows its own header, and a UFunction's marshal size
        // follows its parameter count.
        if (parsed.ItemStride < sizeof(void*) ||
            parsed.ItemSerial + sizeof(std::uint32_t) > parsed.ItemStride ||
            parsed.PropertyNext <= parsed.PropertyOffset ||
            parsed.PropertySize + sizeof(std::int32_t) > parsed.PropertyOffset ||
            parsed.FunctionSize + sizeof(std::uint16_t) > parsed.StructSuper ||
            parsed.ChunkSize % 2U != 0) {
            return false;
        }
        native_layout = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace tycoon::profile
