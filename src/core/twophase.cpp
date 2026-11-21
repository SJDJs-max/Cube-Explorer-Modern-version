// Modern C++ migration of Herbert Kociemba's Cube Explorer (GPL-3.0).
// Source correspondence: CubeDefs.pas, CubiCube.pas, Symmetries.pas,
// CordCube.pas and Search.pas. See docs/twophase-migration.md.
#include "cube/twophase.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cube {
namespace {

constexpr int twist_count = 2187;
constexpr int flip_count = 2048;
constexpr int slice_count = 495;
constexpr int permutation_count = 40320;
constexpr int flip_slice_count = 64430;
constexpr int corner_class_count = 2768;
constexpr int move_count = 18;
constexpr std::array<int, 10> phase2_moves{0,1,2,4,7,9,10,11,13,16};

// The original coordinate is not lexicographic Lehmer ranking. Preserve the
// exact mixed-radix order so the original binary PDBs remain compatible.
template<std::size_t N>
int permutation_coordinate(const std::array<std::uint8_t, N>& p,
                           int count = static_cast<int>(N)) noexcept {
    int x = 0;
    for (int i = count - 1; i > 0; --i) {
        int s = 0;
        for (int j = i - 1; j >= 0; --j) s += p[j] > p[i];
        x = (x + s) * i;
    }
    return x;
}

template<std::size_t N>
void set_permutation(std::array<std::uint8_t, N>& p, int coordinate,
                     int count = static_cast<int>(N)) noexcept {
    std::array<int, N> order{};
    std::array<bool, N> used{};
    for (int i = 0; i < count; ++i) {
        order[i] = coordinate % (i + 1);
        coordinate /= i + 1;
    }
    for (int i = count - 1; i >= 0; --i) {
        int k = count - 1;
        while (used[k]) --k;
        while (order[i]-- > 0) do { --k; } while (used[k]);
        p[i] = static_cast<std::uint8_t>(k);
        used[k] = true;
    }
}

int twist_coordinate(const CubieState& c) noexcept {
    int x = 0;
    for (int i = 0; i < 7; ++i) x = 3 * x + c.co[i];
    return x;
}

void set_twist(CubieState& c, int coordinate) noexcept {
    int sum = 0;
    for (int i = 6; i >= 0; --i) {
        c.co[i] = static_cast<std::uint8_t>(coordinate % 3);
        sum += c.co[i];
        coordinate /= 3;
    }
    c.co[7] = static_cast<std::uint8_t>((3 - sum % 3) % 3);
}

int flip_coordinate(const CubieState& c) noexcept {
    int x = 0;
    for (int i = 0; i < 11; ++i) x = 2 * x + c.eo[i];
    return x;
}

void set_flip(CubieState& c, int coordinate) noexcept {
    int sum = 0;
    for (int i = 10; i >= 0; --i) {
        c.eo[i] = static_cast<std::uint8_t>(coordinate & 1);
        sum += c.eo[i];
        coordinate >>= 1;
    }
    c.eo[11] = static_cast<std::uint8_t>(sum & 1);
}

constexpr int choose(int n, int k) noexcept {
    if (k < 0 || n < k) return 0;
    int result = 1;
    for (int i = 1; i <= k; ++i) result = result * (n - i + 1) / i;
    return result;
}

int slice_coordinate(const CubieState& c) noexcept {
    int sum = 0, k = 3;
    for (int n = 11; k >= 0; --n) {
        if (c.ep[n] >= 8) --k;
        else sum += choose(n, k);
    }
    return sum;
}

void set_slice(CubieState& c, int coordinate) noexcept {
    std::array<bool, 12> occupied{};
    for (int n = 11, k = 3; k >= 0; --n) {
        const int v = choose(n, k);
        if (coordinate < v) { --k; occupied[n] = true; }
        else coordinate -= v;
    }
    int slice_edge = 8;
    for (int i = 0; i < 12; ++i) {
        if (!occupied[i]) continue;
        const auto at = std::find(c.ep.begin(), c.ep.end(), slice_edge);
        std::iter_swap(c.ep.begin() + i, at);
        ++slice_edge;
    }
}

int slice_permutation(const CubieState& c) noexcept {
    std::array<std::uint8_t, 4> p{};
    for (int i = 0; i < 4; ++i) p[i] = c.ep[i + 8] - 8;
    return permutation_coordinate(p);
}

// Symmetries include reflections, represented by corner orientations 3..5.
// This multiplication is intentionally separate from physical compose(),
// whose contract only permits corner orientations 0..2.
CubieState symmetry_multiply(const CubieState& a, const CubieState& b) noexcept {
    CubieState out;
    for (int i = 0; i < 8; ++i) {
        out.cp[i] = a.cp[b.cp[i]];
        const int x = a.co[b.cp[i]], y = b.co[i];
        int o;
        if (x < 3 && y < 3) o = (x + y) % 3;
        else if (x < 3) o = 3 + (x + y) % 3;
        else if (y < 3) o = 3 + (x - y + 3) % 3;
        else o = (x - y + 3) % 3;
        out.co[i] = static_cast<std::uint8_t>(o);
    }
    for (int i = 0; i < 12; ++i) {
        out.ep[i] = a.ep[b.ep[i]];
        out.eo[i] = static_cast<std::uint8_t>((a.eo[b.ep[i]] + b.eo[i]) & 1);
    }
    return out;
}

// The first sixteen symmetries have exactly the original ordering:
// F2 (two choices), U4 (four choices), LR reflection (two choices).
std::array<CubieState, 3> symmetry_generators() {
    std::array<CubieState, 3> g;
    g[0].cp = {5,4,7,6,1,0,3,2};
    g[0].ep = {6,5,4,7,2,1,0,3,9,8,11,10};
    g[1].cp = {3,0,1,2,7,4,5,6};
    g[1].ep = {3,0,1,2,7,4,5,6,11,8,9,10};
    g[1].eo = {0,0,0,0,0,0,0,0,1,1,1,1};
    g[2].cp = {1,0,3,2,5,4,7,6};
    g[2].co.fill(3);
    g[2].ep = {2,1,0,3,6,5,4,7,9,8,11,10};
    return g;
}

template<class T>
std::vector<T> read_binary(const std::filesystem::path& path, std::size_t count,
                          std::uint64_t expected_checksum, std::stop_token cancellation) {
    static_assert(std::endian::native == std::endian::little,
                  "Original Delphi cache files are little endian.");
    if (cancellation.stop_requested()) throw std::runtime_error("Two-phase table preparation cancelled");
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open two-phase table: " + path.string());
    const auto expected = static_cast<std::streamoff>(count * sizeof(T));
    if (stream.tellg() != expected)
        throw std::runtime_error("Wrong two-phase table size: " + path.string());
    std::vector<T> data(count);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(data.data()), expected);
    if (!stream) throw std::runtime_error("Cannot read two-phase table: " + path.string());
    // FNV-1a is a corruption/version check, not a cryptographic signature.
    // These fingerprints identify the original bundled HTM artifacts.
    std::uint64_t checksum = 14695981039346656037ull;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    for (std::size_t i = 0; i < count * sizeof(T); ++i) {
        if ((i & 65535u) == 0 && cancellation.stop_requested())
            throw std::runtime_error("Two-phase table preparation cancelled");
        checksum = (checksum ^ bytes[i]) * 1099511628211ull;
    }
    if (checksum != expected_checksum)
        throw std::runtime_error("Two-phase table checksum mismatch: " + path.string());
    return data;
}

