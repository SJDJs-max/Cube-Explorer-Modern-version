#include "cube/mcc.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cube;

namespace {
int checks = 0;

void expect_eq(double got, double want, const std::string& label) {
    ++checks;
    if (std::fabs(got - want) > 1e-9) {
        throw std::runtime_error("MCC mismatch on [" + label + "]: got " + std::to_string(got) +
                                 ", expected " + std::to_string(want));
    }
}
} // namespace

int main() {
    try {
        struct Case {
            const char* alg;
            double def_mcc;
            double custom_mcc;
        };
        const std::vector<Case> cases = {
            {"", 0.0, 0.0},
            {"R U R'", 3.4, 4.0},
            {"R U R' U'", 4.4, 5.0},
            {"U R U' R'", 3.6, 4.0},
            {"R U R' U R U2 R'", 7.6, 8.8},
            {"R U2 R' U' R U' R'", 8.3, 9.8},
            {"R U R' U' R' F R2 U' R' U' R U R' F'", 14.4, 16.6},
            {"r U R' U' r' F R F'", 11.2, 12.2},
            {"M2 U M2 U2 M2 U M2", 14.6, 16.2},
            {"M' U M2 U M2 U M' U2 M2", 17.2, 18.9},
            {"R' U' F' R U R' U' R' F R2 U' R' U' R U R' U R", 19.6, 22.3},
            {"R U R' F' R U R' U' R' F R2 U' R'", 13.4, 15.6},
            {"F R U' R' U' R U R' F' R U R' U' R' F R F'", 19.2, 21.5},
            {"R' U2 R U2 R' F R U R' U' R' F' R2", 16.9, 19.6},
            {"L' U' L U' L' U2 L", 7.8, 9.0},
            {"R U' R' U F' U L' U2 L F U", 16.0, 17.8},
            {"M' U R U' R' U' R U' r' U2 r U' r'", 19.1, 21.2},
            {"R U D' R' U' D R2", 7.6, 9.3},
            {"S R U R' U' R' F R f'", 14.1, 16.0},
            {"R E R' U' R E' R'", 9.0, 10.7},
            {"R U R' U' x R' U R U'", 10.5, 11.7},
            {"y R U R' U' F'", 8.1, 9.0},
            {"y2 R U2 R' U' R U' R'", 12.6, 15.0},
            {"z R U R' U' z'", 10.6, 12.0},
            {"B U B' U' B2 D2 F2 L2", 18.0, 20.9},
            {"d U R U' R'", 9.5, 10.1},
            {"D U R U' R'", 4.5, 5.1},
            {"f R U R' U' f'", 7.4, 8.2},
            {"u R U' R' u'", 4.6, 5.0},
            {"l' U' L U R U' r' F", 11.0, 12.1},
            {"R2 u R' U R' U' R u' R2 y' R' U R", 16.9, 18.2},
            {"S2 R U R' S2", 11.6, 13.2},
            {"E2 R U R' E2", 8.7, 10.2},
            {"x2 R U R' U'", 8.6, 9.8},
            {"x' R U' R' D R U R' D'", 10.4, 12.2},
        };

        const MccParams custom{1.0, 1.5, 1.6, 0.7, 1.2, 1.8, 1.4, 2.5, 1.0, 4.0};
        for (const auto& c : cases) {
            const auto moves = std::string(c.alg).empty() ? std::vector<Move>{} : parse_moves(c.alg);
            expect_eq(compute_mcc(moves, MccParams{}, false), c.def_mcc, std::string("default:") + c.alg);
            expect_eq(compute_mcc(moves, custom, false), c.custom_mcc, std::string("custom:") + c.alg);
        }

        // Pre-AUF stripping parity (only drops leading U or D/d + U, never trailing U).
        expect_eq(compute_mcc(parse_moves("U R U' R'"), MccParams{}, false), 3.6, "with pre-AUF");
        expect_eq(compute_mcc(parse_moves("U R U' R'"), MccParams{}, true), 2.6, "without pre-AUF");
        expect_eq(compute_mcc(parse_moves("R U' R' U"), MccParams{}, true),
                  compute_mcc(parse_moves("R U' R' U"), MccParams{}, false), "trailing U preserved");
        expect_eq(compute_mcc(parse_moves("d U R U' R'"), MccParams{}, true),
                  compute_mcc(parse_moves("d R U' R'"), MccParams{}, false), "d U pre-AUF");

        std::cout << "PASS mcc tests: " << checks << " checks matched algSpeed.js 100%.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL mcc tests: " << e.what() << '\n';
        return 1;
    }
}
