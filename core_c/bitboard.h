#ifndef CHESS_ENGINE_BITBOARD_H
#define CHESS_ENGINE_BITBOARD_H
#include <stdint.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
enum { WHITE=0, BLACK=1, BOTH=2 };
enum { WK=1, WQ=2, BK=4, BQ=8 };
enum { WP, WN, WB, WR, WQ_PIECE, WK_PIECE, BP, BN, BB, BR, BQ_PIECE, BK_PIECE };
typedef struct { uint64_t pieces[12], occupancies[3]; int side,en_passant,castling,halfmove_clock,fullmove_number; uint64_t hash_key; } BoardState;
typedef struct { int source,target,piece,promoted,capture_flag,double_push_flag,enpassant_flag,castling_flag; } Move;
typedef struct { Move moves[256]; int count; } MoveList;
#if defined(_MSC_VER)
static __inline int bit_ctzll(uint64_t x) {
 unsigned long i; if (!_BitScanForward64(&i,x)) return 64; return (int)i;
}
static __inline int bit_popcountll(uint64_t x) {
 return (int)__popcnt64(x);
}
#else
static inline int bit_ctzll(uint64_t x) {
 return x ? __builtin_ctzll(x) : 64;
}
static inline int bit_popcountll(uint64_t x) {
 return __builtin_popcountll(x);
}
#endif
/* Initialize attack masks and deterministic Zobrist keys before direct C use.
 * The Python binding and load_fen call this during normal application startup. */
void init_bitboards(void);
/* Validates the six-field FEN syntax and basic king/board constraints. */
int validate_fen(const char *fen);
/* NULL loads the standard start position; invalid non-NULL input leaves board
 * unchanged. Check validate_fen first when reporting input errors to a caller. */
void load_fen(BoardState *board,const char *fen);
int is_square_attacked(const BoardState *board,int sq,int by_side);
int is_in_check(const BoardState *board,int side);
/* Applies a pseudo-legal move only if it is present in generated move candidates
 * and leaves the moving side's king safe. */
int make_move(BoardState *board,Move move);
/* Fast path for a move returned by generate_legal_moves. Do not pass arbitrary
 * caller-constructed moves to this function. */
int make_generated_move(BoardState *board,Move move);
void generate_legal_moves(const BoardState *board,MoveList *legal_list);
uint64_t perft(BoardState *board,int depth);
int piece_on_square(const BoardState *board,int sq);
uint64_t compute_hash(const BoardState *board);
int parse_uci_move(const BoardState *board,const char *uci,Move *out);
#ifdef __cplusplus
}
#endif
#endif