bool allowed_successor(int previous_face, int face) noexcept {
    return face != previous_face && !(face < 3 && previous_face == face + 3);
}

constexpr auto distance_update = [] {
    std::array<std::array<std::int8_t, 3>, 32> table{};
    for (int previous = 0; previous < 32; ++previous)
        for (int residue = 0; residue < 3; ++residue) {
            const int delta = (residue - previous % 3 + 3) % 3;
            table[previous][residue] = static_cast<std::int8_t>(previous + (delta == 2 ? -1 : delta));
        }
    return table;
}();

int next_distance(int previous, int residue) noexcept {
    // Original Search.pas GetPruningLength table; the search does not need
    // repeated modulo arithmetic at every visited coordinate.
    assert(previous >= 0 && previous < 32 && residue >= 0 && residue < 3);
    return distance_update[previous][residue];
}

struct SearchAxes {
    std::array<CubieState, 3> symmetry, inverse_symmetry;
    std::array<std::array<int, move_count>, 3> original_move{};
    SearchAxes() {
        // TripSearch.pas uses S_URF3 twice to cycle UD, RL and FB subgroups.
        auto& s = symmetry[1];
        s.cp = {0,4,5,1,3,7,6,2}; s.co = {1,2,1,2,2,1,2,1};
        s.ep = {1,8,5,9,3,11,7,10,0,4,6,2}; s.eo = {1,0,1,0,1,0,1,0,1,1,1,1};
        symmetry[2] = compose(s, s);
        if (compose(symmetry[2], s) != CubieState{})
            throw std::logic_error("Triple-search symmetry does not have order three");
        for (int axis = 0; axis < 3; ++axis) {
            inverse_symmetry[axis] = inverse(symmetry[axis]);
            for (int original = 0; original < move_count; ++original) {
                const auto moved = conjugate(outer_move(static_cast<Face>(original / 3), original % 3 + 1), axis);
                int found = -1;
                for (int transformed = 0; transformed < move_count; ++transformed)
                    if (outer_move(static_cast<Face>(transformed / 3), transformed % 3 + 1) == moved) {
                        found = transformed;
                        break;
                    }
                if (found < 0) throw std::logic_error("Triple-search symmetry changes the face move set");
                original_move[axis][found] = original;
            }
        }
    }
    CubieState conjugate(const CubieState& state, int axis) const {
        return compose(compose(symmetry[axis], state), inverse_symmetry[axis]);
    }
};

const SearchAxes& search_axes() {
    static const SearchAxes axes;
    return axes;
}

} // namespace

struct TwoPhaseSolver::Tables {
    std::vector<std::uint32_t> raw_representatives;
    std::vector<std::uint32_t> flip_slice_move; // original 27 columns
    std::vector<std::uint8_t> phase1_packed, phase2_packed;
    std::vector<std::uint32_t> raw_to_symmetric;
    std::vector<std::array<std::uint16_t, move_count>> twist_move, corner_move, edge8_move;
    std::array<std::array<std::uint8_t, move_count>, 24> slice_move{};
    std::vector<std::array<std::uint16_t, 16>> twist_conjugate, edge8_conjugate;
    std::array<CubieState, 16> symmetries{};
    std::array<std::uint8_t, 16> inverse_symmetry{};
    std::array<std::array<std::uint8_t, 16>, 16> symmetry_product{};
    std::array<std::array<std::uint8_t, move_count>, 16> symmetry_move{};
    std::array<std::array<std::uint8_t, 5>, 243> unpack{};
    std::array<std::uint16_t, permutation_count> corner_to_symmetric{};
    std::array<std::uint8_t, permutation_count> corner_phase2_distance{};
    std::array<CubieState, move_count> physical_moves{};
    std::stop_token preparation_stop;

