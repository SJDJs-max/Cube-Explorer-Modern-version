#include "cube/template_search.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace cube {
namespace {
using Clock = std::chrono::steady_clock;
using Variants = std::array<std::vector<FaceTemplate>, 6>;
constexpr std::array<std::array<int, 2>, 12> edge_neighbors = {{{0,3},{1,0},{2,1},{3,2},
    {7,4},{4,5},{5,6},{6,7},{0,4},{5,1},{6,2},{3,7}}}; // CubeDefs.EN

Validation validate_request(const TemplateSearchRequest &request) {
    for (const auto &face : request.templates)
        for (auto label : face)
            if (label > 5) return {false, "Template type labels must be in 0..5."};
    if (request.time_limit.count() < 0) return {false, "Template time limit must be nonnegative."};
    return {};
}
bool unused(const FaceTemplate &face) {
    return std::all_of(face.begin(), face.end(), [](auto label) { return label == 5; });
}
FaceTemplate rotated(const FaceTemplate &face) {
    FaceTemplate result;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
        result[r * 3 + c] = face[(2 - c) * 3 + r];
    return result;
}
FaceTemplate reflected(const FaceTemplate &face) {
    FaceTemplate result;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
        result[r * 3 + c] = face[r * 3 + 2 - c];
    return result;
}
Variants prepare(const TemplateSearchRequest &request) {
    Variants result;
    for (int t = 0; t < 5; ++t) {
        if (unused(request.templates[t])) continue;
        std::array<FaceTemplate, 8> variants;
        variants[0] = request.templates[t];
        for (int k = 1; k < 4; ++k) variants[k] = rotated(variants[k - 1]);
        for (int k = 0; k < 4; ++k) variants[k + 4] = reflected(variants[k]);
        for (int face = 0; face < 6; ++face) if (request.allowed_templates[face][t])
            for (const auto &variant : variants)
                if (std::find(result[face].begin(), result[face].end(), variant) == result[face].end())
                    result[face].push_back(variant);
    }
    return result;
}
bool partial_match(const FaceTemplate &pattern, const std::int8_t *stickers) {
    std::array<int, 6> color_to_type, type_to_color;
    color_to_type.fill(-1);
    type_to_color.fill(-1);
    for (int i = 0; i < 9; ++i) {
        const auto color = stickers[i];
        if (color < 0) continue;
        const auto type = pattern[i];
        if ((color_to_type[color] >= 0 && color_to_type[color] != type) ||
            (type_to_color[type] >= 0 && type_to_color[type] != color)) return false;
        color_to_type[color] = type;
        type_to_color[type] = color;
    }
    return true;
}
bool face_matches(int face, const FaceletState &raw, const Variants &variants) {
    return std::any_of(variants[face].begin(), variants[face].end(), [&](const auto &pattern) {
        return partial_match(pattern, raw.colors.data() + face * 9);
    });
}
int corner_sticker(int corner, int face) {
    for (int sticker : corner_facelets[corner]) if (sticker / 9 == face) return sticker;
    return -1;
}
bool continuous_edge(const FaceletState &raw, int edge) {
    const int first = edge_facelets[edge][0], second = edge_facelets[edge][1];
    for (int corner : edge_neighbors[edge]) {
        const bool a = raw.colors[first] == raw.colors[corner_sticker(corner, first / 9)];
        const bool b = raw.colors[second] == raw.colors[corner_sticker(corner, second / 9)];
        if (a != b) return false;
    }
    return true;
}
template <std::size_t N> int parity(const std::array<std::uint8_t, N> &permutation) {
    int result = 0;
    for (std::size_t i = 0; i < N; ++i) for (std::size_t j = i + 1; j < N; ++j)
        result ^= permutation[i] > permutation[j];
    return result;
}

struct Abort { TemplateSearchStop reason; };
struct Search {
    const TemplateSearchRequest &request;
    const Variants variants;
    TemplateSearchReport &report;
    TemplateSolutionCallback on_result;
    TemplateProgressCallback on_progress;
    std::stop_token stop;
    Clock::time_point started = Clock::now(), last_progress = started;
    CubeState cube;
    FaceletState raw;
    std::array<bool, 8> corner_used{};
    std::array<bool, 12> edge_used{};
    int corners = 0, edges = 0, corner_parity = 0;

