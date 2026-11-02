#include "bitboard.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define BB(sq) (UINT64_C(1) << (sq))
static uint64_t pawn_att[2][64], knight_att[64], king_att[64];
static uint64_t zob_piece[12][64], zob_castle[16], zob_ep[64], zob_side;
static uint64_t rng_state=UINT64_C(0x9e3779b97f4a7c15);
static int bitboards_initialized=0;
static uint64_t rnd64(void){ uint64_t x=rng_state; x^=x>>12; x^=x<<25; x^=x>>27; rng_state=x; return x*UINT64_C(2685821657736338717); }
static int inside(int f,int r){return f>=0&&f<8&&r>=0&&r<8;}
void init_bitboards(void){
 if(bitboards_initialized)return;
 int sq,f,r,df,dr; for(sq=0;sq<64;sq++){ f=sq&7;r=sq>>3;
  for(df=-2;df<=2;df++)for(dr=-2;dr<=2;dr++){ if((df*df+dr*dr)==5&&inside(f+df,r+dr)) knight_att[sq]|=BB((r+dr)*8+f+df); if((df||dr)&&df>=-1&&df<=1&&dr>=-1&&dr<=1&&inside(f+df,r+dr)) king_att[sq]|=BB((r+dr)*8+f+df); }
  if(inside(f-1,r+1))pawn_att[WHITE][sq]|=BB((r+1)*8+f-1); if(inside(f+1,r+1))pawn_att[WHITE][sq]|=BB((r+1)*8+f+1);
  if(inside(f-1,r-1))pawn_att[BLACK][sq]|=BB((r-1)*8+f-1); if(inside(f+1,r-1))pawn_att[BLACK][sq]|=BB((r-1)*8+f+1);
 }
 for(int p=0;p<12;p++)for(sq=0;sq<64;sq++)zob_piece[p][sq]=rnd64(); for(int i=0;i<16;i++)zob_castle[i]=rnd64(); for(sq=0;sq<64;sq++)zob_ep[sq]=rnd64(); zob_side=rnd64();bitboards_initialized=1;
}
int piece_on_square(const BoardState*b,int sq){if(!b||sq<0||sq>63)return -1;for(int p=0;p<12;p++)if(b->pieces[p]&BB(sq))return p;return -1;}
static void rebuild(BoardState*b){b->occupancies[WHITE]=b->occupancies[BLACK]=0;for(int p=0;p<6;p++)b->occupancies[WHITE]|=b->pieces[p];for(int p=6;p<12;p++)b->occupancies[BLACK]|=b->pieces[p];b->occupancies[BOTH]=b->occupancies[WHITE]|b->occupancies[BLACK];}
uint64_t compute_hash(const BoardState*b){uint64_t h=0;for(int p=0;p<12;p++){uint64_t x=b->pieces[p];while(x){int s=bit_ctzll(x);x&=x-1;h^=zob_piece[p][s];}}h^=zob_castle[b->castling&15];if(b->en_passant>=0&&b->en_passant<64)h^=zob_ep[b->en_passant];if(b->side==BLACK)h^=zob_side;return h;}
static int parse_clock(const char *text, int allow_zero, int *result) {
 if (!text || !*text) return 0;
 unsigned long value = 0;
 for (const char *p = text; *p; ++p) {
  if (*p < '0' || *p > '9') return 0;
  const unsigned long digit = (unsigned long)(*p - '0');
  if (value > ((unsigned long)INT_MAX - digit) / 10UL) return 0;
  value = value * 10UL + digit;
 }
 if (!allow_zero && value == 0) return 0;
 *result = (int)value;
 return 1;
}