    explicit Tables(const std::filesystem::path& path, const Progress& report, std::stop_token stop)
        : preparation_stop(stop) {
        preparation_checkpoint();
        if (report) report("Loading original HTM pruning tables");
        raw_representatives = read_binary<std::uint32_t>(path / "RawFlipSlice", flip_slice_count,
            0xbc88142b1266c092ull, stop);
        flip_slice_move = read_binary<std::uint32_t>(path / "flipUDslice.move", flip_slice_count * 27u,
            0x0b91ad9f0de56766ull, stop);
        phase1_packed = read_binary<std::uint8_t>(path / "phase1PF.prun", flip_slice_count * twist_count / 5u,
            0x4267d37a7911276full, stop);
        phase2_packed = read_binary<std::uint8_t>(path / "phase2PF.prun", corner_class_count * permutation_count / 5u,
            0x17aa5de4fdc1ff87ull, stop);
        if (raw_representatives.front() != 0 || raw_representatives.back() != 919506 ||
            !std::is_sorted(raw_representatives.begin(), raw_representatives.end()))
            throw std::runtime_error("Incompatible RawFlipSlice representatives");
        for (const auto x : flip_slice_move)
            if ((x >> 4) >= flip_slice_count) throw std::runtime_error("Invalid flipUDslice.move entry");
        for (const auto* data : {&phase1_packed, &phase2_packed})
            if (std::any_of(data->begin(), data->end(), [](auto x) { return x >= 243; }))
                throw std::runtime_error("Invalid packed pruning-table byte");
        for (int b = 0; b < 243; ++b)
            for (int d = 0, value = b; d < 5; ++d, value /= 3)
                unpack[b][d] = static_cast<std::uint8_t>(value % 3);
        for (int m = 0; m < move_count; ++m)
            physical_moves[m] = outer_move(static_cast<Face>(m / 3), m % 3 + 1);
        if (report) report("Building original symmetry and coordinate tables");
        build_symmetries();
        build_coordinate_tables();
        build_symmetry_coordinates();
        build_corner_distances();
        verify_tables();
        preparation_checkpoint();
        preparation_stop = {};
        if (report) report("Two-phase tables ready");
    }

    void preparation_checkpoint() const {
        if (preparation_stop.stop_requested())
            throw std::runtime_error("Two-phase table preparation cancelled");
    }

    CubieState conjugate(const CubieState& c, int s) const noexcept {
        return symmetry_multiply(symmetry_multiply(symmetries[s], c), symmetries[inverse_symmetry[s]]);
    }

    void build_symmetries() {
        const auto g = symmetry_generators();
        CubieState c;
        int index = 0;
        for (int f = 0; f < 2; ++f) {
            for (int u = 0; u < 4; ++u) {
                for (int r = 0; r < 2; ++r) {
                    symmetries[index++] = c;
                    c = symmetry_multiply(c, g[2]);
                }
                c = symmetry_multiply(c, g[1]);
            }
            c = symmetry_multiply(c, g[0]);
        }
        if (c != CubieState{}) throw std::logic_error("Symmetry generators do not close");
        for (int a = 0; a < 16; ++a) {
            for (int b = 0; b < 16; ++b) {
                const auto product = symmetry_multiply(symmetries[a], symmetries[b]);
                const auto found = std::find(symmetries.begin(), symmetries.end(), product);
                if (found == symmetries.end()) throw std::logic_error("Symmetry group is not closed");
                symmetry_product[a][b] = static_cast<std::uint8_t>(found - symmetries.begin());
                if (product == CubieState{}) inverse_symmetry[a] = static_cast<std::uint8_t>(b);
            }
        }
        for (int s = 0; s < 16; ++s) {
            for (int m = 0; m < move_count; ++m) {
                const auto transformed = conjugate(physical_moves[m], s);
                const auto found = std::find(physical_moves.begin(), physical_moves.end(), transformed);
                if (found == physical_moves.end()) throw std::logic_error("Symmetry does not preserve move set");
                symmetry_move[s][m] = static_cast<std::uint8_t>(found - physical_moves.begin());
            }
        }
    }

    void build_coordinate_tables() {
        twist_move.resize(twist_count);
        corner_move.resize(permutation_count);
        edge8_move.resize(permutation_count);
        twist_conjugate.resize(twist_count);
        edge8_conjugate.resize(permutation_count);
        for (int i = 0; i < twist_count; ++i) {
            if ((i & 255) == 0) preparation_checkpoint();
            CubieState c;
            set_twist(c, i);
            for (int m = 0; m < move_count; ++m)
                twist_move[i][m] = static_cast<std::uint16_t>(twist_coordinate(compose(c, physical_moves[m])));
            for (int s = 0; s < 16; ++s)
                twist_conjugate[i][s] = static_cast<std::uint16_t>(twist_coordinate(conjugate(c, s)));
        }
        for (int i = 0; i < permutation_count; ++i) {
            if ((i & 255) == 0) preparation_checkpoint();
            CubieState corners, edges;
            set_permutation(corners.cp, i);
            set_permutation(edges.ep, i, 8);
            for (int m = 0; m < move_count; ++m)
                corner_move[i][m] = static_cast<std::uint16_t>(permutation_coordinate(compose(corners, physical_moves[m]).cp));
            for (const int m : phase2_moves)
                edge8_move[i][m] = static_cast<std::uint16_t>(permutation_coordinate(compose(edges, physical_moves[m]).ep, 8));
            for (int s = 0; s < 16; ++s)
                edge8_conjugate[i][s] = static_cast<std::uint16_t>(permutation_coordinate(conjugate(edges, s).ep, 8));
        }
        for (int i = 0; i < 24; ++i) {
            CubieState c;
            std::array<std::uint8_t, 4> p{};
            set_permutation(p, i);
            for (int e = 0; e < 4; ++e) c.ep[e + 8] = p[e] + 8;
            for (const int m : phase2_moves)
                slice_move[i][m] = static_cast<std::uint8_t>(slice_permutation(compose(c, physical_moves[m])));
        }
    }

