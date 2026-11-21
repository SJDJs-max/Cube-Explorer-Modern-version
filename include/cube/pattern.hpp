#pragma once

#include "cube/model.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace cube {

// -1 has the same meaning as the original NNN/NN and orientation 6:
// the corresponding identity or orientation is unconstrained. These are
// independent fields: an orientation-only marker is not an unknown piece.
struct PatternState {
    std::array<std::int8_t, 8> cp{}, co{};
    std::array<std::int8_t, 12> ep{}, eo{};
    std::array<std::int8_t, 6> center_ori{};
    std::uint8_t frame = 0;
    PatternState();
    bool operator==(const PatternState&) const = default;
};

[[nodiscard]] PatternState pattern_from_cube(const CubeState&, bool include_centers = false);
[[nodiscard]] PatternState apply_pattern(const PatternState&, Move);
[[nodiscard]] PatternState apply_pattern(const PatternState&, std::span<const Move>);
[[nodiscard]] Validation validate_pattern(const PatternState&);
// 54 URFDLB facelets. ?/. = unspecified; lowercase colors = ignore cubie
// orientation; @ on one sticker (others unspecified) = orientation only.
// Center orientation arrows are a separate center_ori field; this color/marker
// text format does not serialize arrows.
[[nodiscard]] PatternState parse_pattern_facelets(std::string_view);
[[nodiscard]] std::string format_pattern_facelets(const PatternState&);

enum class PatternDirection { Solve, Generate };
enum class PatternStop { Exhausted, ResultLimit, TimeLimit, NodeLimit, Cancelled, InvalidInput };
enum class PatternHeuristic { None, Standard, Strong };

struct PatternRequest {
    PatternState pattern;
    PatternDirection direction = PatternDirection::Solve;
    std::vector<Move> allowed_moves = all_moves(false, false, false);
    int min_turns = 0;
    // -1 searches successive depths until cancelled; 0 means zero turns.
    int max_turns = -1;
    std::uint64_t max_nodes = 0;
    std::size_t max_results = 0;
    std::chrono::milliseconds time_limit{0};
    // Streaming callers may retain results externally without duplicating the
    // complete output in PatternReport. The report.results counter is always
    // maintained, regardless of collection.
    bool collect_solutions = true;
    bool modulo_u = true;
    bool remove_u_symmetric_prefix = true;
    bool reject_alternating_triples = true;
    bool deduplicate_commuting = true;
    // Cache only completely explored subtrees having no eligible completion,
    // including the remaining budget and the move-grammar context in the key.
    bool memoize_dead_ends = true;
    // Only enabled after a mechanical reachability check proves that free
    // rotations can reset the notation context for every abstract paid move.
    bool quotient_free_rotations = true;
    // Optional fine-grained usage limits (-1 means unlimited; 0 forbids).
    int max_rotations = -1;
    std::array<int, 18> max_symbol_uses{-1, -1, -1, -1, -1, -1, -1, -1, -1,
                                        -1, -1, -1, -1, -1, -1, -1, -1, -1};
    PatternHeuristic heuristic = PatternHeuristic::Standard;
    std::function<bool()> cancelled;
};

struct PatternSolution {
    std::vector<Move> moves;
    int turn_cost = 0;
    int rotations = 0;
    int residual_u = 0;
    PatternState terminal;
    bool verified = false;
    // All shorter admissible depths were ruled out by lower bounds or complete
    // enumeration. Only results at that first proven depth carry the mark.
    bool optimality_proven = false;
};

struct PatternProgress {
    std::uint64_t nodes = 0;
    int depth = 0;
    std::size_t results = 0;
    std::chrono::milliseconds elapsed{};
    bool preparing = false;
    // Exactly one forced event at the beginning of each enumerated depth.
    bool depth_started = false;
};

struct PatternReport {
    std::vector<PatternSolution> solutions;
    std::size_t results = 0;
    PatternStop stop = PatternStop::Exhausted;
    std::string message;
    std::uint64_t nodes = 0;
    int completed_depth = -1;
    std::chrono::milliseconds elapsed{}, preparation_time{};
    std::size_t pruning_entries = 0;
    bool root_u_symmetric = false;
    bool free_rotation_quotient = false;
    // For a finite maximum: all canonical paths through that depth. With an
    // unlimited request, this is returned only for a finitely proved case
    // (already at a goal, no paid actions, or unreachable abstraction).
    [[nodiscard]] bool exhausted() const noexcept { return stop == PatternStop::Exhausted; }
};

using PatternSolutionCallback = std::function<void(const PatternSolution&)>;
using PatternProgressCallback = std::function<void(const PatternProgress&)>;

[[nodiscard]] PatternReport enumerate_pattern(const PatternRequest&,
    PatternSolutionCallback on_solution = {}, PatternProgressCallback on_progress = {});
[[nodiscard]] Validation verify_pattern_solution(const PatternRequest&, const PatternSolution&);
[[nodiscard]] bool violates_alternating_triple(std::span<const Move>, Move next) noexcept;
// Equivalence only under adjacent swaps of commuting physical transformations;
// never reduces a word to its final cube state or changes the returned word.
[[nodiscard]] std::string commuting_key(std::span<const Move>);
[[nodiscard]] std::string_view pattern_stop_name(PatternStop) noexcept;

} // namespace cube
