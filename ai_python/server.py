"""FastAPI service for a local human-versus-engine chess game."""
from __future__ import annotations
from pathlib import Path
import time
from threading import RLock
from typing import Any
from typing import Literal
import chess_engine
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field

ROOT = Path(__file__).resolve().parents[1]
app = FastAPI(title="Chess Engine AI", version="1.0.0", docs_url=None, redoc_url=None, openapi_url=None)
engine = chess_engine.SearchEngine()
engine_lock = RLock()
history: list[str] = []
telemetry: dict[str, Any] = {"score_cp": 0, "depth": 0, "nodes": 0, "nps": 0, "best_move": ""}

@app.middleware("http")
async def security_headers(request, call_next):
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Referrer-Policy"] = "no-referrer"
    response.headers["Permissions-Policy"] = "camera=(), microphone=(), geolocation=()"
    response.headers["Cache-Control"] = "no-store"
    return response

class MoveRequest(BaseModel):
    model_config = {"extra": "forbid"}
    source: int | None = Field(default=None, ge=0, le=63)
    target: int | None = Field(default=None, ge=0, le=63)
    promotion: Literal["q", "r", "b", "n"] = "q"
    uci: str | None = Field(default=None, min_length=4, max_length=5, pattern=r"^[a-h][1-8][a-h][1-8][qrbn]?$")
    max_depth: int = Field(default=6, ge=1, le=12)
    time_limit_ms: int = Field(default=1000, ge=1, le=10000)

class FenRequest(BaseModel):
    model_config = {"extra": "forbid"}
    fen: str = Field(min_length=1, max_length=127)

def state() -> dict[str, Any]:
    board = list(engine.get_board_array())
    return {"board": board, "side_to_move": "white" if engine.get_side_to_move() == 0 else "black",
            "in_check": bool(engine.is_in_check()), "game_status": engine.get_game_status(),
            "legal_moves": list(engine.get_legal_moves()), "ai_telemetry": dict(telemetry),
            "fen": engine.get_fen(), "move_history": list(history)}

@app.get("/", response_class=HTMLResponse)
def index() -> HTMLResponse:
    path = ROOT / "frontend" / "index.html"
    return HTMLResponse(path.read_text(encoding="utf-8"))

@app.get("/state")
def get_state() -> dict[str, Any]:
    with engine_lock:
        return state()

@app.post("/reset")
def reset() -> dict[str, Any]:
    with engine_lock:
        engine.reset(); history.clear(); telemetry.update(score_cp=0, depth=0, nodes=0, nps=0, best_move="")
        return state()

@app.post("/fen")
def set_fen(body: FenRequest) -> dict[str, Any]:
    if len(body.fen.split()) != 6:
        raise HTTPException(422, "FEN must contain six fields")
    with engine_lock:
        try:
            engine.load_fen(body.fen)
        except ValueError as exc:
            raise HTTPException(422, "Invalid FEN") from exc
        history.clear(); telemetry.update(score_cp=0, depth=0, nodes=0, nps=0, best_move="")
        return state()

@app.post("/move")
def move(body: MoveRequest) -> dict[str, Any]:
    if bool(body.uci) == (body.source is not None or body.target is not None):
        raise HTTPException(422, "Provide either uci or both source and target")
    if not body.uci and (body.source is None or body.target is None):
        raise HTTPException(422, "Provide either uci or both source and target")
    with engine_lock:
        if engine.get_game_status() != "PLAYING":
            return state()
        before = time.perf_counter()
        if body.uci:
            ok = engine.apply_uci(body.uci)
            human_uci = body.uci
        else:
            promotion_move = any(m["source"] == body.source and m["target"] == body.target and m["promotion"] for m in engine.get_legal_moves())
            ok = engine.apply_human_move(body.source, body.target, body.promotion)
            sq = lambda n: chr(97+n%8)+str(n//8+1)
            human_uci = sq(body.source)+sq(body.target)
            if promotion_move:
                human_uci += body.promotion
        if not ok:
            raise HTTPException(400, "Illegal move")
        history.append(human_uci)
        if engine.get_game_status() == "PLAYING":
            ai_side = engine.get_side_to_move()
            result = engine.get_best_move(body.max_depth, body.time_limit_ms)
            if engine.apply_uci(result["uci"]):
                history.append(result["uci"])
            elapsed = max(time.perf_counter()-before, 1e-6)
            white_score = result["score_cp"] if ai_side == 0 else -result["score_cp"]
            telemetry.update(score_cp=white_score, depth=result["depth"], nodes=result["nodes"],
                             nps=int(result["nodes"]/elapsed), best_move=result["uci"])
        return state()
