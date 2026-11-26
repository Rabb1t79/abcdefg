#pragma once

// Access to the live UObject graph: name resolution, reflection lookups and the
// object-registry translation between `anomaly.ue5.objects` handles and raw UObject
// addresses. The single write path here is the score assist in deck_runtime.hpp; every
// other operation only reads.

#include "native_layout.hpp"

#include <anomaly/sdk/cpp.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace tycoon {

using Clock = std::chrono::steady_clock;

struct FName final {
    std::uint32_t id{};
    std::uint32_t number{};
};
static_assert(sizeof(FName) == 8);

// Parameter frames of the functions this plugin calls. They are only ever borrowed for
// the duration of one synchronous ProcessEvent call.
struct Guid final {
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint32_t third{};
    std::uint32_t fourth{};
};
static_assert(sizeof(Guid) == 16);

class Reader final {
public:
    Reader(const AnomalyUe5NamesServiceV1* names, const AnomalyCoreServiceV1* core) noexcept
        : names_(names), core_(core) {}

    template <class Value>
    [[nodiscard]] bool Read(const std::uintptr_t address, Value& value) const noexcept {
        return address > 0x10000U && core_ != nullptr && core_->read_memory != nullptr &&
            core_->read_memory(core_->user, address,
                {reinterpret_cast<std::uint8_t*>(&value), sizeof(value)}).code == ANOMALY_STATUS_V1_OK;
    }

    [[nodiscard]] std::string Name(const FName value) const {
        if (value.id == 0 || names_ == nullptr || names_->resolve_utf8 == nullptr) return {};
        std::size_t size{};
        if (names_->resolve_utf8(names_->user, value.id, nullptr, &size).code != ANOMALY_STATUS_V1_OK ||
            size < 2 || size > 512) {
            return {};
        }
        std::string text(size, '\0');
        if (names_->resolve_utf8(names_->user, value.id, text.data(), &size).code != ANOMALY_STATUS_V1_OK ||
            size > text.size()) {
            return {};
        }
        text.resize(size);
        if (const auto terminator = text.find('\0'); terminator != std::string::npos) text.resize(terminator);
        if (value.number != 0) {
            text += '_';
            text += std::to_string(value.number - 1);
        }
        return text;
    }

    [[nodiscard]] std::string NameAt(const std::uintptr_t address) const {
        FName name{};
        return Read(address, name) ? Name(name) : std::string{};
    }

    [[nodiscard]] bool IsA(const std::uintptr_t object, const std::string_view type) const {
        std::uintptr_t type_object{};
        if (!Read(object + profile::native_layout.ObjectClass, type_object)) return false;
        for (int depth = 0; type_object != 0 && depth < 64; ++depth) {
            if (NameAt(type_object + profile::native_layout.ObjectName) == type) return true;
            std::uintptr_t parent{};
            if (!Read(type_object + profile::native_layout.StructSuper, parent) || parent == type_object) break;
            type_object = parent;
        }
        return false;
    }

    // A reflected function is trusted only when its name, marshalled parameter count and
    // frame size all match the declaration this plugin was written against.
    [[nodiscard]] bool MatchesFunction(const std::uintptr_t function, const std::string_view name,
        const std::uint8_t parameters, const std::uint16_t frame_size) const {
        std::uint8_t observed_parameters{};
        std::uint16_t observed_size{};
        return IsA(function, "Function") &&
            NameAt(function + profile::native_layout.ObjectName) == name &&
            Read(function + profile::native_layout.FunctionCount, observed_parameters) &&
            observed_parameters == parameters &&
            Read(function + profile::native_layout.FunctionSize, observed_size) &&
            observed_size == frame_size;
    }

    // Walks ChildProperties up the class chain, which is how a reflected field keeps its
    // real offset after a game update; a name/size mismatch reports "missing".
    [[nodiscard]] int PropertyOffset(const std::uintptr_t object, const std::string_view name,
        const std::int32_t size) const {
        std::uintptr_t type{};
        if (!Read(object + profile::native_layout.ObjectClass, type)) return -1;
        for (int depth = 0; type != 0 && depth < 32; ++depth) {
            std::uintptr_t property{};
            if (!Read(type + profile::native_layout.StructProperties, property)) return -1;
            for (int index = 0; property != 0 && index < 4096; ++index) {
                if (NameAt(property + profile::native_layout.PropertyName) == name) {
                    std::int32_t observed_size{};
                    std::int32_t observed_offset{};
                    if (!Read(property + profile::native_layout.PropertySize, observed_size) ||
                        observed_size != size ||
                        !Read(property + profile::native_layout.PropertyOffset, observed_offset) ||
                        observed_offset < 0 || observed_offset > 0x20000) {
                        return -1;
                    }
                    return observed_offset;
                }
                std::uintptr_t next{};
                if (!Read(property + profile::native_layout.PropertyNext, next) || next == property) break;
                property = next;
            }
            std::uintptr_t parent{};
            if (!Read(type + profile::native_layout.StructSuper, parent) || parent == type) break;
            type = parent;
        }
        return -1;
    }

