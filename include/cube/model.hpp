#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cube {

enum class Face : std::uint8_t { U, R, F, D, L, B };
inline constexpr std::string_view face_names = "URFDLB";
inline constexpr std::string_view move_names = "URFDLBurfdlbMESxyz";
inline constexpr std::array<std::array<int, 3>, 8> corner_facelets = {{{8,9,20},{6,18,38},{0,36,47},{2,45,11},{29,26,15},{27,44,24},{33,53,42},{35,17,51}}};
inline constexpr std::array<std::array<int, 2>, 12> edge_facelets = {{{5,10},{7,19},{3,37},{1,46},{32,16},{28,25},{30,43},{34,52},{23,12},{21,41},{50,39},{48,14}}};

struct Move {
    char symbol = 'U';
    std::uint8_t power = 1;
    bool operator==(const Move&) const = default;
    [[nodiscard]] bool is_rotation() const noexcept;
    [[nodiscard]] bool is_wide() const noexcept;
    [[nodiscard]] bool is_slice() const noexcept;
    [[nodiscard]] int turn_cost() const noexcept { return is_rotation() ? 0 : 1; }
    [[nodiscard]] int rotation_count() const noexcept { return is_rotation() ? 1 : 0; }
};

struct CubieState {
    std::array<std::uint8_t, 8> cp{0,1,2,3,4,5,6,7};
    std::array<std::uint8_t, 8> co{};
    std::array<std::uint8_t, 12> ep{0,1,2,3,4,5,6,7,8,9,10,11};
    std::array<std::uint8_t, 12> eo{};
    bool operator==(const CubieState&) const = default;
};

// Cubies and center orientations always use the input's logical URFDLB centers.
// frame is the proper spatial orientation of those centers (identity is 0).
struct CubeState {
    CubieState cubies{};
    std::uint8_t frame = 0;
    std::array<std::uint8_t, 6> center_ori{};
    bool operator==(const CubeState&) const = default;
};

struct MoveTransform {
    CubieState delta{};
    std::uint8_t next_frame = 0;
    std::array<std::uint8_t, 6> center_delta{};
};

struct Validation {
    bool valid = true;
    std::string message;
    explicit operator bool() const noexcept { return valid; }
};

[[nodiscard]] CubieState compose(const CubieState& state, const CubieState& delta) noexcept;
[[nodiscard]] CubieState inverse(const CubieState& state) noexcept;
[[nodiscard]] Move inverse(Move move) noexcept;
[[nodiscard]] std::vector<Move> inverse(std::span<const Move> moves);
[[nodiscard]] const CubieState& outer_move(Face face, int power = 1);
[[nodiscard]] const MoveTransform& move_transform(std::uint8_t frame, Move move);
[[nodiscard]] CubeState apply(const CubeState& state, Move move);
[[nodiscard]] CubeState apply(const CubeState& state, std::span<const Move> moves);
void apply_in_place(CubeState& state, Move move);
[[nodiscard]] bool is_solved(const CubieState& state) noexcept;
[[nodiscard]] bool is_solved(const CubeState& state, bool oriented_centers = false) noexcept;
// 0..3 gives the residual logical U power, -1 means not in the target set.
[[nodiscard]] int solved_mod_u(const CubeState& state, bool oriented_centers = false) noexcept;
[[nodiscard]] Validation validate(const CubieState& state);
[[nodiscard]] Validation validate(const CubeState& state);
[[nodiscard]] std::vector<Move> parse_moves(std::string_view text);
[[nodiscard]] std::string format_move(Move move);
[[nodiscard]] std::string format_moves(std::span<const Move> moves, bool bracket_initial_u = false);
[[nodiscard]] int turn_count(std::span<const Move> moves) noexcept;
[[nodiscard]] int rotation_count(std::span<const Move> moves) noexcept;
[[nodiscard]] int move_axis(Move move); // X=0,Y=1,Z=2; physical notation axis.
[[nodiscard]] int move_index(Move move); // index into move_names * 3 + power - 1.
[[nodiscard]] Move move_from_index(int index);
[[nodiscard]] std::vector<Move> all_moves(bool slices = true, bool wide = true, bool rotations = true);
[[nodiscard]] Face logical_face(std::uint8_t frame, Face physical_face);
[[nodiscard]] Face physical_face(std::uint8_t frame, Face logical_face);
[[nodiscard]] std::uint8_t rotated_frame(std::uint8_t frame, Move rotation);

// Independent physical simulator: geometry, not the cubie transition tables.
// Facelets are URFDLB face-major, each viewed from outside, rows top to bottom.
// Values 0..5 are colors; -1 represents an unspecified facelet for diagnostics.
struct FaceletState {
    std::array<std::int8_t, 54> colors{};
    std::array<std::uint8_t, 6> center_ori{}; // directions relative to each face's top edge.
    bool operator==(const FaceletState&) const = default;
};
[[nodiscard]] FaceletState solved_facelets();
[[nodiscard]] FaceletState to_facelets(const CubeState& state);
[[nodiscard]] CubeState from_facelets(const FaceletState& state);
[[nodiscard]] FaceletState parse_facelets(std::string_view text);
[[nodiscard]] std::string format_facelets(const FaceletState& state);
[[nodiscard]] FaceletState apply_facelets(const FaceletState& state, Move move);
[[nodiscard]] FaceletState apply_facelets(const FaceletState& state, std::span<const Move> moves);
[[nodiscard]] Validation verify_sequence(const CubeState& start, std::span<const Move> moves, const CubeState& expected);

} // namespace cube
