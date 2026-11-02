# Bitboard Chess Engine Lab

A local human-versus-engine chess demonstration. The project connects a C bitboard move generator, a C++ search engine, and a small Python API to a browser interface that exposes live search telemetry.

For the full technology stack, architecture diagram, C/C++ walkthrough, demo script, test results, and likely professor questions, see [PRESENTATION_GUIDE.md](PRESENTATION_GUIDE.md).

## Project structure

| Path | Role |
| --- | --- |
| `core_c/bitboard.c`, `core_c/bitboard.h` | Board representation, move generation, FEN handling, and perft support |
| `engine_cpp/evaluator.hpp` | Position evaluation |
| `engine_cpp/search.hpp` | Search, move ordering, and transposition table |
| `engine_cpp/bindings.cpp` | Python bindings for the native engine |
| `ai_python/server.py` | FastAPI game API and static page route |
| `ai_python/train_model.py` | Model training/export utility |
| `frontend/index.html` | Browser chessboard and engine dashboard |
| `setup.py` | Native extension build configuration |

## Run locally

Use Python 3.10 or newer and a working C/C++ compiler supported by Python's `setuptools` (Visual Studio C++ Build Tools on Windows, or `gcc`/`clang` on Linux and macOS).

From this directory:

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python setup.py build_ext --inplace
python -m uvicorn ai_python.server:app --host 127.0.0.1 --port 8000
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). Keep the terminal running while presenting. On Linux or macOS, activate the virtual environment with `source .venv/bin/activate`.

The page currently loads React and Babel from pinned public CDNs, so its browser UI requires internet access. The engine and API run locally. For an offline presentation, vendor those browser dependencies or bundle the frontend before the demo.

PyTorch is kept out of the main install because it is only needed by the optional training script. To train/export evaluator weights, install the extra dependency with `python -m pip install -r requirements-training.txt` after the main requirements.

## Playing and presenting

- You play White; the engine replies as Black.
- Click one of your pieces, then a highlighted legal destination. Promotion choices appear when needed.
- The analysis panel displays the last search's evaluation, depth, nodes, nodes per second, and chosen move.
- Depth and time controls apply to the engine's next reply. Higher values can take longer.
- Move history uses UCI coordinate notation (for example, `e2e4`).
- Use **New game** to reset the board and **Refresh** to reload the current server state.

## API overview

- `GET /` — browser interface
- `GET /state` — board, legal moves, game status, and telemetry
- `POST /move` — apply a legal human move and request an engine reply
- `POST /reset` — start a fresh game
- `POST /fen` — load a FEN position

The server binds to loopback by default in the command above. It is intended as a local project demonstration, not as a hardened public internet service.

## Technical notes

This is an educational prototype. Its strength and rules coverage should be judged against independent chess-engine references before making tournament-strength or complete-rules claims. The included perft script can be used as a development aid once the native extension and its optional Python dependencies are installed.

Run the reference perft checks from this directory with `python ai_python/test_perft.py` after building the extension.