    void build_symmetry_coordinates() {
        constexpr auto unset = std::numeric_limits<std::uint32_t>::max();
        raw_to_symmetric.assign(slice_count * flip_count, unset);
        for (int i = 0; i < flip_slice_count; ++i) {
            if ((i & 255) == 0) preparation_checkpoint();
            CubieState c;
            const int raw = static_cast<int>(raw_representatives[i]);
            set_slice(c, raw / flip_count);
            set_flip(c, raw % flip_count);
            for (int s = 0; s < 16; ++s) {
                const auto transformed = conjugate(c, inverse_symmetry[s]);
                const auto target = slice_coordinate(transformed) * flip_count + flip_coordinate(transformed);
                if (raw_to_symmetric[target] == unset)
                    raw_to_symmetric[target] = (static_cast<std::uint32_t>(i) << 4) | s;
            }
        }
        if (std::find(raw_to_symmetric.begin(), raw_to_symmetric.end(), unset) != raw_to_symmetric.end())
            throw std::logic_error("Flip/slice symmetry map does not cover all states");

        corner_to_symmetric.fill(std::numeric_limits<std::uint16_t>::max());
        int class_index = 0;
        for (int i = 0; i < permutation_count; ++i) {
            if ((i & 255) == 0) preparation_checkpoint();
            if (corner_to_symmetric[i] != std::numeric_limits<std::uint16_t>::max()) continue;
            CubieState c;
            set_permutation(c.cp, i);
            for (int s = 0; s < 16; ++s) {
                const auto target = permutation_coordinate(conjugate(c, inverse_symmetry[s]).cp);
                if (corner_to_symmetric[target] == std::numeric_limits<std::uint16_t>::max())
                    corner_to_symmetric[target] = static_cast<std::uint16_t>((class_index << 4) | s);
            }
            ++class_index;
        }
        if (class_index != corner_class_count) throw std::logic_error("Wrong corner symmetry class count");
    }

    void build_corner_distances() {
        corner_phase2_distance.fill(255);
        corner_phase2_distance[0] = 0;
        std::vector<int> queue;
        queue.reserve(permutation_count);
        queue.push_back(0);
        for (std::size_t head = 0; head < queue.size(); ++head) {
            if ((head & 255u) == 0) preparation_checkpoint();
            const int c = queue[head];
            for (const int m : phase2_moves) {
                const int child = corner_move[c][m];
                if (corner_phase2_distance[child] != 255) continue;
                corner_phase2_distance[child] = corner_phase2_distance[c] + 1;
                queue.push_back(child);
            }
        }
        if (queue.size() != permutation_count) throw std::logic_error("Corner phase-two table is disconnected");
    }

    std::uint8_t packed_value(const std::vector<std::uint8_t>& data,
                              std::uint32_t index) const noexcept {
        const auto split = static_cast<std::uint32_t>(data.size() * 4);
        return index < split ? unpack[data[index >> 2]][index & 3]
                             : unpack[data[index - split]][4];
    }

    std::uint32_t symmetric_coordinate(const CubieState& c) const noexcept {
        return raw_to_symmetric[slice_coordinate(c) * flip_count + flip_coordinate(c)];
    }

    std::uint32_t next_flip_slice(std::uint32_t packed, int move) const noexcept {
        const int symmetry = packed & 15;
        const auto next = flip_slice_move[(packed >> 4) * 27 + symmetry_move[symmetry][move]];
        return (next & ~15u) | symmetry_product[next & 15][symmetry];
    }

    int phase1_residue(std::uint32_t packed, int twist) const noexcept {
        return packed_value(phase1_packed,
            twist_count * (packed >> 4) + twist_conjugate[twist][packed & 15]);
    }

    int phase2_residue(int corners, int edges) const noexcept {
        const auto sym = corner_to_symmetric[corners];
        return packed_value(phase2_packed,
            corner_class_count * edge8_conjugate[edges][sym & 15] + (sym >> 4));
    }

    int phase1_distance(std::uint32_t packed, int twist) const {
        int distance = 0;
        // Modulo-three distances determine a descending edge uniquely up to
        // ties. Traversal is only for the root; children update in O(1).
        while ((packed >> 4) != 0 || twist != 0) {
            const int residue = phase1_residue(packed, twist);
            const int target = (residue + 2) % 3;
            bool descended = false;
            for (int m = 0; m < move_count; ++m) {
                const auto child = next_flip_slice(packed, m);
                const int child_twist = twist_move[twist][m];
                if (phase1_residue(child, child_twist) == target) {
                    packed = child;
                    twist = child_twist;
                    descended = true;
                    break;
                }
            }
            if (!descended || ++distance > 12)
                throw std::runtime_error("Incompatible or damaged phase-one pruning table");
        }
        return distance;
    }