int validate_fen(const char *fen) {
 if (!fen) return 0;
 size_t length = 0;
 while (length <= 127 && fen[length]) ++length;
 if (length == 0 || length > 127) return 0;

 char copy[128];
 memcpy(copy, fen, length + 1);
 char *fields[6];
 int field_count = 0;
 char *cursor = copy;
 while (*cursor) {
  while (*cursor == ' ') ++cursor;
  if (!*cursor) break;
  if (field_count == 6) return 0;
  fields[field_count++] = cursor;
  while (*cursor && *cursor != ' ') ++cursor;
  if (*cursor) *cursor++ = '\0';
 }
 if (field_count != 6) return 0;

 init_bitboards();
 BoardState position = {0};
 position.en_passant = -1;
 int rank = 7, file = 0, kings[2] = {0, 0}, pawn_count[2] = {0, 0};
 int piece_count[2] = {0, 0};
 const char *piece_chars = "PNBRQKpnbrqk";
 for (const char *p = fields[0]; *p; ++p) {
  if (*p == '/') {
   if (file != 8 || rank == 0) return 0;
   --rank;
   file = 0;
  } else if (*p >= '1' && *p <= '8') {
   file += *p - '0';
   if (file > 8) return 0;
  } else {
   const char *piece = strchr(piece_chars, *p);
   if (!piece || file >= 8) return 0;
   const int index = (int)(piece - piece_chars);
   const int side = index >= 6 ? BLACK : WHITE;
   const int square = rank * 8 + file;
   position.pieces[index] |= BB(square);
   ++piece_count[side];
   if (index == WK_PIECE) ++kings[WHITE];
   if (index == BK_PIECE) ++kings[BLACK];
   if (index == WP || index == BP) {
    ++pawn_count[side];
    if (rank == 0 || rank == 7) return 0;
   }
   ++file;
  }
 }
 if (rank != 0 || file != 8 || kings[WHITE] != 1 || kings[BLACK] != 1 ||
     piece_count[WHITE] > 16 || piece_count[BLACK] > 16 ||
     pawn_count[WHITE] > 8 || pawn_count[BLACK] > 8) return 0;

 if (!strcmp(fields[1], "w")) position.side = WHITE;
 else if (!strcmp(fields[1], "b")) position.side = BLACK;
 else return 0;

 uint64_t occupied = 0;
 for (int piece = 0; piece < 12; ++piece) occupied |= position.pieces[piece];

 if (strcmp(fields[2], "-")) {
  int rights = 0;
  for (const char *p = fields[2]; *p; ++p) {
   const int bit = *p == 'K' ? WK : *p == 'Q' ? WQ :
                   *p == 'k' ? BK : *p == 'q' ? BQ : 0;
   if (!bit || (rights & bit)) return 0;
   rights |= bit;
  }
  position.castling = rights;
 }

 if (position.castling & WK && (!(position.pieces[WK_PIECE] & BB(4)) ||
                                 !(position.pieces[WR] & BB(7)))) return 0;
 if (position.castling & WQ && (!(position.pieces[WK_PIECE] & BB(4)) ||
                                 !(position.pieces[WR] & BB(0)))) return 0;
 if (position.castling & BK && (!(position.pieces[BK_PIECE] & BB(60)) ||
                                 !(position.pieces[BR] & BB(63)))) return 0;
 if (position.castling & BQ && (!(position.pieces[BK_PIECE] & BB(60)) ||
                                 !(position.pieces[BR] & BB(56)))) return 0;

 if (strcmp(fields[3], "-")) {
  if (strlen(fields[3]) != 2 || fields[3][0] < 'a' || fields[3][0] > 'h' ||
      (fields[3][1] != '3' && fields[3][1] != '6')) return 0;
  if ((position.side == WHITE && fields[3][1] != '6') ||
      (position.side == BLACK && fields[3][1] != '3')) return 0;
  const int square = (fields[3][1] - '1') * 8 + fields[3][0] - 'a';
  if (occupied & BB(square)) return 0;
  if (position.side == WHITE) {
   if (!(position.pieces[BP] & BB(square - 8)) ||
       (occupied & BB(square + 8))) return 0;
  } else if (!(position.pieces[WP] & BB(square + 8)) ||
             (occupied & BB(square - 8))) return 0;
  position.en_passant = square;
 }

 if (!parse_clock(fields[4], 1, &position.halfmove_clock) ||
     !parse_clock(fields[5], 0, &position.fullmove_number)) return 0;

 rebuild(&position);
 const int white_king = bit_ctzll(position.pieces[WK_PIECE]);
 const int black_king = bit_ctzll(position.pieces[BK_PIECE]);
 if (king_att[white_king] & BB(black_king)) return 0;
 // The side that just moved may not have left its own king in check.
 if (is_in_check(&position, position.side ^ 1)) return 0;
 return 1;
}

