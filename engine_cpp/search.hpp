#ifndef CHESS_ENGINE_SEARCH_HPP
#define CHESS_ENGINE_SEARCH_HPP

#include "evaluator.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

struct SearchResult {
    Move best_move{};
    int score_cp = 0;
    int depth = 0;
    uint64_t nodes = 0;
};

// Iterative-deepening negamax search with alpha-beta pruning, principal-variation
// search, a direct-mapped transposition table, move ordering, and quiescence.
class SearchEngine {
    static constexpr int max_search_ply = 128;
    static constexpr int mate_score = 30000;
    static constexpr int max_mate_distance = max_search_ply;
    static constexpr int max_depth = 64;

    struct Entry {
        uint64_t key = 0;
        int depth = -1;
        int score = 0;
        // 0 = exact, 1 = upper bound, 2 = lower bound.
        int bound = 0;
        Move move{};
    };

    std::vector<Entry> table;
    Evaluator evaluator;
    uint64_t nodes = 0;
    std::array<std::array<Move, 2>, max_search_ply> killers{};
    std::chrono::steady_clock::time_point deadline{};
    bool stopped = false;

    static int piece_value(int piece) {
        static constexpr int values[6] = {100, 320, 335, 500, 900, 20000};
        return values[piece % 6];
    }

    static bool same_move(Move first, Move second) {
        return first.source == second.source && first.target == second.target &&
               first.promoted == second.promoted;
    }

    static int score_to_table(int score, int ply) {
        if (score > mate_score - max_mate_distance) return score + ply;
        if (score < -mate_score + max_mate_distance) return score - ply;
        return score;
    }

    static int score_from_table(int score, int ply) {
        if (score > mate_score - max_mate_distance) return score - ply;
        if (score < -mate_score + max_mate_distance) return score + ply;
        return score;
    }

    bool check_timeout() {
        // Polling every 256 nodes keeps clock reads out of most inner-loop work.
        if ((nodes < 2 || (nodes & 255U) == 0) &&
            std::chrono::steady_clock::now() >= deadline) {
            stopped = true;
        }
        return stopped;
    }

    void order_moves(std::vector<Move>& moves, const BoardState& board,
                     Move table_move, int ply) const {
        const auto score = [&](Move move) {
            if (same_move(move, table_move)) return 1'000'000;

            if (move.capture_flag) {
                const int victim = move.enpassant_flag ? 100 : piece_on_square(&board, move.target);
                const int victim_value = victim >= 0 ? piece_value(victim) : 100;
                return 100'000 + victim_value * 10 - piece_value(move.piece);
            }

            if (move.promoted) return 90'000 + piece_value(move.promoted);
            if (ply < max_search_ply && same_move(move, killers[ply][0])) return 80'000;
            if (ply < max_search_ply && same_move(move, killers[ply][1])) return 79'000;
            return 0;
        };

        std::stable_sort(moves.begin(), moves.end(), [&](Move first, Move second) {
            return score(first) > score(second);
        });
    }

    int quiescence(BoardState& board, int alpha, int beta, int ply) {
        ++nodes;
        if (check_timeout()) return alpha;
        if (ply >= max_search_ply) return evaluator.evaluate(board);

        const bool in_check = is_in_check(&board, board.side);
        MoveList legal{};
        generate_legal_moves(&board, &legal);

        // Detect stalemate and checkmate before stand-pat can return early.
        if (legal.count == 0) return in_check ? -mate_score + ply : 0;
        if (board.halfmove_clock >= 100) return 0;

        if (!in_check) {
            const int stand_pat = evaluator.evaluate(board);
            if (stand_pat >= beta) return beta;
            alpha = std::max(alpha, stand_pat);
        }

        std::vector<Move> tactical_moves;
        tactical_moves.reserve(static_cast<std::size_t>(legal.count));
        for (int i = 0; i < legal.count; ++i) {
            const Move move = legal.moves[i];
            if (in_check || move.capture_flag || move.promoted) tactical_moves.push_back(move);
        }
        order_moves(tactical_moves, board, Move{}, ply);

        for (const Move move : tactical_moves) {
            BoardState child = board;
            if (!make_generated_move(&child, move)) continue;

            const int score = -quiescence(child, -beta, -alpha, ply + 1);
            if (stopped) return alpha;
            if (score >= beta) return beta;
            alpha = std::max(alpha, score);
        }
        return alpha;
    }