    // The exact projected distance is needed only when it fits the current
    // phase-two budget. Otherwise any proved value above that budget suffices.
    int phase2_distance(int corners, int edges, int maximum = 18) const {
        int distance = 0;
        int residue = phase2_residue(corners, edges);
        while (corners != 0 || edges != 0) {
            // This corner-only distance is already available from the original
            // precheck. Round it up to the next value compatible with the full
            // projection's mod-three residue. Each accepted descent decreases
            // that projection's exact distance by one, so this remains a lower
            // bound on the initial distance throughout the walk.
            // The projection's diameter is 18. A looser bound, typical before
            // the first candidate, needs no additional corner-bound lookups.
            if (maximum < 18) {
                const int corner_bound = corner_phase2_distance[corners];
                const int lower_bound = corner_bound + (residue - corner_bound % 3 + 3) % 3;
                if (distance + lower_bound > maximum) return maximum + 1;
            }
            const int target = (residue + 2) % 3;
            bool descended = false;
            for (const int m : phase2_moves) {
                const int c = corner_move[corners][m], e = edge8_move[edges][m];
                if (phase2_residue(c, e) == target) {
                    corners = c;
                    edges = e;
                    residue = target;
                    descended = true;
                    break;
                }
            }
            if (!descended || ++distance > 18)
                throw std::runtime_error("Incompatible or damaged phase-two pruning table");
        }
        return distance;
    }

    void verify_tables() const {
        if (phase1_residue(0, 0) != 0 || phase2_residue(0, 0) != 0)
            throw std::runtime_error("Pruning-table origin is not zero");
        for (int m = 0; m < move_count; ++m) {
            const auto& c = physical_moves[m];
            const auto raw = symmetric_coordinate(c);
            const auto moved = next_flip_slice(0, m);
            // Stabilizers may provide distinct equivalent symmetry indices.
            if ((raw >> 4) != (moved >> 4) ||
                phase1_residue(raw, twist_coordinate(c)) != phase1_residue(moved, twist_coordinate(c)))
                throw std::runtime_error("Original move table does not match cubie conventions");
            const int expected = (m / 3 == 0 || m / 3 == 3 || m % 3 == 1) ? 0 : 1;
            if (phase1_distance(raw, twist_coordinate(c)) != expected)
                throw std::runtime_error("One-move phase-one distance is inconsistent");
        }
        for (const int m : phase2_moves) {
            const auto& c = physical_moves[m];
            if (phase2_distance(permutation_coordinate(c.cp), permutation_coordinate(c.ep, 8)) != 1)
                throw std::runtime_error("One-move phase-two distance is inconsistent");
        }
    }

    std::uint64_t resident_bytes() const noexcept {
        return raw_representatives.size() * sizeof(std::uint32_t) +
            flip_slice_move.size() * sizeof(std::uint32_t) + phase1_packed.size() + phase2_packed.size() +
            raw_to_symmetric.size() * sizeof(std::uint32_t) +
            (twist_move.size() + corner_move.size() + edge8_move.size()) * sizeof(twist_move[0]) +
            (twist_conjugate.size() + edge8_conjugate.size()) * sizeof(twist_conjugate[0]) +
            sizeof(*this);
    }

    struct SharedSearch {
        const CubeState& input;
        const TwoPhaseOptions& options;
        std::stop_token cancellation;
        OnSolution callback;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::atomic<int> best_length;
        std::atomic<std::uint64_t> reserved_nodes{0};
        std::atomic<TwoPhaseStop> stop_reason{TwoPhaseStop::depth_limit};
        std::mutex mutex;
        TwoPhaseResult result;
        std::exception_ptr failure;

        SharedSearch(const CubeState& c, const TwoPhaseOptions& o, std::stop_token stop, OnSolution cb)
            : input(c), options(o), cancellation(stop), callback(std::move(cb)), best_length(o.max_moves + 1) {
            result.search_axes = 3;
        }
        void stop(TwoPhaseStop reason) {
            auto expected = TwoPhaseStop::depth_limit;
            stop_reason.compare_exchange_strong(expected, reason, std::memory_order_relaxed);
        }
        bool interrupted(bool check_time) {
            if (stop_reason.load(std::memory_order_relaxed) != TwoPhaseStop::depth_limit) return true;
            if (cancellation.stop_requested()) stop(TwoPhaseStop::cancelled);
            else if (check_time && options.time_limit.count() > 0 &&
                     std::chrono::steady_clock::now() - started >= options.time_limit)
                stop(TwoPhaseStop::time_limit);
            return stop_reason.load(std::memory_order_relaxed) != TwoPhaseStop::depth_limit;
        }
        bool reserve_node() {
            // Unlimited searches keep per-axis counters, avoiding a contended
            // atomic increment on every node. A finite budget is shared exactly.
            if (!options.node_limit) return true;
            auto before = reserved_nodes.load(std::memory_order_relaxed);
            while (before < options.node_limit) {
                if (reserved_nodes.compare_exchange_weak(before, before + 1, std::memory_order_relaxed))
                    return true;
            }
            stop(TwoPhaseStop::node_limit);
            return false;
        }
        void accept(TwoPhaseSolution candidate, int axis) {
            if (candidate.turn_count >= best_length.load(std::memory_order_relaxed)) return;
            for (auto& move : candidate.moves) {
                const auto transformed = static_cast<int>(face_names.find(move.symbol)) * 3 + move.power - 1;
                const int original = search_axes().original_move[axis][transformed];
                move.symbol = face_names[static_cast<int>(physical_face(input.frame, static_cast<Face>(original / 3)))];
                move.power = static_cast<std::uint8_t>(original % 3 + 1);
            }
            candidate.search_axis = axis;
            candidate.final_state = cube::apply(input, candidate.moves);
            if (!is_solved(candidate.final_state) || !verify_sequence(input, candidate.moves, candidate.final_state))
                throw std::logic_error("Triple-search result failed independent physical replay");
            std::lock_guard lock(mutex);
            if (interrupted(false)) return;
            if (candidate.turn_count >= best_length.load(std::memory_order_relaxed)) return;
            best_length.store(candidate.turn_count, std::memory_order_relaxed);
            result.solved = true;
            result.solution = candidate;
            if (callback) callback(candidate); // preserve the serial callback contract
            if (candidate.turn_count <= options.target_moves) stop(TwoPhaseStop::target_reached);
            else if (cancellation.stop_requested()) stop(TwoPhaseStop::cancelled);
        }
    };