void load_fen(BoardState *board, const char *fen) {
 if (!board) return;
 init_bitboards();
 if (!fen) fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
 else if (!validate_fen(fen)) return;

 const size_t length = strlen(fen);
 char copy[128];
 memcpy(copy, fen, length + 1);
 char *fields[6];
 int field_count = 0;
 char *cursor = copy;
 while (*cursor) {
  while (*cursor == ' ') ++cursor;
  if (!*cursor) break;
  fields[field_count++] = cursor;
  while (*cursor && *cursor != ' ') ++cursor;
  if (*cursor) *cursor++ = '\0';
 }
 if (field_count != 6) return;

 memset(board, 0, sizeof(*board));
 board->en_passant = -1;
 board->fullmove_number = 1;
 board->side = fields[1][0] == 'b' ? BLACK : WHITE;
 int rank = 7, file = 0;
 const char *piece_chars = "PNBRQKpnbrqk";
 cursor = fields[0];
 while (*cursor) {
  const char c = *cursor++;
  if (c == '/') {
   --rank;
   file = 0;
  } else if (isdigit((unsigned char)c)) {
   file += c - '0';
  } else {
   const char *piece = strchr(piece_chars, c);
   board->pieces[piece - piece_chars] |= BB(rank * 8 + file);
   ++file;
  }
 }

 if (strcmp(fields[2], "-")) {
  for (cursor = fields[2]; *cursor; ++cursor) {
   switch (*cursor) {
   case 'K': board->castling |= WK; break;
   case 'Q': board->castling |= WQ; break;
   case 'k': board->castling |= BK; break;
   case 'q': board->castling |= BQ; break;
   }
  }
 }

 if (strcmp(fields[3], "-")) {
  const int file_index = fields[3][0] - 'a';
  const int rank_index = fields[3][1] - '1';
  board->en_passant = rank_index * 8 + file_index;
 }
 (void)parse_clock(fields[4], 1, &board->halfmove_clock);
 (void)parse_clock(fields[5], 0, &board->fullmove_number);

 rebuild(board);
 board->hash_key = compute_hash(board);
}
static uint64_t slider_attacks(const BoardState *board, int square, int bishop) {
    static const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    const int file = square & 7;
    const int rank = square >> 3;
    const int first_direction = bishop ? 4 : 0;
    const int end_direction = bishop ? 8 : 4;
    uint64_t attacks = 0;

    for (int direction = first_direction; direction < end_direction; ++direction) {
        int target_file = file + directions[direction][0];
        int target_rank = rank + directions[direction][1];
        while (inside(target_file, target_rank)) {
            const int target = target_rank * 8 + target_file;
            attacks |= BB(target);
            if (board->occupancies[BOTH] & BB(target)) break;
            target_file += directions[direction][0];
            target_rank += directions[direction][1];
        }
    }
    return attacks;
}

int is_square_attacked(const BoardState *board, int square, int by_side) {
    if (!board || square < 0 || square > 63 ||
        (by_side != WHITE && by_side != BLACK)) return 0;

    if (by_side == WHITE) {
        return (pawn_att[BLACK][square] & board->pieces[WP]) ||
               (knight_att[square] & board->pieces[WN]) ||
               (king_att[square] & board->pieces[WK_PIECE]) ||
               (slider_attacks(board, square, 0) &
                   (board->pieces[WR] | board->pieces[WQ_PIECE])) ||
               (slider_attacks(board, square, 1) &
                   (board->pieces[WB] | board->pieces[WQ_PIECE]));
    }

    return (pawn_att[WHITE][square] & board->pieces[BP]) ||
           (knight_att[square] & board->pieces[BN]) ||
           (king_att[square] & board->pieces[BK_PIECE]) ||
           (slider_attacks(board, square, 0) &
               (board->pieces[BR] | board->pieces[BQ_PIECE])) ||
           (slider_attacks(board, square, 1) &
               (board->pieces[BB] | board->pieces[BQ_PIECE]));
}

int is_in_check(const BoardState *board, int side) {
    if (!board || (side != WHITE && side != BLACK)) return 0;
    const uint64_t king = board->pieces[side == WHITE ? WK_PIECE : BK_PIECE];
    if (!king) return 0;
    return is_square_attacked(board, bit_ctzll(king), side ^ 1);
}

static void add_move(MoveList *list, int source, int target, int piece,
                     int promoted, int capture, int double_push,
                     int en_passant, int castling) {
    if (list->count >= 256) return;

    Move move;
    move.source = source;
    move.target = target;
    move.piece = piece;
    move.promoted = promoted;
    move.capture_flag = capture;
    move.double_push_flag = double_push;
    move.enpassant_flag = en_passant;
    move.castling_flag = castling;
    list->moves[list->count++] = move;
}