    [[nodiscard]] std::uintptr_t PointerField(const std::uintptr_t object, const std::string_view name,
        const int cached_offset) const {
        const auto offset = cached_offset >= 0
            ? cached_offset
            : PropertyOffset(object, name, static_cast<std::int32_t>(sizeof(std::uintptr_t)));
        std::uintptr_t value{};
        return offset >= 0 && Read(object + static_cast<std::uintptr_t>(offset), value) ? value : 0;
    }

    // Score assist: rewrites one replicated int32 that the cash-out request reads.
    [[nodiscard]] bool Write(const std::uintptr_t address, const void* const source,
        const std::size_t size) const noexcept {
        return address > 0x10000U && core_ != nullptr && core_->write_memory != nullptr &&
            core_->write_memory(core_->user, address,
                {static_cast<const std::uint8_t*>(source), size}).code == ANOMALY_STATUS_V1_OK;
    }

private:
    const AnomalyUe5NamesServiceV1* names_{};
    const AnomalyCoreServiceV1* core_{};
};

// The `anomaly.ue5.objects` service hands out generation handles, while the component this
// plugin drives arrives as a plain pointer through reflection. This domain converts
// between the two and re-validates the serial and index on every lookup.
class ObjectDomain final {
public:
    [[nodiscard]] bool Initialize(const Reader& reader, const AnomalyUe5ObjectsServiceV1* objects) {
        if (registry_ != 0) return true;
        const auto now = Clock::now();
        if (now < retry_) return false;
        retry_ = now + std::chrono::seconds(5);
        const auto instruction = profile::compatibility.Address("Objects");
        std::int32_t displacement{};
        if (instruction == 0 || objects == nullptr || !reader.Read(instruction + 3, displacement)) return false;
        const auto target = static_cast<std::uintptr_t>(
            static_cast<std::intptr_t>(instruction) + 7 + static_cast<std::intptr_t>(displacement));
        registry_ = target - profile::native_layout.RegistrySubtract;
        return true;
    }

    [[nodiscard]] std::uintptr_t Pointer(const Reader& reader, const AnomalyGenerationHandleV1 handle) const {
        if (registry_ == 0 || handle.id == 0) return 0;
        const auto& layout = profile::native_layout;
        const auto index = ANOMALY_UE5_OBJECT_HANDLE_INDEX(handle);
        const auto serial = ANOMALY_UE5_OBJECT_HANDLE_SERIAL(handle);
        std::uint32_t count{};
        std::uintptr_t chunks{};
        std::uintptr_t chunk{};
        if (!reader.Read(registry_ + layout.RegistryCount, count) || count > 16U * 1024U * 1024U ||
            index >= count ||
            !reader.Read(registry_ + layout.RegistryItems, chunks) ||
            !reader.Read(chunks + sizeof(void*) * static_cast<std::uintptr_t>(index / layout.ChunkSize), chunk)) {
            return 0;
        }
        const auto item = chunk + static_cast<std::uintptr_t>(layout.ItemStride) * (index % layout.ChunkSize);
        std::uintptr_t object{};
        std::uint32_t observed_serial{};
        std::int32_t internal_index{};
        if (!reader.Read(item, object) ||
            !reader.Read(item + layout.ItemSerial, observed_serial) || observed_serial != serial ||
            !reader.Read(object + layout.ObjectIndex, internal_index) ||
            internal_index != static_cast<std::int32_t>(index)) {
            return 0;
        }
        return object;
    }

    [[nodiscard]] AnomalyGenerationHandleV1 Handle(const Reader& reader, const AnomalyUe5ObjectsServiceV1* objects,
        const std::uintptr_t address) const {
        if (address == 0 || objects == nullptr || objects->snapshot_at == nullptr) return {};
        std::int32_t index{};
        if (!reader.Read(address + profile::native_layout.ObjectIndex, index) || index < 0) return {};
        AnomalyUe5ObjectSnapshotV1 snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (objects->snapshot_at(objects->user, static_cast<std::uint32_t>(index), &snapshot).code !=
            ANOMALY_STATUS_V1_OK) {
            return {};
        }
        return Pointer(reader, snapshot.handle) == address ? snapshot.handle : AnomalyGenerationHandleV1{};
    }

private:
    std::uintptr_t registry_{};
    Clock::time_point retry_{};
};

}  // namespace tycoon
