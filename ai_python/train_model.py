"""Train a small NNUE-shaped evaluator on generated material/PST targets."""
from __future__ import annotations
import argparse
from pathlib import Path
import torch
from torch import nn

EXPECTED_BYTES = (768 * 128 + 128 + 128 + 1) * 4

class ChessNNUE(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.fc1 = nn.Linear(768, 128)
        self.fc2 = nn.Linear(128, 1)
    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.fc2(torch.clamp(self.fc1(x), 0.0, 1.0))

def make_data(count: int, seed: int) -> tuple[torch.Tensor, torch.Tensor]:
    g = torch.Generator().manual_seed(seed)
    x = torch.zeros((count, 768), dtype=torch.float32)
    # Each sample is a valid sparse occupancy-like feature set. Values are white-perspective centipawns.
    material = torch.tensor([100, 320, 335, 500, 900, 0, -100, -320, -335, -500, -900, 0], dtype=torch.float32)
    for i in range(count):
        side_count = int(torch.randint(2, 17, (), generator=g))
        ids = torch.randint(0, 768, (side_count,), generator=g)
        x[i, ids] = 1.0
    weights = material.repeat_interleave(64)
    values = x @ weights
    # Material, piece-square advancement/centralization, mobility proxy, and king shelter targets.
    positional = []
    for piece in range(12):
        sign = 1.0 if piece < 6 else -1.0
        for sq in range(64):
            rank, file = divmod(sq, 8)
            relative_rank = rank if piece < 6 else 7 - rank
            center = max(0.0, 6.0 - abs(3.5 - file) - abs(3.5 - rank))
            kind = piece % 6
            advancement = relative_rank * 7.0 if kind == 0 else 0.0
            mobility = center * (7.0 if kind == 1 else 4.0 if kind == 2 else 2.0 if kind in (3, 4) else 0.0)
            king_shelter = (3.5 - abs(3.5 - file)) * 2.0 if kind == 5 and relative_rank == 0 else 0.0
            positional.append(sign * (advancement + mobility + king_shelter))
    values = values + x @ torch.tensor(positional, dtype=torch.float32)
    values = torch.clamp(values, -2500, 2500).unsqueeze(1)
    return x, values

def export(model: ChessNNUE, output: Path) -> None:
    # C++ accumulates W1 rows for each active feature: [768,128] row-major.
    w1 = model.fc1.weight.detach().cpu().T.contiguous()
    b1 = model.fc1.bias.detach().cpu().contiguous()
    w2 = model.fc2.weight.detach().cpu().reshape(128).contiguous()
    b2 = model.fc2.bias.detach().cpu().reshape(1).contiguous()
    values = torch.cat((w1.reshape(-1), b1, w2, b2)).to(dtype=torch.float32).numpy().astype("<f4", copy=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(values.tobytes())
    assert output.stat().st_size == EXPECTED_BYTES, f"expected {EXPECTED_BYTES} bytes, got {output.stat().st_size}"

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, default=12000)
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("weights.bin"))
    args = parser.parse_args()
    torch.manual_seed(args.seed)
    x, y = make_data(args.samples, args.seed)
    model = ChessNNUE()
    optimizer = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-4)
    loss_fn = nn.SmoothL1Loss()
    for _ in range(args.epochs):
        permutation = torch.randperm(len(x))
        for indices in permutation.split(256):
            optimizer.zero_grad(set_to_none=True)
            loss = loss_fn(model(x[indices]), y[indices])
            loss.backward()
            optimizer.step()
    export(model, args.output)
    print(f"Exported {args.output} ({args.output.stat().st_size} bytes)")

if __name__ == "__main__":
    main()