static int castle_allowed(const BoardState *board, int side, int target) {
    const int home = side == BLACK ? 60 : 4;
    const int king_side = target == home + 2;
    const int expected_target = king_side ? home + 2 : home - 2;
    const int rook_square = side == BLACK
        ? (king_side ? 63 : 56)
        : (king_side ? 7 : 0);
    const int rook = side == BLACK ? BR : WR;
    const int right = side == BLACK
        ? (king_side ? BK : BQ)
        : (king_side ? WK : WQ);
    const int king_piece = side == BLACK ? BK_PIECE : WK_PIECE;

    if (target != expected_target || !(board->castling & right) ||
        !(board->pieces[king_piece] & BB(home)) ||
        !(board->pieces[rook] & BB(rook_square)) ||
        is_square_attacked(board, home, side ^ 1)) return 0;

    if (king_side) {
        if (board->occupancies[BOTH] & (BB(home + 1) | BB(home + 2))) return 0;
        return !is_square_attacked(board, home + 1, side ^ 1) &&
               !is_square_attacked(board, home + 2, side ^ 1);
    }

    if (board->occupancies[BOTH] &
        (BB(home - 1) | BB(home - 2) | BB(home - 3))) return 0;
    return !is_square_attacked(board, home - 1, side ^ 1) &&
           !is_square_attacked(board, home - 2, side ^ 1);
}

static void add_pawn_promotions(MoveList *list, int source, int target,
                                int piece, int capture) {
    const int side_offset = piece / 6 * 6;
    for (int kind = WN; kind <= WQ_PIECE; ++kind) {
        add_move(list, source, target, piece, side_offset + kind,
                 capture, 0, 0, 0);
    }
}

static void generate_pseudo_legal_moves(const BoardState *board, MoveList *list) {
    list->count = 0;
    const int side = board->side;
    const int side_offset = side == BLACK ? 6 : 0;
    const uint64_t own_pieces = board->occupancies[side];
    const uint64_t enemy_pieces = board->occupancies[side ^ 1];

    for (int kind = WP; kind <= WK_PIECE; ++kind) {
        uint64_t pieces = board->pieces[side_offset + kind];
        while (pieces) {
            const int source = bit_ctzll(pieces);
            pieces &= pieces - 1;
            const int file = source & 7;
            const int rank = source >> 3;

            if (kind == WP) {
                const int step = side == BLACK ? -8 : 8;
                const int next_rank = rank + (side == BLACK ? -1 : 1);

                if (inside(file, next_rank) &&
                    !(board->occupancies[BOTH] & BB(source + step))) {
                    if (next_rank == 0 || next_rank == 7) {
                        add_pawn_promotions(list, source, source + step,
                                            side_offset + WP, 0);
                    } else {
                        add_move(list, source, source + step,
                                 side_offset + WP, 0, 0, 0, 0, 0);
                        const int starting_rank = side == BLACK ? 6 : 1;
                        const int double_target = source + 2 * step;
                        if (rank == starting_rank &&
                            !(board->occupancies[BOTH] & BB(double_target))) {
                            add_move(list, source, double_target,
                                     side_offset + WP, 0, 0, 1, 0, 0);
                        }
                    }
                }

                uint64_t captures = pawn_att[side][source] & enemy_pieces;
                while (captures) {
                    const int target = bit_ctzll(captures);
                    captures &= captures - 1;
                    const int target_rank = target >> 3;
                    if (target_rank == 0 || target_rank == 7) {
                        add_pawn_promotions(list, source, target,
                                            side_offset + WP, 1);
                    } else {
                        add_move(list, source, target, side_offset + WP,
                                 0, 1, 0, 0, 0);
                    }
                }

                if (board->en_passant >= 0 && board->en_passant < 64 &&
                    (pawn_att[side][source] & BB(board->en_passant))) {
                    add_move(list, source, board->en_passant,
                             side_offset + WP, 0, 1, 0, 1, 0);
                }
                continue;
            }

            uint64_t destinations = 0;
            switch (kind) {
            case WN:
                destinations = knight_att[source];
                break;
            case WB:
                destinations = slider_attacks(board, source, 1);
                break;
            case WR:
                destinations = slider_attacks(board, source, 0);
                break;
            case WQ_PIECE:
                destinations = slider_attacks(board, source, 0) |
                               slider_attacks(board, source, 1);
                break;
            case WK_PIECE:
                destinations = king_att[source];
                break;
            default:
                break;
            }

            destinations &= ~own_pieces;
            while (destinations) {
                const int target = bit_ctzll(destinations);
                destinations &= destinations - 1;
                const int is_capture = (enemy_pieces & BB(target)) != 0;
                add_move(list, source, target, side_offset + kind,
                         0, is_capture, 0, 0, 0);
            }

            if (kind == WK_PIECE && source == (side == BLACK ? 60 : 4) &&
                !is_in_check(board, side)) {
                const int home = side == BLACK ? 60 : 4;
                if (castle_allowed(board, side, home + 2)) {
                    add_move(list, source, home + 2, side_offset + WK_PIECE,
                             0, 0, 0, 0, 1);
                }
                if (castle_allowed(board, side, home - 2)) {
                    add_move(list, source, home - 2, side_offset + WK_PIECE,
                             0, 0, 0, 0, 1);
                }
            }
        }
    }
}

