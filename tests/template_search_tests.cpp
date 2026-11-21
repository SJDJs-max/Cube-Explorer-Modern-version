#include "cube/template_search.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>

using namespace cube;
namespace {
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
FaceTemplate normalize(FaceTemplate face) {
    std::array<int, 6> types;
    types.fill(-1);
    int next = 0;
    for (auto &label : face) {
        if (types[label] < 0) types[label] = next++;
        label = static_cast<std::uint8_t>(types[label]);
    }
    return face;
}
FaceTemplate transformed(FaceTemplate face, int rotation, bool reflection) {
    constexpr std::array<int, 9> rotate = {6,3,0,7,4,1,8,5,2};
    constexpr std::array<int, 9> mirror = {2,1,0,5,4,3,8,7,6};
    auto permute = [&](const auto &permutation) {
        auto next = face;
        for (int i = 0; i < 9; ++i) next[i] = face[permutation[i]];
        face = next;
    };
    for (int i = 0; i < rotation; ++i) permute(rotate);
    if (reflection) permute(mirror);
    return face;
}
FaceTemplate key(FaceTemplate face, bool reflections = true) {
    auto result = normalize(face);
    for (int r = 0; r < 4; ++r) for (int m = 0; m < (reflections ? 2 : 1); ++m)
        result = std::min(result, normalize(transformed(face, r, m != 0)));
    return result;
}
bool independent_match(const TemplateSearchRequest &request, const FaceletState &stickers) {
    for (int face = 0; face < 6; ++face) {
        FaceTemplate raw;
        for (int i = 0; i < 9; ++i) raw[i] = static_cast<std::uint8_t>(stickers.colors[face * 9 + i]);
        const auto actual = key(raw);
        bool found = false;
        for (int t = 0; t < 5; ++t) {
            if (!request.allowed_templates[face][t] ||
                std::all_of(request.templates[t].begin(), request.templates[t].end(), [](auto x) { return x == 5; })) continue;
            if (key(request.templates[t]) == actual) found = true;
        }
        if (!found) return false;
    }
    return true;
}
std::vector<std::string> collect(const TemplateSearchRequest &request, TemplateSearchReport *out = nullptr) {
    std::vector<std::string> result;
    std::set<std::string> unique;
    auto report = enumerate_templates(request, [&](const CubeState &state) {
        require(static_cast<bool>(validate(state)), "Emitted cube has illegal orientation or parity");
        const auto facelets = to_facelets(state);
        require(from_facelets(facelets) == state, "Emitted cube does not independently decode");
        require(independent_match(request, facelets), "Emitted cube fails independent normalized-partition matcher");
        require(static_cast<bool>(verify_template_result(request, state)), "Emitted cube fails complete verifier");
        const auto text = format_facelets(facelets);
        for (char color : face_names)
            require(std::count(text.begin(), text.end(), color) == 9, "Emitted facelets duplicate or omit physical colors");
        require(unique.insert(text).second, "Overlapping templates emitted a duplicate state");
        result.push_back(text);
    });
    require(report.results == result.size(), "Report result count differs from real callbacks");
    if (out) *out = report;
    return result;
}
TemplateSearchRequest striped() {
    TemplateSearchRequest request;
    request.templates[0].fill(0);
    request.templates[1] = {1,1,1,0,0,0,0,0,0};
    return request;
}
bool request_for(const CubeState &cube, TemplateSearchRequest &request) {
    const auto stickers = to_facelets(cube);
    std::vector<FaceTemplate> types;
    for (auto &face : request.allowed_templates) face.fill(false);
    for (int f = 0; f < 6; ++f) {
        FaceTemplate face;
        for (int i = 0; i < 9; ++i) face[i] = static_cast<std::uint8_t>(stickers.colors[f * 9 + i]);
        face = key(face);
        auto it = std::find(types.begin(), types.end(), face);
        if (it == types.end()) { types.push_back(face); it = types.end() - 1; }
        if (types.size() > 5) return false;
        const auto index = static_cast<std::size_t>(it - types.begin());
        request.templates[index] = face;
        request.allowed_templates[f][index] = true;
    }
    return true;
}
}

