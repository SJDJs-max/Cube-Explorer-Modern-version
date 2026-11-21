#pragma once

#include "cube/model.hpp"
#include <span>

namespace cube {

// Movecount Coefficient (MCC) parameters matching Trangium's algSpeed model.
struct MccParams {
    double wrist_mult = 0.8;
    double push_mult = 1.3;
    double ring_mult = 1.4;
    double destabilize = 0.5;
    double add_regrip = 1.0;
    double double_mult = 1.65;
    double se_slice_mult = 1.25;
    double overwork_mult = 2.25;
    double moveblock = 0.8;
    double rotation = 3.5;

    bool operator==(const MccParams&) const = default;
};

// Computes MCC (Movecount Coefficient, rounded to 1 decimal place) directly from
// structured moves without altering the moves or search turn counts.
// When ignore_pre_auf is true, a leading U (or D/d followed by U) is excluded
// from MCC scoring only, matching the user's MCC tool convention.
[[nodiscard]] double compute_mcc(std::span<const Move> moves,
                                 const MccParams& params = {},
                                 bool ignore_pre_auf = false) noexcept;

} // namespace cube