static int apply_pseudo_move(BoardState *board, Move move) {
    if (!board || board->side < WHITE || board->side > BLACK ||
        move.source < 0 || move.source > 63 || move.target < 0 || move.target > 63 ||
        move.source == move.target || move.piece < 0 || move.piece > 11 ||
        !(board->pieces[move.piece] & BB(move.source)) ||
        move.piece / 6 != board->side ||
        (board->occupancies[board->side] & BB(move.target))) return 0;

    const int side = board->side;
    const int source_rank = move.source >> 3;
    const int target_rank = move.target >> 3;
    const int is_pawn = move.piece % 6 == WP;

    if (move.promoted &&
        (move.promoted < 0 || move.promoted >= 12 ||
         move.promoted / 6 != side || move.promoted % 6 < WN ||
         move.promoted % 6 > WQ_PIECE || !is_pawn ||
         target_rank != (side == BLACK ? 0 : 7))) return 0;
    if (!move.promoted && is_pawn &&
        target_rank == (side == BLACK ? 0 : 7)) return 0;
    if (move.enpassant_flag && move.double_push_flag) return 0;

    if (move.enpassant_flag &&
        (!is_pawn || move.target != board->en_passant ||
         piece_on_square(board, move.target) >= 0 ||
         piece_on_square(board, move.target + (side == BLACK ? 8 : -8)) !=
             (side == BLACK ? WP : BP))) return 0;

    if (move.double_push_flag &&
        (!is_pawn || source_rank != (side == BLACK ? 6 : 1) ||
         move.target != move.source + (side == BLACK ? -16 : 16) ||
         piece_on_square(board, move.source + (side == BLACK ? -8 : 8)) >= 0 ||
         piece_on_square(board, move.target) >= 0)) return 0;

    if (move.castling_flag &&
        (move.piece % 6 != WK_PIECE ||
         move.source != (side == BLACK ? 60 : 4) ||
         !castle_allowed(board, side, move.target))) return 0;

    const int captured_piece = piece_on_square(board, move.target);
    if (captured_piece == (side == BLACK ? WK_PIECE : BK_PIECE)) return 0;

    const BoardState old_position = *board;
    board->pieces[move.piece] &= ~BB(move.source);
    if (move.enpassant_flag) {
        const int captured_square = move.target + (side == BLACK ? 8 : -8);
        const int captured_pawn = side == BLACK ? WP : BP;
        board->pieces[captured_pawn] &= ~BB(captured_square);
    } else if (captured_piece >= 0) {
        board->pieces[captured_piece] &= ~BB(move.target);
    }

    const int placed_piece = move.promoted ? move.promoted : move.piece;
    board->pieces[placed_piece] |= BB(move.target);

    if (move.castling_flag) {
        switch (move.target) {
        case 6:
            board->pieces[WR] &= ~BB(7);
            board->pieces[WR] |= BB(5);
            break;
        case 2:
            board->pieces[WR] &= ~BB(0);
            board->pieces[WR] |= BB(3);
            break;
        case 62:
            board->pieces[BR] &= ~BB(63);
            board->pieces[BR] |= BB(61);
            break;
        case 58:
            board->pieces[BR] &= ~BB(56);
            board->pieces[BR] |= BB(59);
            break;
        }
    }

    if (move.source == 4 || move.target == 4) board->castling &= ~(WK | WQ);
    if (move.source == 60 || move.target == 60) board->castling &= ~(BK | BQ);
    if (move.source == 0 || move.target == 0) board->castling &= ~WQ;
    if (move.source == 7 || move.target == 7) board->castling &= ~WK;
    if (move.source == 56 || move.target == 56) board->castling &= ~BQ;
    if (move.source == 63 || move.target == 63) board->castling &= ~BK;

    board->en_passant = move.double_push_flag
        ? move.source + (side == BLACK ? -8 : 8)
        : -1;
    if (is_pawn || captured_piece >= 0 || move.enpassant_flag) {
        board->halfmove_clock = 0;
    } else if (board->halfmove_clock < INT_MAX) {
        ++board->halfmove_clock;
    }
    if (side == BLACK && board->fullmove_number < INT_MAX) {
        ++board->fullmove_number;
    }

    board->side ^= 1;
    rebuild(board);
    if (is_in_check(board, side)) {
        *board = old_position;
        return 0;
    }

    board->hash_key = compute_hash(board);
    return 1;
}

