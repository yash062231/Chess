#ifndef CHESS_ENGINE_EVALUATOR_HPP
#define CHESS_ENGINE_EVALUATOR_HPP

#include "../core_c/bitboard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

// Evaluations are measured in centipawns and returned from the side-to-move's
// point of view, which lets negamax use one score convention throughout.
class Evaluator {
    static constexpr std::size_t feature_count = 12 * 64;
    static constexpr std::size_t hidden_count = 128;
    static constexpr std::size_t expected_float_count =
        feature_count * hidden_count + hidden_count + hidden_count + 1;
    static constexpr float max_weight_magnitude = 1'000'000.0f;

    std::vector<float> input_weights;
    std::array<float, hidden_count> hidden_bias{};
    std::array<float, hidden_count> output_weights{};
    float output_bias = 0.0f;
    bool has_network = false;

    static int piece_square_bonus(int piece, int square) {
        const int file = square & 7;
        const int rank = square >> 3;
        const int center = 6 - (std::abs(3 - file) + std::abs(3 - rank));

        switch (piece % 6) {
        case 0: return rank * 7 + center * 2; // Pawn: advance and approach center.
        case 1: return center * 7;            // Knight.
        case 2: return center * 4;            // Bishop.
        case 3: return center * 2;            // Rook.
        case 4: return center * 2;            // Queen.
        default: return 0;                    // King: material/phase handles it.
        }
    }

public:
    // The optional binary format is little-endian float32 in row-major order:
    // [768 x 128 input weights, 128 hidden biases, 128 output weights, 1 bias].
    bool load(const std::string& path) {
        has_network = false;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;

        const auto byte_count = file.tellg();
        const auto expected_byte_count = static_cast<std::streamoff>(
            expected_float_count * sizeof(float));
        if (byte_count != expected_byte_count) return false;

        file.seekg(0, std::ios::beg);
        std::vector<float> values(expected_float_count);
        file.read(reinterpret_cast<char*>(values.data()), expected_byte_count);
        if (!file || file.gcount() != expected_byte_count) return false;

        for (float value : values) {
            if (!std::isfinite(value) || std::abs(value) > max_weight_magnitude) {
                return false;
            }
        }

        std::size_t offset = 0;
        input_weights.assign(values.begin(), values.begin() + feature_count * hidden_count);
        offset += feature_count * hidden_count;
        std::copy_n(values.begin() + offset, hidden_count, hidden_bias.begin());
        offset += hidden_count;
        std::copy_n(values.begin() + offset, hidden_count, output_weights.begin());
        offset += hidden_count;
        output_bias = values[offset];
        has_network = true;
        return true;
    }

    int evaluate(const BoardState& board) const {
        static constexpr int middlegame_material[6] = {100, 325, 335, 500, 975, 0};
        static constexpr int endgame_material[6] = {120, 320, 335, 510, 950, 0};

        int middlegame_score = 0;
        int endgame_score = 0;
        int phase = 0;
        std::array<float, hidden_count> hidden = hidden_bias;

        // Material and piece-square terms are accumulated from White's view.
        // Mirroring Black's square keeps advancement bonuses symmetric.
        for (int piece = 0; piece < 12; ++piece) {
            uint64_t pieces = board.pieces[piece];
            while (pieces) {
                const int square = bit_ctzll(pieces);
                pieces &= pieces - 1;

                const int sign = piece < 6 ? 1 : -1;
                const int relative_square = piece < 6 ? square : 63 - square;
                const int kind = piece % 6;
                const int positional = piece_square_bonus(piece, relative_square);

                middlegame_score += sign * (middlegame_material[kind] + positional);
                endgame_score += sign * (endgame_material[kind] + positional / 2);
                phase += (kind == 1 || kind == 2) ? 1 : (kind == 3 ? 2 : (kind == 4 ? 4 : 0));

                if (has_network) {
                    const std::size_t row = static_cast<std::size_t>(piece) * 64 + square;
                    const std::size_t base = row * hidden_count;
                    for (std::size_t neuron = 0; neuron < hidden_count; ++neuron) {
                        hidden[neuron] += input_weights[base + neuron];
                    }
                }
            }
        }

        phase = std::min(24, phase);
        const int classical_score =
            (middlegame_score * phase + endgame_score * (24 - phase)) / 24;

        int white_score = classical_score;
        if (has_network) {
            double network_score = output_bias;
            for (std::size_t neuron = 0; neuron < hidden_count; ++neuron) {
                const float activation = std::clamp(hidden[neuron], 0.0f, 1.0f);
                network_score += static_cast<double>(activation) * output_weights[neuron];
            }

            // Bound malformed or poorly calibrated checkpoints before converting
            // to an integer score. Keep the hand-built evaluator in control.
            const int neural_score = static_cast<int>(std::lround(
                std::clamp(network_score, -10000.0, 10000.0)));
            white_score = (classical_score * 3 + neural_score) / 4;
        }

        return board.side == WHITE ? white_score : -white_score;
    }
};

#endif
