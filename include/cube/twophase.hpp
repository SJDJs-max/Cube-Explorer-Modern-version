#pragma once

#include "cube/model.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace cube {

// The ordinary solver retains Cube Explorer's HTM, outer-face two-phase search.
// Pattern/wide/mod-U options deliberately do not enter this interface.
struct TwoPhaseOptions {
    int max_moves = 30;
    int target_moves = 20;
    int max_phase1_moves = 20;
    std::chrono::milliseconds time_limit{5000}; // zero means no time limit
    std::uint64_t node_limit = 0;               // zero means no node limit
    bool use_triple_search = false;            // original optional UD/RL/FB search
};

enum class TwoPhaseStop {
    target_reached,
    time_limit,
    node_limit,
    cancelled,
    depth_limit,
    invalid_input
};

struct TwoPhaseSolution {
    std::vector<Move> moves;
    int turn_count = 0;
    int phase1_count = 0;
    int phase2_count = 0;
    CubeState final_state;
    int search_axis = 0; // logical center frame: 0=UD, 1=RL, 2=FB
};

struct TwoPhaseResult {
    bool solved = false;
    TwoPhaseSolution solution;
    TwoPhaseStop stop_reason = TwoPhaseStop::depth_limit;
    std::uint64_t nodes = 0;
    std::chrono::milliseconds elapsed{};
    std::string message;
    // Two-phase's historical G1-prefix pruning is not a proof of optimality.
    bool optimality_proven = false;
    int search_axes = 1;
};

struct TwoPhaseTableInfo {
    std::uint64_t resident_bytes = 0;
    std::size_t flip_slice_classes = 64430;
    std::size_t corner_classes = 2768;
    std::string source_version = "Cube Explorer 5.15 / original HTM tables";
};

// A cursor into the original phase-one projection. This is a read-only view of
// the shared PDB, used by the independent three-axis optimal search. Construct
// with phase1_cursor and advance only with indices 0..17 (URFDLB, three powers).
struct Phase1Cursor {
    std::uint32_t flip_slice = 0;
    std::uint16_t twist = 0;
    std::int8_t distance = 0;
};

class TwoPhaseSolver {
public:
    using Progress = std::function<void(std::string_view)>;
    using OnSolution = std::function<void(const TwoPhaseSolution&)>;

    // This directory must contain RawFlipSlice, flipUDslice.move,
    // phase1PF.prun and phase2PF.prun. It is only ever opened for reading.
    explicit TwoPhaseSolver(const std::filesystem::path& table_directory,
                            Progress progress = {}, std::stop_token preparation_stop = {});
    ~TwoPhaseSolver();
    TwoPhaseSolver(const TwoPhaseSolver&) noexcept;
    TwoPhaseSolver& operator=(const TwoPhaseSolver&) noexcept;
    TwoPhaseSolver(TwoPhaseSolver&&) noexcept;
    TwoPhaseSolver& operator=(TwoPhaseSolver&&) noexcept;

    [[nodiscard]] TwoPhaseResult solve(const CubeState& input,
        const TwoPhaseOptions& options = {}, std::stop_token stop = {},
        OnSolution on_solution = {}) const;
    [[nodiscard]] TwoPhaseTableInfo table_info() const;

    // Exact distance to G1 according to the original phase-one PDB.
    // Exposed for diagnostics and migration regression tests, not mod-U search.
    [[nodiscard]] int phase1_lower_bound(const CubieState& input) const;
    [[nodiscard]] Phase1Cursor phase1_cursor(const CubieState& input) const;
    [[nodiscard]] Phase1Cursor advance_phase1(Phase1Cursor cursor, int move_index) const noexcept;

private:
    struct Tables;
    std::shared_ptr<const Tables> tables_;
};

[[nodiscard]] std::string_view to_string(TwoPhaseStop reason) noexcept;

} // namespace cube
