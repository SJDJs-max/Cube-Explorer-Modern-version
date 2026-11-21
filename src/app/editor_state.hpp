#pragma once

#include "cube/model.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cube::editor {

enum class Pieces { All, Corners, Edges };
enum class Reset { Empty, Clean, Random };
enum class Correction { CornerTwist, EdgeFlip, PermutationParity };
struct CheckResult {
    bool accepted = true; // Original Check return: any correction returns false.
    bool changed = false;
    std::vector<Correction> corrections;
};

// A raw editing buffer may temporarily contain impossible/half-painted cubies.
// URFDLB = colors, lowercase = orientation ignored, @ = orientation only, ? = free.
// submission=false mirrors FaceletCube.Check(facelet,false): preserve new paint.
// submission=true mirrors Check(facelet,true): clear the offending current paint.
[[nodiscard]] CheckResult check_facelet(std::string& raw54, int facelet, bool submission = false);
[[nodiscard]] CheckResult check_all(std::string& raw54);
void reset(std::string& raw54, std::array<std::int8_t,6>& center_ori,
           Reset kind, Pieces pieces = Pieces::All, std::uint64_t seed = 0);
// Permute raw stickers without requiring a complete or valid cubie state.
void apply_moves_raw(std::string& raw54, std::array<std::int8_t,6>& center_ori,
                     std::span<const Move> moves);

// Legacy UI notation is expanded before entering the modern move parser.
// Rs = R L', Ra = R L; these are two actual outer moves, not one fake metric step.
[[nodiscard]] std::vector<Move> parse_legacy_moves(std::string_view text);
inline constexpr std::array<int,48> legacy_facelet_order={
    7,19,5,10,1,46,3,37,28,25,32,16,34,52,30,43,23,12,21,41,48,14,50,39,
    8,20,9,2,11,45,0,47,36,6,38,18,29,15,26,27,24,44,33,42,53,35,51,17};
// Returns nullopt when the input does not identify a 48-sticker Singmaster line.
// N/n/?/. are unknown; colors are case-insensitive, as in the original importer.
[[nodiscard]] std::optional<std::string> parse_legacy_facelets(std::string_view text);

} // namespace cube::editor