    Search(const TemplateSearchRequest &r, TemplateSearchReport &out,
           TemplateSolutionCallback result, TemplateProgressCallback progress, std::stop_token token)
        : request(r), variants(prepare(r)), report(out), on_result(std::move(result)),
          on_progress(std::move(progress)), stop(token) {
        raw.colors.fill(-1);
        for (int f = 0; f < 6; ++f) raw.colors[f * 9 + 4] = static_cast<std::int8_t>(f);
    }
    void checkpoint(bool force = false) {
        if (stop.stop_requested()) throw Abort{TemplateSearchStop::Cancelled};
        if (!force && report.nodes % 256 != 0) return;
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started);
        if (request.time_limit.count() && elapsed >= request.time_limit)
            throw Abort{TemplateSearchStop::TimeLimit};
        if (on_progress && (force || now - last_progress >= std::chrono::milliseconds(100))) {
            on_progress({report.nodes, report.results, corners, edges, elapsed});
            last_progress = now;
            if (stop.stop_requested()) throw Abort{TemplateSearchStop::Cancelled};
        }
    }
    void node() {
        checkpoint();
        if (request.node_limit && report.nodes >= request.node_limit)
            throw Abort{TemplateSearchStop::NodeLimit};
        ++report.nodes;
    }
    void emit() {
        checkpoint(true);
        const auto verification = verify_template_result(request, cube);
        if (!verification) throw std::logic_error("Template result verification failed: " + verification.message);
        const auto complete = to_facelets(cube);
        if (complete.colors != raw.colors || from_facelets(complete) != cube)
            throw std::logic_error("Template cubie assignment disagrees with independent facelet decoding.");
        ++report.results;
        if (on_result) on_result(cube);
        checkpoint(true);
        if (request.max_results && report.results >= request.max_results)
            throw Abort{TemplateSearchStop::ResultLimit};
    }
    void set_edges(int position, int flip_sum) {
        edges = position;
        for (int identity = 0; identity < 12; ++identity) if (!edge_used[identity]) {
            edge_used[identity] = true;
            cube.cubies.ep[position] = static_cast<std::uint8_t>(identity);
            for (int orientation = 0; orientation < 2; ++orientation) {
                node();
                cube.cubies.eo[position] = static_cast<std::uint8_t>(orientation);
                for (int n = 0; n < 2; ++n)
                    raw.colors[edge_facelets[position][(n + orientation) % 2]] =
                        static_cast<std::int8_t>(edge_facelets[identity][n] / 9);
                bool matches = true;
                for (int sticker : edge_facelets[position])
                    if (!face_matches(sticker / 9, raw, variants)) { matches = false; break; }
                if (!matches || (request.continuous && !continuous_edge(raw, position))) continue;
                if (position == 11) {
                    if ((flip_sum + orientation) % 2 || parity(cube.cubies.ep) != corner_parity) continue;
                    edges = 12;
                    emit();
                } else set_edges(position + 1, flip_sum + orientation);
                edges = position;
            }
            edge_used[identity] = false;
            for (int sticker : edge_facelets[position]) raw.colors[sticker] = -1;
        }
    }
    void set_corners(int position, int twist_sum) {
        corners = position;
        for (int identity = 0; identity < 8; ++identity) if (!corner_used[identity]) {
            corner_used[identity] = true;
            cube.cubies.cp[position] = static_cast<std::uint8_t>(identity);
            for (int orientation = 0; orientation < 3; ++orientation) {
                node();
                cube.cubies.co[position] = static_cast<std::uint8_t>(orientation);
                for (int n = 0; n < 3; ++n)
                    raw.colors[corner_facelets[position][(n + orientation) % 3]] =
                        static_cast<std::int8_t>(corner_facelets[identity][n] / 9);
                bool matches = true;
                for (int sticker : corner_facelets[position])
                    if (!face_matches(sticker / 9, raw, variants)) { matches = false; break; }
                if (!matches) continue;
                if (position == 7) {
                    if ((twist_sum + orientation) % 3) continue;
                    corners = 8;
                    corner_parity = parity(cube.cubies.cp);
                    set_edges(0, 0);
                } else set_corners(position + 1, twist_sum + orientation);
                corners = position;
            }
            corner_used[identity] = false;
            for (int sticker : corner_facelets[position]) raw.colors[sticker] = -1;
        }
    }
};
} // namespace