    template<bool Triple = false> struct Search {
        const Tables& t;
        const CubeState& input;
        TwoPhaseOptions options;
        std::stop_token cancellation;
        OnSolution callback;
        TwoPhaseResult result;
        std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::array<int, 64> path{};
        std::array<int, 64> corner_prefix{};
        std::array<CubieState, 64> cubie_prefix{};
        int corner_valid = 0, cubie_valid = 0;
        int best_length;
        bool interrupted = false;
        SharedSearch* shared = nullptr;
        int axis = 0;

        Search(const Tables& tables, const CubeState& cube, const TwoPhaseOptions& o,
               std::stop_token token, OnSolution cb, SharedSearch* common = nullptr, int search_axis = 0)
            : t(tables), input(cube), options(o), cancellation(token),
              callback(std::move(cb)), best_length(o.max_moves + 1), shared(common), axis(search_axis) {
            corner_prefix[0] = permutation_coordinate(input.cubies.cp);
            cubie_prefix[0] = input.cubies;
        }

        bool check_limits(bool force = false) {
            if (interrupted) return true;
            if constexpr (Triple) {
                if (shared->interrupted(force || (result.nodes & 1023u) == 0)) {
                    result.stop_reason = shared->stop_reason.load(std::memory_order_relaxed);
                    return interrupted = true;
                }
                return false;
            }
            if (cancellation.stop_requested()) {
                result.stop_reason = TwoPhaseStop::cancelled;
                return interrupted = true;
            }
            if (options.node_limit && result.nodes >= options.node_limit) {
                result.stop_reason = TwoPhaseStop::node_limit;
                return interrupted = true;
            }
            if ((force || (result.nodes & 1023u) == 0) && options.time_limit.count() > 0 &&
                std::chrono::steady_clock::now() - started >= options.time_limit) {
                result.stop_reason = TwoPhaseStop::time_limit;
                return interrupted = true;
            }
            return false;
        }
        bool visit_node() {
            if (check_limits()) return false;
            if constexpr (Triple) {
                if (!shared->reserve_node()) {
                    result.stop_reason = shared->stop_reason.load(std::memory_order_relaxed);
                    interrupted = true;
                    return false;
                }
            }
            ++result.nodes;
            return true;
        }
        int length_bound() const {
            if constexpr (Triple)
                return std::min(best_length, shared->best_length.load(std::memory_order_relaxed));
            return best_length;
        }

        bool phase2(int corners, int edges, int slice, int h,
                    int remaining, int position, int previous_face) {
            if (h > remaining) return false;
            if constexpr (Triple)
                if (position + remaining >= length_bound()) return false;
            if (remaining == 0) return corners == 0 && edges == 0 && slice == 0;
            for (int face = 0; face < 6; ++face) {
                if (!allowed_successor(previous_face, face)) continue;
                const bool ud = face == 0 || face == 3;
                for (int power = ud ? 0 : 1; power < 3; power += ud ? 1 : 3) {
                    if (!visit_node()) return false;
                    const int m = face * 3 + power;
                    const int c = t.corner_move[corners][m], e = t.edge8_move[edges][m];
                    const int child_h = next_distance(h, t.phase2_residue(c, e));
                    // Search.pas:1163 applies the same HTM face-power bound in
                    // phase two. The other U/D powers differ by one allowed
                    // move, so none fits if this child is over by two or more.
                    if (child_h > remaining) break;
                    if (child_h > remaining - 1) continue;
                    path[position] = m;
                    if (phase2(c, e, t.slice_move[slice][m], child_h,
                               remaining - 1, position + 1, face)) return true;
                }
            }
            return false;
        }

        void accept_solution(int phase1_length, int total_length) {
            TwoPhaseSolution candidate;
            candidate.moves.reserve(total_length);
            for (int i = 0; i < total_length; ++i) {
                const auto physical = physical_face(input.frame, static_cast<Face>(path[i] / 3));
                candidate.moves.push_back({face_names[static_cast<int>(physical)],
                    static_cast<std::uint8_t>(path[i] % 3 + 1)});
            }
            candidate.turn_count = total_length;
            candidate.phase1_count = phase1_length;
            candidate.phase2_count = total_length - phase1_length;
            candidate.final_state = cube::apply(input, candidate.moves);
            if (!is_solved(candidate.final_state))
                throw std::logic_error("Two-phase result failed cubie replay");
            const auto verification = verify_sequence(input, candidate.moves, candidate.final_state);
            if (!verification) throw std::logic_error("Two-phase independent replay failed: " + verification.message);
            best_length = total_length;
            result.solved = true;
            result.solution = candidate;
            if constexpr (Triple) shared->accept(candidate, axis);
            if (callback) callback(candidate);
            if (total_length <= options.target_moves) {
                interrupted = true;
                result.stop_reason = TwoPhaseStop::target_reached;
            }
        }

        void finish_phase1(int length) {
            if (check_limits(true)) return;
            const int maximum = std::min(options.max_moves, length_bound() - 1) - length;
            if (maximum < 0) return;
            // Keep the original cheap corner-only phase-two precheck. Compute
            // other coordinates only when this prefix can improve the result.
            // Search.pas invalid-prefix bookkeeping: only recompute the suffix
            // changed by backtracking, after the cheap corner precheck.
            for (int i = corner_valid; i < length; ++i)
                corner_prefix[i + 1] = t.corner_move[corner_prefix[i]][path[i]];
            corner_valid = length;
            const int corners = corner_prefix[length];
            if (t.corner_phase2_distance[corners] > maximum) return;
            for (int i = cubie_valid; i < length; ++i)
                cubie_prefix[i + 1] = compose(cubie_prefix[i], t.physical_moves[path[i]]);
            cubie_valid = length;
            const auto& c = cubie_prefix[length];
            if (twist_coordinate(c) != 0 || flip_coordinate(c) != 0 || slice_coordinate(c) != 0)
                throw std::logic_error("Phase-one endpoint is outside G1");
            const int edges = permutation_coordinate(c.ep, 8), slice = slice_permutation(c);
            const int h = t.phase2_distance(corners, edges, maximum);
            for (int depth = h; depth <= maximum && !interrupted; ++depth) {
                if (length + depth >= length_bound()) break;
                if (phase2(corners, edges, slice, h, depth, length,
                           length == 0 ? -1 : path[length - 1] / 3)) {
                    accept_solution(length, length + depth);
                    return; // shortest phase two for this phase-one prefix
                }
            }
        }

        void phase1(std::uint32_t flip_slice, int twist, int h,
                    int remaining, int position, int previous_face) {
            if (interrupted || h > remaining) return;
            if constexpr (Triple)
                if (position + remaining >= length_bound()) return;
            if (remaining == 0) {
                if (h == 0) finish_phase1(position);
                return;
            }
            for (int face = 0; face < 6 && !interrupted; ++face) {
                if (!allowed_successor(previous_face, face)) continue;
                for (int power = 0; power < 3 && !interrupted; ++power) {
                    if (!visit_node()) return;
                    const int m = face * 3 + power;
                    const auto child = t.next_flip_slice(flip_slice, m);
                    const int child_twist = t.twist_move[twist][m];
                    const int child_h = next_distance(h, t.phase1_residue(child, child_twist));
                    // Original Next2PhaseSolution jumps to the next axis here.
                    // The other powers of this face are at most one move away;
                    // none can fit the remaining-1 moves if child_h>remaining.
                    if (child_h > remaining) break;
                    if (child_h > remaining - 1) continue;
                    // Search.pas:747: do not extend a phase-one prefix that
                    // already enters G1. This is the historical two-phase
                    // speed/solution-length tradeoff, not an optimal solver.
                    if (child_h == 0 && remaining > 1) continue;
                    path[position] = m;
                    corner_valid = std::min(corner_valid, position);
                    cubie_valid = std::min(cubie_valid, position);
                    phase1(child, child_twist, child_h, remaining - 1, position + 1, face);
                }
            }
        }

        TwoPhaseResult run() {
            const auto validation = validate(input);
            if (!validation || options.max_moves < 0 || options.max_moves > 60 ||
                options.max_phase1_moves < 0 || options.max_phase1_moves > 30 ||
                options.target_moves < 0 || options.time_limit.count() < 0) {
                result.stop_reason = TwoPhaseStop::invalid_input;
                result.message = validation ? "Invalid two-phase search limits" : validation.message;
            } else if (!check_limits(true)) {
                if (is_solved(input)) accept_solution(0, 0);
                else {
                    const auto coordinate = t.symmetric_coordinate(input.cubies);
                    const int twist = twist_coordinate(input.cubies);
                    const int h = t.phase1_distance(coordinate, twist);
                    for (int depth = h; !interrupted && depth <= options.max_phase1_moves &&
                         depth <= options.max_moves && depth < length_bound(); ++depth)
                        phase1(coordinate, twist, h, depth, 0, -1);
                }
            }
            result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            if (result.message.empty()) result.message = std::string(to_string(result.stop_reason));
            return result;
        }
    };
};

