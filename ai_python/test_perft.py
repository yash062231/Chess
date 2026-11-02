"""Run the C move generator against standard reference perft positions."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import chess_engine  # noqa: E402

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"

def main() -> None:
    engine = chess_engine.SearchEngine()
    positions = ((START, (20, 400, 8902, 197281)), (KIWIPETE, (48, 2039, 97862)))
    for fen, expected in positions:
        engine.load_fen(fen)
        for depth, nodes in enumerate(expected, 1):
            actual = engine.run_perft(depth)
            assert actual == nodes, f"depth {depth}: expected {nodes}, got {actual} for {fen}"
            print(f"depth {depth}: {actual:,} nodes")

    engine.reset()
    assert len(engine.get_legal_moves()) == 20
    assert not engine.apply_uci("e2e5")
    assert engine.apply_uci("e2e4")
    assert engine.get_side_to_move() == 1

    engine.load_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1")
    assert engine.apply_uci("e5d6")
    engine.load_fen("k7/8/8/4KpPr/8/8/8/8 w - f6 0 1")
    assert not engine.apply_uci("g5f6")  # En passant would expose the White king.

    engine.load_fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1")
    assert engine.apply_uci("a7a8n")
    assert engine.get_board_array()[56] == 1
    assert not engine.apply_uci("a7a8k")

    engine.load_fen("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1")
    assert engine.get_game_status() == "CHECKMATE_WHITE"
    engine.load_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")
    assert engine.get_game_status() == "STALEMATE"
    engine.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 100 1")
    assert engine.get_game_status() == "DRAW_50"

    try:
        engine.load_fen("not-a-fen")
    except ValueError:
        pass
    else:
        raise AssertionError("Malformed FEN was accepted")

    print("Perft, special moves, FEN validation, and terminal-state checks passed.")

if __name__ == "__main__":
    main()