TemplateSearchRequest::TemplateSearchRequest() {
    for (auto &face : templates) face.fill(5);
    for (auto &selection : allowed_templates) selection.fill(true);
}

Validation verify_template_result(const TemplateSearchRequest &request, const CubeState &state) {
    if (auto valid = validate_request(request); !valid) return valid;
    if (auto valid = validate(state); !valid) return valid;
    if (state.frame != 0) return {false, "Template results use the fixed URFDLB center frame."};
    try {
        const auto raw = to_facelets(state);
        if (from_facelets(raw) != state) return {false, "Facelet encoding does not decode to the same legal cube."};
        // Complete verification uses pairwise equality, not the incremental
        // bijection matcher or its precomputed template variants.
        for (int face = 0; face < 6; ++face) {
            bool matched = false;
            for (int t = 0; t < 5 && !matched; ++t) {
                if (!request.allowed_templates[face][t] || unused(request.templates[t])) continue;
                for (int reflection = 0; reflection < 2 && !matched; ++reflection)
                    for (int rotation = 0; rotation < 4 && !matched; ++rotation) {
                        std::array<int, 9> label;
                        for (int i = 0; i < 9; ++i) {
                            int r = i / 3, c = i % 3;
                            if (reflection) c = 2 - c;
                            for (int k = 0; k < rotation; ++k) { const int old_r = r; r = c; c = 2 - old_r; }
                            label[i] = request.templates[t][r * 3 + c];
                        }
                        bool equal = true;
                        for (int i = 0; i < 9 && equal; ++i) for (int j = 0; j < i; ++j)
                            if ((raw.colors[face * 9 + i] == raw.colors[face * 9 + j]) !=
                                (label[i] == label[j])) { equal = false; break; }
                        matched = equal;
                    }
            }
            if (!matched) return {false, "A complete face does not match any selected template type partition."};
        }
        if (request.continuous) {
            // Discover adjacent corners from face incidence rather than EN.
            for (const auto &edge : edge_facelets) for (const auto &corner : corner_facelets) {
                int first = -1, second = -1;
                for (int sticker : corner) {
                    if (sticker / 9 == edge[0] / 9) first = sticker;
                    if (sticker / 9 == edge[1] / 9) second = sticker;
                }
                if (first >= 0 && second >= 0 &&
                    ((raw.colors[edge[0]] == raw.colors[first]) != (raw.colors[edge[1]] == raw.colors[second])))
                    return {false, "Pattern is not continuous across a cube edge."};
            }
        }
        return {};
    } catch (const std::exception &error) { return {false, error.what()}; }
}

TemplateSearchReport enumerate_templates(const TemplateSearchRequest &request,
    TemplateSolutionCallback on_result, TemplateProgressCallback on_progress, std::stop_token stop) {
    TemplateSearchReport report;
    if (auto valid = validate_request(request); !valid) {
        report.stop = TemplateSearchStop::InvalidInput;
        report.message = valid.message;
        return report;
    }
    Search search(request, report, std::move(on_result), std::move(on_progress), stop);
    try {
        search.checkpoint(true);
        if (std::all_of(search.variants.begin(), search.variants.end(), [](const auto &face) { return !face.empty(); }))
            search.set_corners(0, 0);
        search.checkpoint(true);
    } catch (const Abort &abort) { report.stop = abort.reason; }
    report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - search.started);
    return report;
}

std::string_view template_search_stop_name(TemplateSearchStop stop) noexcept {
    switch (stop) {
    case TemplateSearchStop::Exhausted: return "exhausted";
    case TemplateSearchStop::Cancelled: return "cancelled";
    case TemplateSearchStop::ResultLimit: return "result_limit";
    case TemplateSearchStop::TimeLimit: return "time_limit";
    case TemplateSearchStop::NodeLimit: return "node_limit";
    case TemplateSearchStop::InvalidInput: return "invalid_input";
    }
    return "unknown";
}
} // namespace cube
