#include "search.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>

namespace py = pybind11;

namespace {

std::string to_uci(const Move& move) {
    std::string text;
    text += static_cast<char>('a' + (move.source & 7));
    text += static_cast<char>('1' + (move.source >> 3));
    text += static_cast<char>('a' + (move.target & 7));
    text += static_cast<char>('1' + (move.target >> 3));

    if (move.promoted) {
        static constexpr char promotion_letters[] = "pnbrqk";
        text += promotion_letters[move.promoted % 6];
    }
    return text;
}

} // namespace

class EngineAPI {
    SearchEngine engine;
    BoardState board{};
    std::string status = "PLAYING";

    void update_status() {
        MoveList legal{};
        generate_legal_moves(&board, &legal);

        if (legal.count == 0) {
            if (is_in_check(&board, board.side)) {
                status = board.side == WHITE ? "CHECKMATE_BLACK" : "CHECKMATE_WHITE";
            } else {
                status = "STALEMATE";
            }
        } else if (board.halfmove_clock >= 100) {
            status = "DRAW_50";
        } else {
            status = "PLAYING";
        }
    }

public:
    EngineAPI() {
        reset();
    }

    void reset() {
        load_fen(&board, nullptr);
        engine.clear();
        update_status();
    }

    void load(const std::string& fen) {
        if (!validate_fen(fen.c_str())) throw py::value_error("Invalid FEN string");
        load_fen(&board, fen.c_str());
        engine.clear();
        update_status();
    }

    bool load_weights(const std::string& path) {
        return engine.get_evaluator().load(path);
    }

    py::list legal_moves() const {
        MoveList moves{};
        generate_legal_moves(&board, &moves);

        py::list result;
        for (int i = 0; i < moves.count; ++i) {
            const Move& move = moves.moves[i];
            py::dict item;
            item["source"] = move.source;
            item["target"] = move.target;
            item["promotion"] = move.promoted
                ? std::string(1, " pnbrq"[move.promoted % 6])
                : std::string();
            item["uci"] = to_uci(move);
            result.append(item);
        }
        return result;
    }

    bool apply_human_move(int source, int target, const std::string& promotion) {
        MoveList moves{};
        generate_legal_moves(&board, &moves);

        for (int i = 0; i < moves.count; ++i) {
            const Move move = moves.moves[i];
            if (move.source != source || move.target != target) continue;
            if (move.promoted &&
                (promotion.empty() || " nbrq"[move.promoted % 6] != promotion[0])) {
                continue;
            }

            const bool applied = make_generated_move(&board, move);
            update_status();
            return applied;
        }
        return false;
    }

    py::dict best_move(int max_depth, int time_limit_ms) {
        const SearchResult result = engine.best(board, max_depth, time_limit_ms);

        py::dict response;
        response["uci"] = to_uci(result.best_move);
        response["source"] = result.best_move.source;
        response["target"] = result.best_move.target;
        response["promotion"] = result.best_move.promoted
            ? std::string(1, " pnbrq"[result.best_move.promoted % 6])
            : std::string();
        response["score_cp"] = result.score_cp;
        response["depth"] = result.depth;
        response["nodes"] = result.nodes;
        return response;
    }

    bool apply_uci(const std::string& text) {
        Move move{};
        if (!parse_uci_move(&board, text.c_str(), &move)) return false;

        const bool applied = make_generated_move(&board, move);
        update_status();
        return applied;
    }

    std::string game_status() {
        update_status();
        return status;
    }

    py::list board_array() const {
        py::list result;
        for (int square = 0; square < 64; ++square) {
            result.append(piece_on_square(&board, square));
        }
        return result;
    }

    std::string fen() const {
        std::string text;
        static constexpr char pieces[] = "PNBRQKpnbrqk";

        for (int rank = 7; rank >= 0; --rank) {
            int empty_squares = 0;
            for (int file = 0; file < 8; ++file) {
                const int piece = piece_on_square(&board, rank * 8 + file);
                if (piece < 0) {
                    ++empty_squares;
                    continue;
                }

                if (empty_squares) {
                    text += static_cast<char>('0' + empty_squares);
                    empty_squares = 0;
                }
                text += pieces[piece];
            }
            if (empty_squares) text += static_cast<char>('0' + empty_squares);
            if (rank > 0) text += '/';
        }

        text += board.side == WHITE ? " w " : " b ";
        std::string rights;
        if (board.castling & WK) rights += 'K';
        if (board.castling & WQ) rights += 'Q';
        if (board.castling & BK) rights += 'k';
        if (board.castling & BQ) rights += 'q';
        text += rights.empty() ? "-" : rights;
        text += ' ';

        if (board.en_passant < 0) {
            text += '-';
        } else {
            text += static_cast<char>('a' + (board.en_passant & 7));
            text += static_cast<char>('1' + (board.en_passant >> 3));
        }
        text += ' ' + std::to_string(board.halfmove_clock);
        text += ' ' + std::to_string(board.fullmove_number);
        return text;
    }

    uint64_t run_perft(int depth) { return perft(&board, depth); }
    bool in_check() { return is_in_check(&board, board.side); }
    int side_to_move() const { return board.side; }
    int halfmove_clock() const { return board.halfmove_clock; }
};

PYBIND11_MODULE(chess_engine, module) {
    init_bitboards();

    py::class_<EngineAPI>(module, "SearchEngine")
        .def(py::init<>())
        .def("reset", &EngineAPI::reset)
        .def("load_fen", &EngineAPI::load)
        .def("load_weights", &EngineAPI::load_weights)
        .def("get_legal_moves", &EngineAPI::legal_moves)
        .def("apply_human_move", &EngineAPI::apply_human_move)
        .def("apply_uci", &EngineAPI::apply_uci)
        .def("get_best_move", &EngineAPI::best_move,
             py::arg("max_depth") = 6, py::arg("time_limit_ms") = 1000)
        .def("get_game_status", &EngineAPI::game_status)
        .def("get_board_array", &EngineAPI::board_array)
        .def("run_perft", &EngineAPI::run_perft)
        .def("get_fen", &EngineAPI::fen)
        .def("is_in_check", &EngineAPI::in_check)
        .def("get_side_to_move", &EngineAPI::side_to_move)
        .def("get_halfmove_clock", &EngineAPI::halfmove_clock);
}