TwoPhaseSolver::TwoPhaseSolver(const std::filesystem::path& path, Progress progress,
                             std::stop_token preparation_stop) {
    // GUI rows create short-lived solver objects. Keep one immutable table set
    // per canonical directory for the process lifetime, as the original global
    // tables do, rather than rebuilding after the last row finishes searching.
    static std::timed_mutex cache_mutex;
    static std::map<std::filesystem::path, std::shared_ptr<const Tables>> cache;
    if (preparation_stop.stop_requested())
        throw std::runtime_error("Two-phase table preparation cancelled");
    const auto key = std::filesystem::weakly_canonical(path);
    std::unique_lock lock(cache_mutex, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(10)))
        if (preparation_stop.stop_requested())
            throw std::runtime_error("Two-phase table preparation cancelled");
    if (preparation_stop.stop_requested())
        throw std::runtime_error("Two-phase table preparation cancelled");
    if (const auto found = cache.find(key); found != cache.end()) {
        tables_ = found->second;
        lock.unlock();
        if (progress) progress("Reusing original HTM pruning tables");
        return;
    }
    tables_ = std::make_shared<Tables>(key, progress, preparation_stop);
    cache.emplace(key, tables_);
}
TwoPhaseSolver::~TwoPhaseSolver() = default;
TwoPhaseSolver::TwoPhaseSolver(const TwoPhaseSolver&) noexcept = default;
TwoPhaseSolver& TwoPhaseSolver::operator=(const TwoPhaseSolver&) noexcept = default;
TwoPhaseSolver::TwoPhaseSolver(TwoPhaseSolver&&) noexcept = default;
TwoPhaseSolver& TwoPhaseSolver::operator=(TwoPhaseSolver&&) noexcept = default;

