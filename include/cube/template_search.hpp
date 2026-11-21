#pragma once

#include "cube/model.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>

namespace cube {

using FaceTemplate = std::array<std::uint8_t, 9>;
struct TemplateSearchRequest {
    // Six abstract equality classes, not literal URFDLB colors. Label 5 is gray.
    // An entirely gray template is unused; gray in a mixed template is a class.
    std::array<FaceTemplate, 5> templates;
    std::array<std::array<bool, 5>, 6> allowed_templates; // URFDLB x template.
    bool continuous = false;
    std::uint64_t max_results = 0;
    std::chrono::milliseconds time_limit{0};
    std::uint64_t node_limit = 0;
    TemplateSearchRequest();
};

enum class TemplateSearchStop { Exhausted, Cancelled, ResultLimit, TimeLimit, NodeLimit, InvalidInput };
struct TemplateSearchProgress {
    std::uint64_t nodes = 0, results = 0;
    int assigned_corners = 0, assigned_edges = 0;
    std::chrono::milliseconds elapsed{};
};
struct TemplateSearchReport {
    TemplateSearchStop stop = TemplateSearchStop::Exhausted;
    std::uint64_t nodes = 0, results = 0;
    std::chrono::milliseconds elapsed{};
    std::string message;
    [[nodiscard]] bool exhausted() const noexcept { return stop == TemplateSearchStop::Exhausted; }
};

using TemplateSolutionCallback = std::function<void(const CubeState &)>;
using TemplateProgressCallback = std::function<void(const TemplateSearchProgress &)>;

// Enumerates complete legal states, not maneuver strings. Find Generators is a
// UI action that sends each state to the existing ordinary two-phase solver.
// Per-face rotations/reflections are included. Global cube-isomorphism filtering
// and inversion equivalence are intentionally not applied.
[[nodiscard]] TemplateSearchReport enumerate_templates(const TemplateSearchRequest &,
    TemplateSolutionCallback on_result = {}, TemplateProgressCallback on_progress = {},
    std::stop_token stop = {});
// Independent complete-face partition and legality check used before emission.
[[nodiscard]] Validation verify_template_result(const TemplateSearchRequest &, const CubeState &);
[[nodiscard]] std::string_view template_search_stop_name(TemplateSearchStop) noexcept;

} // namespace cube