int main() {
    try {
        TemplateSearchReport report;
        TemplateSearchRequest blank;
        require(collect(blank, &report).empty() && report.exhausted(), "All-gray templates must be unused, not wildcards");
        auto uniform = blank;
        uniform.templates[0].fill(0);
        auto solved = collect(uniform, &report);
        require(report.exhausted() && solved == std::vector<std::string>{format_facelets(solved_facelets())},
                "Uniform face type must enumerate exactly solved cube");
        uniform.continuous = true;
        require(collect(uniform) == solved, "Solved pattern must satisfy continuous boundary rule");
        uniform.allowed_templates[0].fill(false);
        require(collect(uniform, &report).empty() && report.exhausted(), "A face with no selected template must have no results");
        uniform.templates[0][4] = 6;
        require(collect(uniform, &report).empty() && report.stop == TemplateSearchStop::InvalidInput,
                "Out-of-range abstract type was accepted");
        uniform = blank;
        uniform.time_limit = std::chrono::milliseconds(-1);
        require(enumerate_templates(uniform).stop == TemplateSearchStop::InvalidInput, "Negative budget accepted");

        auto request = striped();
        auto baseline = collect(request, &report);
        require(report.exhausted() && baseline.size() == 19,
                "Uniform-or-one-strip templates must enumerate solved plus 18 single-face turns");
        std::set<std::string> exact_expected{format_facelets(solved_facelets())};
        for (auto move : all_moves(false, false, false))
            exact_expected.insert(format_facelets(apply_facelets(solved_facelets(), move)));
        require(std::set<std::string>(baseline.begin(), baseline.end()) == exact_expected,
                "Complete template enumeration differs from independent 18-turn physical reference set");
        require(baseline[0] == format_facelets(solved_facelets()) &&
                baseline[1] == format_facelets(apply_facelets(solved_facelets(), {'D',1})),
                "Original corner/edge enumeration ordering changed");

        const std::array<std::uint8_t, 6> rename = {3,0,4,2,1,5};
        auto renamed = request;
        for (auto &face : renamed.templates) for (auto &type : face) type = rename[type];
        require(collect(renamed) == baseline, "Abstract label renaming changed enumerated states");
        auto gray = request;
        for (auto &type : gray.templates[1]) if (type == 1) type = 5;
        require(collect(gray) == baseline, "Gray inside a mixed template must act as a real sixth type");
        auto gray_only = gray;
        gray_only.templates[0].fill(5);
        require(!verify_template_result(gray_only, CubeState{}), "Mixed gray was treated as an unconstrained wildcard");
        auto rotated = request;
        rotated.templates[1] = transformed(rotated.templates[1], 1, false);
        require(collect(rotated) == baseline, "Template 90-degree rotation changed result set/order");
        auto duplicate = request;
        duplicate.templates[2] = duplicate.templates[0];
        duplicate.templates[3] = duplicate.templates[1];
        duplicate.templates[4] = transformed(duplicate.templates[1], 3, true);
        require(collect(duplicate) == baseline, "Overlapping template alternatives changed enumeration");

        auto continuous = request;
        continuous.continuous = true;
        continuous.max_results = 16;
        const auto continuous_results = collect(continuous, &report);
        require(report.stop == TemplateSearchStop::ResultLimit && continuous_results.size() == 16,
                "Continuous must filter boundaries, not stop after the first cube");

        int derived = 0, chiral = 0, discontinuous = 0;
        std::optional<TemplateSearchRequest> asymmetric;
        std::mt19937 random(42017);
        for (int sample = 0; sample < 100; ++sample) {
            CubeState cube;
            for (int n = 0; n < 1 + sample % 7; ++n)
                apply_in_place(cube, {face_names[random() % 6], static_cast<std::uint8_t>(1 + random() % 3)});
            TemplateSearchRequest derived_request;
            if (!request_for(cube, derived_request)) continue;
            ++derived;
            require(static_cast<bool>(verify_template_result(derived_request, cube)), "Legal source-derived template was rejected");
            require(independent_match(derived_request, to_facelets(cube)), "Independent face-type matcher disagrees");
            auto mirror = derived_request;
            const int old_chiral = chiral;
            for (int t = 0; t < 5; ++t) {
                if (key(mirror.templates[t], false) != key(transformed(mirror.templates[t], 0, true), false)) ++chiral;
                mirror.templates[t] = transformed(mirror.templates[t], 0, true);
            }
            require(static_cast<bool>(verify_template_result(mirror, cube)), "Reflection-only face-template variant was rejected");
            if (!asymmetric && chiral > old_chiral) asymmetric = derived_request;
            derived_request.continuous = true;
            if (!verify_template_result(derived_request, cube)) ++discontinuous;
        }
        require(derived > 20 && chiral > 0, "D4 tests did not include genuine reflected asymmetric partitions");
        require(discontinuous > 0, "Continuous tests lacked a cube with discontinuous edge boundaries");
        asymmetric->max_results = 8;
        const auto asymmetric_baseline = collect(*asymmetric, &report);
        require(!asymmetric_baseline.empty(), "Asymmetric legal fixture produced no enumerated states");
        for (auto &face : asymmetric->templates) face = transformed(face, 1, true);
        require(collect(*asymmetric) == asymmetric_baseline,
                "Actual search pruning lost states when a chiral template was reflected");

        for (int kind = 0; kind < 4; ++kind) {
            CubeState invalid;
            if (kind == 0) invalid.cubies.cp[1] = 0;
            if (kind == 1) invalid.cubies.co[0] = 1;
            if (kind == 2) invalid.cubies.eo[0] = 1;
            if (kind == 3) std::swap(invalid.cubies.ep[0], invalid.cubies.ep[1]);
            require(!verify_template_result(request, invalid), "Illegal complete state passed template verification");
        }

        auto limited = request;
        limited.max_results = 3;
        require(collect(limited, &report).size() == 3 && report.stop == TemplateSearchStop::ResultLimit,
                "Result limit was misreported as exhausted");
        limited.max_results = 0;
        limited.node_limit = 17;
        require(enumerate_templates(limited).stop == TemplateSearchStop::NodeLimit, "Node limit was misreported as exhausted");
        auto nodes = enumerate_templates(limited);
        require(nodes.nodes == 17, "Node budget exceeded the configured number of assignments");
        limited.node_limit = 0;
        limited.time_limit = std::chrono::milliseconds(1);
        bool delayed = false;
        report = enumerate_templates(limited, {}, [&](const auto &) {
            if (!delayed) { delayed = true; std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
        });
        require(report.stop == TemplateSearchStop::TimeLimit && !report.exhausted(), "Time limit was misreported as exhausted");
        std::stop_source pre;
        pre.request_stop();
        require(enumerate_templates(request, {}, {}, pre.get_token()).stop == TemplateSearchStop::Cancelled,
                "Pre-cancelled template job ran to completion");
        std::stop_source active;
        report = enumerate_templates(request, [&](const CubeState &cube) {
            require(static_cast<bool>(verify_template_result(request, cube)), "Cancellation callback received invalid state");
            active.request_stop();
        }, {}, active.get_token());
        require(report.stop == TemplateSearchStop::Cancelled && report.results == 1,
                "Cancellation after first callback did not preserve exactly one valid result");
        require(collect(striped(), &report).size() > 1 && report.exhausted(), "Unlimited complete finite search did not exhaust");
        std::cout << "template_search_tests: " << checks << " checks passed; " << derived
                  << " source-derived partitions, " << chiral << " chiral types, " << report.results
                  << " complete striped-template states\n";
    } catch (const std::exception &error) {
        std::cerr << "template_search_tests failed: " << error.what() << '\n';
        return 1;
    }
}
