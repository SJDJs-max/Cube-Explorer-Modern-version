// Native C++20 port of Movecount Coefficient (MCC / algSpeed) for read-only
// ergonomics scoring and optional result sorting in FormulaWindow.
#include "cube/mcc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace cube {
namespace {

enum class FingerLoc : std::uint8_t {
    Home,
    UFlick,
    DFlick,
    FFlick,
    SFlick,
    EFlick,
    MFlick,
    E,
    M,
    Top,
    Bottom,
    LeftU,
    RightU,
    LeftDB,
    RightDB,
    RDown,
    U2Grip,
    Eido
};

struct Finger {
    double time = -1.0;
    FingerLoc loc = FingerLoc::Home;
};

struct TestResult {
    int stop_index = -1; // -1 means completed the entire sequence
    double speed = 0.0;
    int l_wrist = 0;
    int r_wrist = 0;
    double l_last = -1.0;
    double r_last = -1.0;
};

struct MoveTok {
    char raw_sym = ' ';     // exact case: 'R', 'r', 'U', 'u', 'x', ...
    char upper_sym = ' ';   // uppercase: 'R', 'U', 'X', ...
    std::uint8_t power = 1; // 1, 2, 3
    // Canonical code: upper_sym + power
    bool is_upper(char sym, std::uint8_t p) const noexcept { return upper_sym == sym && power == p; }
    bool operator==(const MoveTok& o) const noexcept { return raw_sym == o.raw_sym && power == o.power; }
};

TestResult run_test(std::span<const MoveTok> seq, int l_grip, int r_grip, double speed, const MccParams& p) noexcept {
    Finger lThumb{-1.0, FingerLoc::Home};
    Finger lIndex{-1.0, FingerLoc::Home};
    Finger lMiddle{-1.0, FingerLoc::Home};
    Finger lRing{-1.0, FingerLoc::Home};
    Finger rThumb{-1.0, FingerLoc::Home};
    Finger rIndex{-1.0, FingerLoc::Home};
    Finger rMiddle{-1.0, FingerLoc::Home};
    Finger rRing{-1.0, FingerLoc::Home};
    double lOhCool = -1.0;
    double rOhCool = -1.0;
    int lWrist = l_grip;
    int rWrist = r_grip;
    int grip = 1;
    int udgrip = -1;
    bool has_prev_speed = false;
    double prev_speed = 0.0;
    bool has_first_move_speed = false;
    double first_move_speed = 0.0;

    auto overwork = [&](const Finger& f, FingerLoc prefer, double penalty) noexcept -> double {
        if (f.loc != prefer && (speed - f.time < penalty)) return penalty - speed + f.time;
        return 0.0;
    };
    auto ow = [&](const Finger& f, FingerLoc prefer) noexcept -> double {
        return overwork(f, prefer, p.overwork_mult);
    };
    auto fail = [&](int idx, int lw, int rw) noexcept -> TestResult {
        return {idx,
                speed,
                lw,
                rw,
                std::max({lThumb.time, lIndex.time, lMiddle.time, lRing.time}),
                std::max({rThumb.time, rIndex.time, rMiddle.time, rRing.time})};
    };

    const int n = static_cast<int>(seq.size());
    for (int j = 0; j < n; ++j) {
        const MoveTok move = seq[j];
        const MoveTok prev = j == 0 ? MoveTok{' ', ' ', 0} : seq[j - 1];
        if (has_prev_speed) {
            first_move_speed = speed;
            has_first_move_speed = true;
            speed = prev_speed;
        }
        if (j < n - 1) {
            if ((move.raw_sym == 'U' && seq[j + 1].raw_sym == 'D') ||
                (move.raw_sym == 'D' && seq[j + 1].raw_sym == 'U')) {
                prev_speed = speed;
                has_prev_speed = true;
            }
        }

        const char u = move.upper_sym;
        const std::uint8_t pw = move.power;

        if (u == 'R' && pw == 3) {
            if (rWrist == 2) rWrist = 0;
            else if (rWrist > -1 && !(lWrist >= 1 && rWrist <= 0)) --rWrist;
            else return fail(j, lWrist, rWrist - 1);
            speed += p.wrist_mult;
        } else if (u == 'R' && pw == 1) {
            if (rWrist < 2 && !(lWrist <= -1 && rWrist >= 0)) ++rWrist;
            else return fail(j, lWrist, rWrist + 1);
            speed += p.wrist_mult;
        } else if (u == 'R' && pw == 2) {
            if (rWrist >= 1 && lWrist < 1) rWrist = -1;
            else if (lWrist > -1) rWrist += 2;
            else return fail(j, lWrist, rWrist > 0 ? rWrist - 2 : rWrist + 2);
            speed += p.double_mult * p.wrist_mult;
        } else if (u == 'U' && pw == 1) {
            if (rWrist == 0 && (rThumb.time + p.overwork_mult <= speed || rThumb.loc != FingerLoc::Top) &&
                rIndex.loc != FingerLoc::M) {
                if (ow(rIndex, FingerLoc::Home) <= ow(rMiddle, FingerLoc::Home)) {
                    speed += ow(rIndex, FingerLoc::Home) + 1.0;
                    rIndex = {speed, FingerLoc::UFlick};
                } else {
                    speed += ow(rMiddle, FingerLoc::Home) + 1.0;
                    rIndex = {speed, FingerLoc::UFlick};
                    rMiddle = {speed, FingerLoc::UFlick};
                }
            } else if (rWrist == 1 && lWrist == 0) {
                speed += ow(lIndex, FingerLoc::UFlick);
                if (prev.is_upper('B', 3)) speed += p.moveblock + p.push_mult;
                else speed += p.push_mult;
                lIndex = {speed, FingerLoc::Home};
            } else if (lWrist == 0 && prev.upper_sym != 'F' && prev.upper_sym != 'B') {
                if (lIndex.loc == FingerLoc::UFlick) {
                    speed += overwork(lIndex, FingerLoc::Eido, 0.75 * p.overwork_mult);
                    speed = std::max(speed, lOhCool + 2.5);
                } else {
                    speed += overwork(lIndex, FingerLoc::Eido, 1.25 * p.overwork_mult);
                }
                speed += 1.15 * p.push_mult;
                lIndex = {speed, FingerLoc::UFlick};
                lOhCool = speed;
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'U' && pw == 3) {
            if (lWrist == 0 && (lThumb.time + p.overwork_mult <= speed || lThumb.loc != FingerLoc::Top) &&
                lIndex.loc != FingerLoc::M) {
                if (ow(lIndex, FingerLoc::Home) <= ow(lMiddle, FingerLoc::Home)) {
                    speed += ow(lIndex, FingerLoc::Home) + 1.0;
                    lIndex = {speed, FingerLoc::UFlick};
                } else {
                    speed += ow(lMiddle, FingerLoc::Home) + 1.0;
                    lIndex = {speed, FingerLoc::UFlick};
                    lMiddle = {speed, FingerLoc::UFlick};
                }
            } else if (lWrist == 1 && rWrist == 0) {
                speed += ow(rIndex, FingerLoc::UFlick);
                if (prev.is_upper('B', 1)) speed += p.moveblock + p.push_mult;
                else speed += p.push_mult;
                rIndex = {speed, FingerLoc::Home};
            } else if (rWrist == 0 && prev.upper_sym != 'F' && prev.upper_sym != 'B') {
                if (rIndex.loc == FingerLoc::UFlick) {
                    speed += overwork(rIndex, FingerLoc::Eido, 0.75 * p.overwork_mult);
                    speed = std::max(speed, rOhCool + 2.5);
                } else {
                    speed += overwork(rIndex, FingerLoc::Eido, 1.25 * p.overwork_mult);
                }
                speed += 1.15 * p.push_mult;
                rIndex = {speed, FingerLoc::UFlick};
                rOhCool = speed;
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'U' && pw == 2) {
            if (rWrist == 0 &&
                (lIndex.loc == FingerLoc::M || lWrist != 0 ||
                 std::max({ow(rIndex, FingerLoc::Home), ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::U2Grip)}) <=
                     std::max({ow(lIndex, FingerLoc::Home), ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::U2Grip)}))) {
                speed += ow(rIndex, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += overwork(rRing, FingerLoc::U2Grip, p.moveblock * p.overwork_mult);
                speed += p.double_mult;
                rIndex = {speed, FingerLoc::UFlick};
                rMiddle = {speed, FingerLoc::UFlick};
            } else if (lWrist == 0) {
                speed += ow(lIndex, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += overwork(lRing, FingerLoc::U2Grip, p.moveblock * p.overwork_mult);
                speed += p.double_mult;
                lIndex = {speed, FingerLoc::UFlick};
                lMiddle = {speed, FingerLoc::UFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'D' && pw == 1) {
            if (lWrist == 0 &&
                (rWrist != 0 ||
                 std::max(ow(lRing, FingerLoc::Home), ow(lMiddle, FingerLoc::Home)) <=
                     std::max(ow(rRing, FingerLoc::DFlick), ow(rMiddle, FingerLoc::Home)))) {
                speed += ow(lRing, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                if (prev.upper_sym == 'B') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += p.ring_mult;
                lRing = {speed, FingerLoc::DFlick};
            } else if (rWrist == 0 && prev.upper_sym != 'B') {
                speed += ow(rRing, FingerLoc::DFlick);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += p.ring_mult * p.push_mult;
                rRing = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'D' && pw == 3) {
            if (rWrist == 0 &&
                (lWrist != 0 ||
                 std::max(ow(rRing, FingerLoc::Home), ow(rMiddle, FingerLoc::Home)) <=
                     std::max(ow(lRing, FingerLoc::DFlick), ow(lMiddle, FingerLoc::Home)))) {
                speed += ow(rRing, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                if (prev.upper_sym == 'B') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += p.ring_mult;
                rRing = {speed, FingerLoc::DFlick};
            } else if (lWrist == 0 && prev.upper_sym != 'B') {
                speed += ow(lRing, FingerLoc::DFlick);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += p.ring_mult * p.push_mult;
                lRing = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'D' && pw == 2) {
            if (rWrist == 0 &&
                (lWrist != 0 ||
                 std::max(ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::Home)) <=
                     std::max(ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::Home)))) {
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::Home);
                if (prev.upper_sym == 'B') speed += p.moveblock * 0.5 + p.double_mult * p.ring_mult;
                else speed += p.double_mult * p.ring_mult;
                rRing = {speed, FingerLoc::DFlick};
            } else if (lWrist == 0) {
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::Home);
                if (prev.upper_sym == 'B') speed += p.moveblock * 0.5 + p.double_mult * p.ring_mult;
                else speed += p.double_mult * p.ring_mult;
                lRing = {speed, FingerLoc::DFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'F' && pw == 1) {
            const bool is_lower_f = (move.raw_sym == 'f' && pw == 1);
            if (rWrist == -1) {
                speed += ow(rIndex, FingerLoc::Home) + 1.0;
                rIndex = {speed, FingerLoc::UFlick};
            } else if (lWrist == 1 && !is_lower_f) {
                speed += ow(lRing, FingerLoc::Home);
                if (prev.upper_sym == 'D') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += 1.0;
                lRing = {speed, FingerLoc::DFlick};
            } else if (rWrist == 1 && prev.upper_sym != 'D' && !is_lower_f) {
                speed += ow(rRing, FingerLoc::DFlick);
                speed += p.ring_mult * p.push_mult;
                rRing = {speed, FingerLoc::Home};
            } else if (lWrist == -1 && rWrist == 0 && ow(rIndex, FingerLoc::UFlick) == 0.0) {
                speed += 1.0;
                rIndex = {speed, FingerLoc::FFlick};
            } else if (lWrist == -1 && ow(lIndex, FingerLoc::UFlick) == 0.0 && prev.upper_sym != 'U') {
                speed += p.push_mult;
                lIndex = {speed, FingerLoc::Home};
            } else if (lWrist == -1 && grip == -1) {
                speed += overwork(lThumb, FingerLoc::Top, 0.9 * p.overwork_mult);
                speed += ow(lIndex, FingerLoc::Top);
                if (prev.upper_sym == 'D') speed += 1.8;
                else speed += 1.0;
                ++lWrist;
                lThumb = {speed, FingerLoc::LeftU};
                lIndex = {speed, FingerLoc::Top};
            } else if (lWrist == 0 && grip == -1) {
                speed += ow(lThumb, FingerLoc::Bottom);
                speed += ow(lIndex, FingerLoc::Top);
                if (prev.upper_sym == 'D') speed += 2.05;
                else speed += 1.25;
                lThumb = {speed, FingerLoc::Top};
                lIndex = {speed, FingerLoc::Top};
            } else if (rWrist == 0 && lWrist == 0 && is_lower_f) {
                speed += ow(rIndex, FingerLoc::UFlick);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += 1.0;
                rIndex = {speed, FingerLoc::FFlick};
            } else if (j == 0 && rWrist == 0 && lWrist == 0) {
                speed += ow(rThumb, FingerLoc::Top) + 1.0;
                rThumb = {speed, FingerLoc::RDown};
                rMiddle = {speed, FingerLoc::UFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'F' && pw == 3) {
            const bool is_lower_fp = (move.raw_sym == 'f' && pw == 3);
            // Note: in algSpeed.js case "F'", lines 229 & 233 check `move != "f"` (always true for "F'" and "f'").
            if (lWrist == -1) {
                speed += ow(lIndex, FingerLoc::Home) + 1.0;
                lIndex = {speed, FingerLoc::UFlick};
            } else if (rWrist == 1) {
                speed += ow(rRing, FingerLoc::Home);
                if (prev.upper_sym == 'D') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += 1.0;
                rRing = {speed, FingerLoc::DFlick};
            } else if (lWrist == 1 && prev.upper_sym != 'D') {
                speed += ow(lRing, FingerLoc::DFlick);
                speed += p.ring_mult * p.push_mult;
                lRing = {speed, FingerLoc::Home};
            } else if (rWrist == -1 && lWrist == 0 && ow(lIndex, FingerLoc::UFlick) == 0.0) {
                speed += 1.0;
                lIndex = {speed, FingerLoc::FFlick};
            } else if (rWrist == -1 && ow(rIndex, FingerLoc::UFlick) == 0.0 && prev.upper_sym != 'U') {
                speed += p.push_mult;
                rIndex = {speed, FingerLoc::Home};
            } else if (rWrist == -1 && grip == 1) {
                speed += overwork(rThumb, FingerLoc::Top, 0.9 * p.overwork_mult);
                speed += ow(rIndex, FingerLoc::Top);
                if (prev.upper_sym == 'D') speed += 1.8;
                else speed += 1.0;
                ++rWrist;
                rThumb = {speed, FingerLoc::RightU};
                rIndex = {speed, FingerLoc::Top};
            } else if (rWrist == 0 && grip == 1) {
                speed += ow(rThumb, FingerLoc::Bottom);
                speed += ow(rIndex, FingerLoc::Top);
                if (prev.upper_sym == 'D') speed += 2.05;
                else speed += 1.25;
                rThumb = {speed, FingerLoc::Top};
                rIndex = {speed, FingerLoc::Top};
            } else if (lWrist == 0 && rWrist == 0 && is_lower_fp) {
                speed += ow(lIndex, FingerLoc::UFlick);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += 1.0;
                lIndex = {speed, FingerLoc::FFlick};
            } else if (j == 0 && rWrist == 0 && lWrist == 0) {
                speed += ow(lThumb, FingerLoc::Top) + 1.0;
                lThumb = {speed, FingerLoc::RDown};
                lMiddle = {speed, FingerLoc::UFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'F' && pw == 2) {
            if (rWrist == -1 &&
                (lWrist != -1 ||
                 std::max({ow(rIndex, FingerLoc::Home), ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::U2Grip)}) <=
                     std::max({ow(lIndex, FingerLoc::Home), ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::U2Grip)}))) {
                speed += ow(rIndex, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::U2Grip);
                speed += p.double_mult;
                rIndex = {speed, FingerLoc::UFlick};
                rMiddle = {speed, FingerLoc::UFlick};
            } else if (lWrist == -1) {
                speed += ow(lIndex, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::U2Grip);
                speed += p.double_mult;
                lIndex = {speed, FingerLoc::UFlick};
                lMiddle = {speed, FingerLoc::UFlick};
            } else if (rWrist == 1 &&
                       (lWrist != 1 ||
                        std::max(ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::Home)) <=
                            std::max(ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::Home)))) {
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::Home);
                if (prev.upper_sym == 'D') speed += p.double_mult * p.ring_mult + p.moveblock * 0.5;
                else speed += p.double_mult * p.ring_mult;
                rRing = {speed, FingerLoc::DFlick};
            } else if (lWrist == 1) {
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::Home);
                if (prev.upper_sym == 'D') speed += p.double_mult * p.ring_mult + p.moveblock * 0.5;
                else speed += p.double_mult * p.ring_mult;
                lRing = {speed, FingerLoc::DFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'L' && pw == 1) {
            if (lWrist == 2) lWrist = 0;
            else if (lWrist > -1 && !(rWrist >= 1 && lWrist <= 0)) --lWrist;
            else return fail(j, lWrist - 1, rWrist);
            speed += p.wrist_mult;
        } else if (u == 'L' && pw == 3) {
            if (lWrist < 2 && !(rWrist <= -1 && lWrist >= 0)) ++lWrist;
            else return fail(j, lWrist + 1, rWrist);
            speed += p.wrist_mult;
        } else if (u == 'L' && pw == 2) {
            if (lWrist >= 1 && rWrist < 1) lWrist = -1;
            else if (rWrist > -1) lWrist += 2;
            else return fail(j, lWrist > 0 ? lWrist - 2 : lWrist + 2, rWrist);
            speed += p.double_mult * p.wrist_mult;
        } else if (u == 'B' && pw == 1) {
            if (rWrist == 1) {
                speed += ow(rIndex, FingerLoc::Home) + 1.0;
                rIndex = {speed, FingerLoc::UFlick};
            } else if (lWrist == -1) {
                speed += ow(lRing, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += p.ring_mult;
                lRing = {speed, FingerLoc::DFlick};
            } else if (lWrist == 1 && prev.upper_sym != 'U' && prev.upper_sym != 'D') {
                if (lIndex.loc == FingerLoc::UFlick) {
                    speed += overwork(lIndex, FingerLoc::Eido, 0.75 * p.overwork_mult);
                    speed = std::max(speed, lOhCool + 2.5);
                } else {
                    speed += overwork(lIndex, FingerLoc::Eido, 1.25 * p.overwork_mult);
                }
                speed += 1.15 * p.push_mult;
                lIndex = {speed, FingerLoc::UFlick};
                lOhCool = speed;
            } else if (lWrist == 0 && (rWrist == 1 || rWrist == -1)) {
                speed += overwork(lIndex, FingerLoc::Top, 0.9 * p.overwork_mult);
                if (prev.upper_sym == 'U') speed += 1.45;
                else speed += 1.0;
                lIndex = {speed, FingerLoc::LeftDB};
            } else if (rWrist == -1 && prev.upper_sym != 'U') {
                speed += ow(rRing, FingerLoc::DFlick);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += p.ring_mult * p.push_mult;
                rRing = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'B' && pw == 3) {
            if (lWrist == 1) {
                speed += ow(lIndex, FingerLoc::Home) + 1.0;
                lIndex = {speed, FingerLoc::UFlick};
            } else if (rWrist == -1) {
                speed += ow(rRing, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.ring_mult;
                else speed += p.ring_mult;
                rRing = {speed, FingerLoc::DFlick};
            } else if (rWrist == 1 && prev.upper_sym != 'U' && prev.upper_sym != 'D') {
                if (rIndex.loc == FingerLoc::UFlick) {
                    speed += overwork(rIndex, FingerLoc::Eido, 0.75 * p.overwork_mult);
                    speed = std::max(speed, rOhCool + 2.5);
                } else {
                    speed += overwork(rIndex, FingerLoc::Eido, 1.25 * p.overwork_mult);
                }
                speed += 1.15 * p.push_mult;
                rIndex = {speed, FingerLoc::UFlick};
                rOhCool = speed;
            } else if (rWrist == 0 && (lWrist == 1 || lWrist == -1)) {
                speed += overwork(rIndex, FingerLoc::Top, 0.9 * p.overwork_mult);
                if (prev.upper_sym == 'U') speed += 1.45;
                else speed += 1.0;
                rIndex = {speed, FingerLoc::RightDB};
            } else if (lWrist == -1 && prev.upper_sym != 'U') {
                speed += ow(lRing, FingerLoc::DFlick);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += p.ring_mult * p.push_mult;
                lRing = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'B' && pw == 2) {
            if (rWrist == 1 &&
                (lWrist != 1 ||
                 std::max({ow(rIndex, FingerLoc::Home), ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::U2Grip)}) <=
                     std::max({ow(lIndex, FingerLoc::Home), ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::U2Grip)}))) {
                speed += ow(rIndex, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::U2Grip);
                speed += p.double_mult;
                rIndex = {speed, FingerLoc::UFlick};
                rMiddle = {speed, FingerLoc::UFlick};
            } else if (lWrist == 1) {
                speed += ow(lIndex, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::U2Grip);
                speed += p.double_mult;
                lIndex = {speed, FingerLoc::UFlick};
                lMiddle = {speed, FingerLoc::UFlick};
            } else if (lWrist == -1 &&
                       (rWrist != -1 ||
                        std::max(ow(rMiddle, FingerLoc::Home), ow(rRing, FingerLoc::Home)) >
                            std::max(ow(lMiddle, FingerLoc::Home), ow(lRing, FingerLoc::Home)))) {
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::Home);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.double_mult * p.ring_mult;
                else speed += p.double_mult * p.ring_mult;
                lRing = {speed, FingerLoc::DFlick};
            } else if (rWrist == -1) {
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::Home);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.double_mult * p.ring_mult;
                else speed += p.double_mult * p.ring_mult;
                rRing = {speed, FingerLoc::DFlick};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'S' && pw == 1) {
            if (rWrist == 0 && (lWrist != 0 || overwork(rIndex, FingerLoc::Top, 1.25 * p.overwork_mult) <=
                                                   (p.moveblock * 0.5 + p.push_mult - 1.0) * p.se_slice_mult)) {
                speed += overwork(rIndex, FingerLoc::Top, 1.25 * p.overwork_mult);
                speed += p.se_slice_mult;
                rIndex = {speed, FingerLoc::SFlick};
            } else if (lWrist == 0 && rWrist == -1) {
                speed += overwork(rIndex, FingerLoc::Home, 1.25 * p.overwork_mult);
                speed += overwork(rThumb, FingerLoc::Top, 1.25 * p.overwork_mult);
                speed += overwork(rMiddle, FingerLoc::Home, 1.25 * p.overwork_mult);
                speed += p.se_slice_mult;
                rThumb = {speed, FingerLoc::Top};
                rMiddle = {speed, FingerLoc::EFlick};
            } else if (lWrist == 0 && (rWrist == 0 || (rWrist == 1 && (prev.is_upper('R', 1) || prev.is_upper('L', 1))))) {
                speed += overwork(lIndex, FingerLoc::UFlick, 1.25 * p.overwork_mult);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.push_mult * p.se_slice_mult;
                else speed += p.push_mult * p.se_slice_mult;
                lIndex = {speed, FingerLoc::Top};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'S' && pw == 3) {
            if (lWrist == 0 && (rWrist != 0 || overwork(lIndex, FingerLoc::Top, 1.25 * p.overwork_mult) <=
                                                   (p.moveblock * 0.5 + p.push_mult - 1.0) * p.se_slice_mult)) {
                speed += overwork(lIndex, FingerLoc::Top, 1.25 * p.overwork_mult);
                speed += p.se_slice_mult;
                lIndex = {speed, FingerLoc::SFlick};
            } else if (rWrist == 0 && lWrist == -1) {
                speed += overwork(lIndex, FingerLoc::Home, 1.25 * p.overwork_mult);
                speed += overwork(lThumb, FingerLoc::Bottom, 1.25 * p.overwork_mult);
                speed += overwork(lMiddle, FingerLoc::Home, 1.25 * p.overwork_mult);
                speed += p.se_slice_mult;
                lThumb = {speed, FingerLoc::Top};
                lMiddle = {speed, FingerLoc::EFlick};
            } else if (rWrist == 0 && (lWrist == 0 || (lWrist == 1 && (prev.is_upper('R', 1) || prev.is_upper('L', 1))))) {
                speed += overwork(rIndex, FingerLoc::UFlick, 1.25 * p.overwork_mult);
                if (prev.upper_sym == 'U') speed += p.moveblock * 0.5 + p.push_mult * p.se_slice_mult;
                else speed += p.push_mult * p.se_slice_mult;
                rIndex = {speed, FingerLoc::Top};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'S' && pw == 2) {
            if ((rWrist == -1 || rWrist == 1) && lWrist == 0) {
                speed += ow(rThumb, FingerLoc::Home);
                speed += ow(rIndex, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::U2Grip);
                speed += p.se_slice_mult * p.double_mult;
                rMiddle = {speed, FingerLoc::E};
                rIndex = {speed, FingerLoc::E};
            } else if ((lWrist == -1 || lWrist == 1) && rWrist == 0) {
                speed += ow(lThumb, FingerLoc::Home);
                speed += ow(lIndex, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::U2Grip);
                speed += p.se_slice_mult * p.double_mult;
                // Preserves verbatim algSpeed.js assignment for exact numerical parity.
                rMiddle = {speed, FingerLoc::E};
                rIndex = {speed, FingerLoc::E};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'E' && pw == 1) {
            if ((rWrist == 1 || rWrist == -1) && lWrist == 0) {
                speed += ow(lIndex, FingerLoc::Home);
                speed += p.se_slice_mult;
                lIndex = {speed, FingerLoc::E};
            } else if ((lWrist == 1 || lWrist == -1) && rWrist == 0 && prev.upper_sym != 'B') {
                speed += ow(rIndex, FingerLoc::E);
                speed += p.se_slice_mult * p.push_mult;
                rIndex = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'E' && pw == 3) {
            if ((lWrist == 1 || lWrist == -1) && rWrist == 0) {
                speed += ow(rIndex, FingerLoc::Home);
                speed += p.se_slice_mult;
                rIndex = {speed, FingerLoc::E};
            } else if ((rWrist == 1 || rWrist == -1) && lWrist == 0 && prev.upper_sym != 'B') {
                speed += ow(lIndex, FingerLoc::E);
                speed += p.se_slice_mult * p.push_mult;
                lIndex = {speed, FingerLoc::Home};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'E' && pw == 2) {
            if ((lWrist == 1 || lWrist == -1) && rWrist == 0) {
                speed += ow(rIndex, FingerLoc::Home);
                speed += ow(rMiddle, FingerLoc::Home);
                speed += ow(rRing, FingerLoc::U2Grip);
                speed += p.se_slice_mult * p.double_mult;
                rIndex = {speed, FingerLoc::E};
                rMiddle = {speed, FingerLoc::E};
            } else if ((rWrist == 1 || rWrist == -1) && lWrist == 0) {
                speed += ow(lIndex, FingerLoc::Home);
                speed += ow(lMiddle, FingerLoc::Home);
                speed += ow(lRing, FingerLoc::U2Grip);
                speed += p.se_slice_mult * p.double_mult;
                lIndex = {speed, FingerLoc::E};
                lMiddle = {speed, FingerLoc::E};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'M' && pw == 3) {
            if (lWrist == 0) {
                speed += ow(lThumb, FingerLoc::Home);
                speed += ow(lIndex, FingerLoc::M);
                speed += ow(lMiddle, FingerLoc::M);
                speed += ow(lRing, FingerLoc::M);
                if (prev.upper_sym == 'B') speed += 1.8;
                else speed += 1.0;
                lThumb = {speed, FingerLoc::Home};
                lIndex = {speed, FingerLoc::M};
                lMiddle = {speed, FingerLoc::MFlick};
                lRing = {speed, FingerLoc::M};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'M' && pw == 1) {
            if (lWrist == 0 && prev.upper_sym != 'B') {
                speed += ow(lThumb, FingerLoc::Home);
                speed += ow(lIndex, FingerLoc::M);
                speed += overwork(lMiddle, FingerLoc::MFlick, 1.25 * p.overwork_mult);
                speed += ow(lRing, FingerLoc::M);
                speed += p.push_mult;
                lThumb = {speed, FingerLoc::Home};
                lIndex = {speed, FingerLoc::M};
                lMiddle = {speed, FingerLoc::M};
                lRing = {speed, FingerLoc::M};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'M' && pw == 2) {
            if (lWrist == 0) {
                speed += ow(lThumb, FingerLoc::Home);
                speed += ow(lIndex, FingerLoc::M);
                speed += ow(lMiddle, FingerLoc::M);
                speed += ow(lRing, FingerLoc::M);
                if (prev.upper_sym == 'B') speed += p.moveblock + p.double_mult;
                else speed += p.double_mult;
                lThumb = {speed, FingerLoc::Home};
                lIndex = {speed, FingerLoc::M};
                lMiddle = {speed, FingerLoc::MFlick};
                lRing = {speed, FingerLoc::M};
            } else {
                return fail(j, lWrist, rWrist);
            }
        } else if (u == 'X' && pw == 1) {
            ++lWrist;
            ++rWrist;
            if (lWrist > 1 || rWrist > 1) return fail(j + 1, lWrist, rWrist);
        } else if (u == 'X' && pw == 3) {
            --lWrist;
            --rWrist;
            if (lWrist < -1 || rWrist < -1) return fail(j + 1, lWrist, rWrist);
        } else if (u == 'X' && pw == 2) {
            if (lWrist >= 1 && rWrist >= 1) {
                lWrist -= 2;
                rWrist -= 2;
            } else if (lWrist <= -1 && rWrist <= -1) {
                lWrist += 2;
                rWrist += 2;
            } else if (lWrist + rWrist > 0) {
                return fail(j, lWrist - 2, rWrist - 2);
            } else {
                return fail(j, lWrist + 2, rWrist + 2);
            }
        } else if ((u == 'Y' || u == 'Z') && (pw == 1 || pw == 3)) {
            speed += p.rotation;
            return fail(j + 1, 0, 0);
        } else if ((u == 'Y' || u == 'Z') && pw == 2) {
            speed += p.rotation * p.double_mult;
            return fail(j + 1, 0, 0);
        } else {
            return fail(j, lWrist, rWrist);
        }

        if (has_first_move_speed) {
            speed = std::max(first_move_speed, speed) + 0.5;
            has_prev_speed = false;
            has_first_move_speed = false;
        }
        if ((move.raw_sym == 'R' || move.raw_sym == 'l') && grip == -1) {
            grip = 1;
            speed += 0.65;
        } else if ((move.raw_sym == 'r' || move.raw_sym == 'L') && grip == 1) {
            grip = -1;
            speed += 0.65;
        }
        if (move.raw_sym == 'd' && udgrip == -1) {
            udgrip = 1;
            speed += 2.25;
        } else if ((move.raw_sym == 'U' || move.raw_sym == 'u') && udgrip == 1) {
            udgrip = -1;
            speed += 2.25;
        }
        if (j >= 2) {
            if ((u == 'R' && pw == 1 && move == seq[j - 2] && seq[j - 1].is_upper('U', 3)) ||
                (u == 'R' && pw == 3 && move == seq[j - 2] && seq[j - 1].is_upper('U', 1))) {
                speed -= 0.5;
            } else if ((u == 'R' && pw == 1 && move == seq[j - 2] && seq[j - 1].is_upper('D', 3) && rWrist == 1) ||
                       (u == 'R' && pw == 3 && move == seq[j - 2] && seq[j - 1].is_upper('D', 1))) {
                speed -= 0.3;
            }
        }
        if (u == 'U' && pw == 1 && (lWrist == -1 || rWrist == -1)) speed += p.destabilize;
        if (u == 'B' && pw == 1 && (lWrist == 0 || rWrist == 0)) speed += p.destabilize;
        if (u == 'D' && pw == 1 && (lWrist == 1 || rWrist == 1)) speed += p.destabilize;
        if (u == 'S' && pw == 1 && (lWrist == 1 || rWrist == 1 || lWrist == -1 || rWrist == -1)) speed += p.destabilize;
        if (u == 'E' && pw == 1 && (lWrist == 0 || rWrist == 0)) speed += p.destabilize;
    }
    return {-1, speed, l_grip, r_grip, -1.0, -1.0};
}

} // namespace

double compute_mcc(std::span<const Move> moves, const MccParams& params, bool ignore_pre_auf) noexcept {
    std::vector<MoveTok> seq;
    seq.reserve(moves.size());
    for (auto m : moves) {
        const char up = static_cast<char>(std::toupper(static_cast<unsigned char>(m.symbol)));
        seq.push_back({m.symbol, up, m.power});
    }
    if (ignore_pre_auf && !seq.empty()) {
        if (seq[0].raw_sym == 'U') {
            seq.erase(seq.begin());
        } else if (seq.size() >= 2 && (seq[0].raw_sym == 'd' || seq[0].raw_sym == 'D') && seq[1].raw_sym == 'U') {
            seq[1] = seq[0];
            seq.erase(seq.begin());
        }
    }
    if (seq.empty()) return 0.0;

    std::size_t offset = 0;
    auto slice = [&]() -> std::span<const MoveTok> {
        return std::span<const MoveTok>(seq.data() + offset, seq.size() - offset);
    };
    std::vector<TestResult> tests;
    tests.reserve(16);
    tests.push_back(run_test(slice(), 0, 0, 0.0, params));
    tests.push_back(run_test(slice(), 0, -1, 1.0 + params.add_regrip, params));
    tests.push_back(run_test(slice(), 0, 1, 1.0 + params.add_regrip, params));
    tests.push_back(run_test(slice(), -1, 0, 1.0 + params.add_regrip, params));
    tests.push_back(run_test(slice(), 1, 0, 1.0 + params.add_regrip, params));

    // Guard against pathological sequences causing infinite regrip loops.
    for (int iter = 0; iter < 512; ++iter) {
        TestResult best = tests[0];
        for (std::size_t i = 1; i < tests.size(); ++i) {
            const auto& comp = tests[i];
            if (comp.stop_index == -1 && (best.stop_index != -1 || best.speed > comp.speed)) best = comp;
            else if (comp.stop_index > best.stop_index && best.stop_index != -1) best = comp;
            else if (comp.stop_index == best.stop_index && comp.speed < best.speed && best.stop_index != -1) best = comp;
        }
        if (best.stop_index == -1) return std::floor(best.speed * 10.0 + 0.5) / 10.0;

        const auto cur_slice = slice();
        const char prev_type = best.stop_index >= 1 ? cur_slice[best.stop_index - 1].raw_sym : ' ';
        const char prev2_type = best.stop_index >= 2 ? cur_slice[best.stop_index - 2].raw_sym : ' ';
        const bool double_regrip = (best.l_wrist > 1 || best.l_wrist < -1) && (best.r_wrist > 1 || best.r_wrist < -1);
        const bool prev_is_rot = (prev_type == 'X' || prev_type == 'x' || prev_type == 'Y' || prev_type == 'y' ||
                                  prev_type == 'Z' || prev_type == 'z');

        offset += static_cast<std::size_t>(best.stop_index);
        const auto next_slice = slice();
        tests.clear();
        for (int lw = -1; lw < 2; ++lw) {
            for (int rw = -1; rw < 2; ++rw) {
                const bool left_match = (best.l_wrist == lw);
                const bool right_match = (best.r_wrist == rw);
                if (prev_is_rot) {
                    tests.push_back(run_test(next_slice, lw, rw, best.speed, params));
                } else {
                    const double r_move_lat =
                        (prev_type == 'R' || prev2_type == 'R' || prev_type == 'r' || prev2_type == 'r') ? 1.0 : 0.0;
                    const double l_move_lat =
                        (prev_type == 'L' || prev2_type == 'L' || prev_type == 'l' || prev2_type == 'l') ? 1.0 : 0.0;
                    if (left_match || double_regrip) {
                        const double r_hand_lat = std::max(0.0, 2.0 - (best.speed - best.r_last));
                        const double penalty = std::max({r_hand_lat, r_move_lat, l_move_lat * 2.0});
                        tests.push_back(run_test(next_slice, lw, rw, best.speed + penalty + params.add_regrip, params));
                    } else if (right_match) {
                        const double l_hand_lat = std::max(0.0, 2.0 - (best.speed - best.l_last));
                        const double penalty = std::max({l_hand_lat, l_move_lat, r_move_lat * 2.0});
                        tests.push_back(run_test(next_slice, lw, rw, best.speed + penalty + params.add_regrip, params));
                    }
                }
            }
        }
        if (tests.empty()) break;
    }
    return 0.0;
}

} // namespace cube