TwoPhaseResult TwoPhaseSolver::solve(const CubeState& input, const TwoPhaseOptions& options,
    std::stop_token stop, OnSolution callback) const {
    if (!options.use_triple_search)
        return Tables::Search<false>(*tables_, input, options, stop, std::move(callback)).run();
    const auto validation = validate(input);
    if (!validation || is_solved(input) || options.max_moves < 0 || options.max_moves > 60 ||
        options.max_phase1_moves < 0 || options.max_phase1_moves > 30 ||
        options.target_moves < 0 || options.time_limit.count() < 0) {
        auto result = Tables::Search<false>(*tables_, input, options, stop, std::move(callback)).run();
        result.search_axes = 3;
        return result;
    }
    Tables::SharedSearch shared(input, options, stop, std::move(callback));
    const auto& axes = search_axes();
    const auto run_axis = [&](int axis) {
        std::uint64_t nodes = 0;
        try {
            CubeState transformed;
            transformed.cubies = axes.conjugate(input.cubies, axis);
            const auto outcome = Tables::Search<true>(*tables_, transformed, options, stop, {}, &shared, axis).run();
            nodes = outcome.nodes;
        } catch (...) {
            std::lock_guard lock(shared.mutex);
            if (!shared.failure) shared.failure = std::current_exception();
            shared.stop(TwoPhaseStop::cancelled);
        }
        std::lock_guard lock(shared.mutex);
        shared.result.nodes += nodes;
    };
    if (!shared.interrupted(true)) {
        std::array<std::jthread, 2> workers;
        try {
            for (int axis = 1; axis < 3; ++axis)
                workers[axis - 1] = std::jthread([&, axis] { run_axis(axis); });
            run_axis(0);
        } catch (...) {
            shared.stop(TwoPhaseStop::cancelled);
            throw;
        }
        for (auto& worker : workers) if (worker.joinable()) worker.join();
    }
    if (shared.failure) std::rethrow_exception(shared.failure);
    shared.result.stop_reason = shared.stop_reason.load(std::memory_order_relaxed);
    shared.result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - shared.started);
    shared.result.message = std::string(to_string(shared.result.stop_reason));
    return shared.result;
}

TwoPhaseTableInfo TwoPhaseSolver::table_info() const {
    TwoPhaseTableInfo info;
    info.resident_bytes = tables_->resident_bytes();
    return info;
}

int TwoPhaseSolver::phase1_lower_bound(const CubieState& input) const {
    const auto check = validate(input);
    if (!check) throw std::invalid_argument(check.message);
    return tables_->phase1_distance(tables_->symmetric_coordinate(input), twist_coordinate(input));
}

Phase1Cursor TwoPhaseSolver::phase1_cursor(const CubieState& input) const {
    const auto check = validate(input);
    if (!check) throw std::invalid_argument(check.message);
    const auto packed = tables_->symmetric_coordinate(input);
    const int twist = twist_coordinate(input);
    return {packed, static_cast<std::uint16_t>(twist),
            static_cast<std::int8_t>(tables_->phase1_distance(packed, twist))};
}

Phase1Cursor TwoPhaseSolver::advance_phase1(Phase1Cursor cursor, int move) const noexcept {
    assert(move >= 0 && move < move_count);
    assert(cursor.twist < twist_count && (cursor.flip_slice >> 4) < flip_slice_count);
    const auto packed = tables_->next_flip_slice(cursor.flip_slice, move);
    const int twist = tables_->twist_move[cursor.twist][move];
    return {packed, static_cast<std::uint16_t>(twist),
            static_cast<std::int8_t>(next_distance(cursor.distance, tables_->phase1_residue(packed, twist)))};
}

std::string_view to_string(TwoPhaseStop reason) noexcept {
    switch (reason) {
        case TwoPhaseStop::target_reached: return "target_reached";
        case TwoPhaseStop::time_limit: return "time_limit";
        case TwoPhaseStop::node_limit: return "node_limit";
        case TwoPhaseStop::cancelled: return "cancelled";
        case TwoPhaseStop::depth_limit: return "depth_limit";
        case TwoPhaseStop::invalid_input: return "invalid_input";
    }
    return "unknown";
}

} // namespace cube
