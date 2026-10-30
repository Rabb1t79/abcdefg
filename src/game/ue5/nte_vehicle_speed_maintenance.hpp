#pragma once

// NTE player-vehicle speed-cap maintenance (portable host add-on).
//
// Why this exists
// ---------------
// UHTVehicleMovementComponent owns its own speed state: TopSpeedRatio is
// BlueprintReadOnly, bEnableTopSpeedLimit is EditConst and
// ReSetupEngineAndTransmission rebuilds the engine/transmission from the data
// table. A raised cap written once therefore survives only until the component
// next recomputes it; the vehicle then falls back to its stock top speed while
// the apply call already reported success (observed: the multiplier reports
// applied but the car still stops at its stock speed).
//
// Keep the computed targets from the successful apply and re-assert them from
// the captured stock baseline whenever the observed fields no longer cover
// them. Repeated passes reuse the same targets, so the factor never compounds,
// and maintenance is limited to factors above 1.0 (raising the cap only).
//
// Portability
// -----------
// This header is deliberately self-contained: it only needs <algorithm>,
// <cmath>, <string> and a Fields value type. To move the fix to another host
// line:
//   1. copy this header next to that line's ue5 adapter;
//   2. add a `NteVehicleSpeedMaintenance<VehicleSpeedFieldBaseline>` member;
//   3. Arm() it where the speed fields were written successfully;
//   4. Reset() it whenever the current vehicle / movement component changes,
//      after a rollback, and on the stock reset path;
//   5. call Maintain() from the Game tick with the adapter's existing
//      read/write helpers.
// Fields only needs `valid`, `top_speed_kmh`, `max_linear_velocity` and normal
// copy semantics.

#include <algorithm>
#include <cmath>
#include <string>

namespace anomaly {
namespace nte_vehicle_speed {

enum class MaintenanceResult {
    // Nothing to do, or the observed fields still cover the armed targets.
    Unchanged,
    // The game reverted the cap and the armed targets were written again.
    Rewritten,
    // The rewrite failed; the caller must disarm and report the failure.
    WriteFailed
};

template <typename Fields>
class NteVehicleSpeedMaintenance final {
public:
    NteVehicleSpeedMaintenance() = default;

    void Reset() noexcept {
        targets_ = Fields{};
        requested_ratio_ = 1.0F;
        armed_ = false;
    }

    // Call after the speed fields were written and verified.
    void Arm(const Fields& targets, const float requested_ratio) noexcept {
        targets_ = targets;
        requested_ratio_ = std::isfinite(requested_ratio) ? requested_ratio : 1.0F;
        armed_ = targets_.valid && requested_ratio_ > 1.0F;
    }

    [[nodiscard]] bool Armed() const noexcept { return armed_; }

    // read_observed: bool(Fields&) - fills the current movement-component values.
    // write_targets: bool(const Fields&, std::string&) - writes and readback-validates.
    template <typename ReadObserved, typename WriteTargets>
    MaintenanceResult Maintain(
        const ReadObserved& read_observed,
        const WriteTargets& write_targets,
        std::string& failure) const {
        if (!armed_ || requested_ratio_ <= 1.001F) return MaintenanceResult::Unchanged;
        Fields observed{};
        if (!read_observed(observed)) return MaintenanceResult::Unchanged;
        // 1% slack covers float round-trip and the game's own rounding.
        if (observed.top_speed_kmh >= targets_.top_speed_kmh * 0.99F &&
            observed.max_linear_velocity >= targets_.max_linear_velocity * 0.99F) {
            return MaintenanceResult::Unchanged;
        }
        if (!write_targets(targets_, failure)) return MaintenanceResult::WriteFailed;
        return MaintenanceResult::Rewritten;
    }

private:
    Fields targets_{};
    float requested_ratio_{1.0F};
    bool armed_{};
};

}  // namespace nte_vehicle_speed
}  // namespace anomaly
