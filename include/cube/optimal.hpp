#pragma once

#include "cube/twophase.hpp"
#include <optional>

namespace cube {

struct OptimalOptions {
    int max_moves = 30;
    std::chrono::milliseconds time_limit{0}; // Search time; zero means unlimited.
    std::uint64_t node_limit = 0;
    // An independently verified ordinary candidate may be retained if search
    // stops before proving optimality. Moves use the input's physical frame.
    std::optional<std::vector<Move>> initial_solution;
    // For the original result-row "next" action. Canonical format_moves strings
    // already returned for this same input. Exclusion never proves optimality.
    std::vector<std::string> excluded_solutions;
};

enum class OptimalStop { found, time_limit, node_limit, cancelled, depth_limit, invalid_input };

struct OptimalSolution {
    std::vector<Move> moves;
    int turn_count = 0;
    CubeState final_state;
};

struct OptimalProgress {
    int depth = 0;
    int completed_depth = -1;
    std::uint64_t nodes = 0;
    std::chrono::milliseconds elapsed{};
};

struct OptimalResult {
    bool solved = false;
    OptimalSolution solution;
    bool optimality_proven = false;
    OptimalStop stop_reason = OptimalStop::depth_limit;
    std::uint64_t nodes = 0;
    std::chrono::milliseconds elapsed{};
    int completed_depth = -1;
    int lower_bound = 0;
    std::string message;
};

// Original standard optimal HTM search: exact IDA* over 18 outer-face moves,
// using the original phase-one PDB in the UD, RL and FB axes. Formula-mode
// modulo-U/wide rules are deliberately absent. No huge-table download needed.
class OptimalSolver {
public:
    using OnProgress = std::function<void(const OptimalProgress&)>;
    explicit OptimalSolver(const std::filesystem::path& tables,
                           TwoPhaseSolver::Progress preparation = {},
                           std::stop_token preparation_stop = {});
    explicit OptimalSolver(TwoPhaseSolver shared_tables);
    [[nodiscard]] OptimalResult solve(const CubeState&, const OptimalOptions& = {},
                                     std::stop_token = {}, OnProgress = {}) const;
    [[nodiscard]] int lower_bound(const CubieState&) const;
private:
    TwoPhaseSolver tables_;
};

[[nodiscard]] std::string_view to_string(OptimalStop) noexcept;

} // namespace cube