    int search(BoardState& board, int depth, int alpha, int beta, int ply) {
        if (ply >= max_search_ply) return evaluator.evaluate(board);
        ++nodes;
        if (check_timeout()) return alpha;

        const bool in_check = is_in_check(&board, board.side);
        if (in_check) ++depth; // One-ply check extension to avoid horizon blunders.
        if (depth <= 0) return quiescence(board, alpha, beta, ply);

        // Halfmove clocks are not part of the position hash, so adjudicate a
        // 50-move draw before consulting the transposition table.
        if (board.halfmove_clock >= 100) {
            MoveList terminal_check{};
            generate_legal_moves(&board, &terminal_check);
            if (terminal_check.count == 0) return in_check ? -mate_score + ply : 0;
            return 0;
        }

        const int original_alpha = alpha;
        Entry& entry = table[board.hash_key % table.size()];
        Move table_move{};
        if (entry.key == board.hash_key) {
            table_move = entry.move;
            if (entry.depth >= depth) {
                const int cached_score = score_from_table(entry.score, ply);
                if (entry.bound == 0) return cached_score;
                if (entry.bound == 1 && cached_score <= alpha) return cached_score;
                if (entry.bound == 2 && cached_score >= beta) return cached_score;
            }
        }

        MoveList legal{};
        generate_legal_moves(&board, &legal);
        if (legal.count == 0) return in_check ? -mate_score + ply : 0;

        std::vector<Move> moves(legal.moves, legal.moves + legal.count);
        order_moves(moves, board, table_move, ply);

        Move best_move = moves.front();
        int searched_moves = 0;
        for (const Move move : moves) {
            BoardState child = board;
            if (!make_generated_move(&child, move)) continue;

            int score;
            if (searched_moves == 0) {
                score = -search(child, depth - 1, -beta, -alpha, ply + 1);
            } else {
                // PVS first tests later moves with a null window; only a move
                // that improves alpha receives a full re-search.
                score = -search(child, depth - 1, -alpha - 1, -alpha, ply + 1);
                if (score > alpha && score < beta) {
                    score = -search(child, depth - 1, -beta, -alpha, ply + 1);
                }
            }

            if (stopped) return alpha;
            ++searched_moves;
            if (score > alpha) {
                alpha = score;
                best_move = move;
            }

            if (alpha >= beta) {
                if (!move.capture_flag && ply < max_search_ply) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = move;
                }
                break;
            }
        }

        entry.key = board.hash_key;
        entry.depth = depth;
        entry.score = score_to_table(alpha, ply);
        entry.bound = alpha <= original_alpha ? 1 : (alpha >= beta ? 2 : 0);
        entry.move = best_move;
        return alpha;
    }

public:
    SearchEngine() : table(1U << 20) {
        init_bitboards();
    }

    void clear() {
        std::fill(table.begin(), table.end(), Entry{});
        killers = {};
    }

    Evaluator& get_evaluator() { return evaluator; }

    SearchResult best(BoardState board, int requested_depth, int time_limit_ms) {
        SearchResult result;
        nodes = 0;
        stopped = false;

        const int depth_limit = std::clamp(requested_depth, 1, max_depth);
        const int time_limit = std::clamp(time_limit_ms, 1, 60000);
        deadline = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(time_limit);

        MoveList legal{};
        generate_legal_moves(&board, &legal);
        if (legal.count == 0 || board.halfmove_clock >= 100) return result;

        // Always keep a legal fallback if the time limit interrupts depth one.
        result.best_move = legal.moves[0];
        for (int depth = 1; depth <= depth_limit; ++depth) {
            if (check_timeout()) break;

            const int score = search(board, depth, -mate_score - 1, mate_score + 1, 0);
            if (stopped) break;

            result.depth = depth;
            result.score_cp = score;
            const Entry& root_entry = table[board.hash_key % table.size()];
            if (root_entry.key == board.hash_key) result.best_move = root_entry.move;
        }

        result.nodes = nodes;
        return result;
    }
};

#endif