int make_generated_move(BoardState *board, Move move) {
    return apply_pseudo_move(board, move);
}

int make_move(BoardState *board, Move move) {
    if (!board || (board->side != WHITE && board->side != BLACK)) return 0;

    MoveList pseudo_legal = {0};
    generate_pseudo_legal_moves(board, &pseudo_legal);
    for (int i = 0; i < pseudo_legal.count; ++i) {
        const Move candidate = pseudo_legal.moves[i];
        if (candidate.source == move.source && candidate.target == move.target &&
            candidate.piece == move.piece && candidate.promoted == move.promoted) {
            return apply_pseudo_move(board, candidate);
        }
    }
    return 0;
}

void generate_legal_moves(const BoardState *board, MoveList *legal_moves) {
    if (!legal_moves) return;
    legal_moves->count = 0;
    if (!board || (board->side != WHITE && board->side != BLACK)) return;

    MoveList pseudo_legal = {0};
    generate_pseudo_legal_moves(board, &pseudo_legal);
    for (int i = 0; i < pseudo_legal.count; ++i) {
        BoardState child = *board;
        if (make_generated_move(&child, pseudo_legal.moves[i])) {
            legal_moves->moves[legal_moves->count++] = pseudo_legal.moves[i];
        }
    }
}

uint64_t perft(BoardState *board, int depth) {
    if (!board || depth < 0) return 0;
    if (depth == 0) return 1;

    MoveList legal_moves = {0};
    generate_legal_moves(board, &legal_moves);
    if (depth == 1) return (uint64_t)legal_moves.count;

    uint64_t nodes = 0;
    for (int i = 0; i < legal_moves.count; ++i) {
        BoardState child = *board;
        if (make_generated_move(&child, legal_moves.moves[i])) {
            nodes += perft(&child, depth - 1);
        }
    }
    return nodes;
}

int parse_uci_move(const BoardState *board, const char *text, Move *out) {
    if (!board || !text || !out) return 0;
    const size_t length = strlen(text);
    if (length != 4 && length != 5) return 0;
    if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8' ||
        text[2] < 'a' || text[2] > 'h' || text[3] < '1' || text[3] > '8') return 0;

    const int source = (text[1] - '1') * 8 + (text[0] - 'a');
    const int target = (text[3] - '1') * 8 + (text[2] - 'a');
    const char promotion = length == 5
        ? (char)tolower((unsigned char)text[4])
        : '\0';
    if (length == 5 && promotion != 'n' && promotion != 'b' &&
        promotion != 'r' && promotion != 'q') return 0;

    MoveList legal_moves = {0};
    generate_legal_moves(board, &legal_moves);
    for (int i = 0; i < legal_moves.count; ++i) {
        const Move move = legal_moves.moves[i];
        if (move.source != source || move.target != target) continue;
        if (move.promoted) {
            const int requested_piece = promotion == 'n' ? WN :
                                        promotion == 'b' ? WB :
                                        promotion == 'r' ? WR : WQ_PIECE;
            if (length != 5 || move.promoted % 6 != requested_piece) continue;
        } else if (length != 4) {
            continue;
        }
        *out = move;
        return 1;
    }
    return 0;
}
