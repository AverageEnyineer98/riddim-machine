#!/usr/bin/env python3
"""Trains Magic Sound: a conditional VAE over Riddim Forge presets.

The model learns how our own presets are put together (which tables go with which warps,
filters, mod routings and effects, per sound family) and generates new ones from random
points in its latent space, so every click gives a different patch.

Training data comes only from our own recipes (Tools/make_forge_presets.py --training) and
our factory presets. Nothing from any commercial pack is used.

    python3 Tools/make_forge_presets.py --training 8000 --out train.json
    ForgePresetLab --vectors train.json train_vec.json
    ForgePresetLab --vectors Presets/forge_factory.json factory_vec.json
    ForgePresetLab Presets/forge_factory.json analysis.json
    python3 Tools/train_magic.py train_vec.json factory_vec.json Presets/magic_model analysis.json

Writes <out>.json (layout plus each family's sound profile) and <out>.bin (decoder weights,
float32 little-endian), which the plugin compiles in and runs in ForgeMagic.cpp. The profiles
(median spectrum and movement of each family's factory presets) are what Magic Sound checks
a new patch against before it loads it.
"""

import json
import math
import random
import struct
import sys
from pathlib import Path

import numpy as np
import torch
from torch import nn

LATENT = 24
HIDDEN = (256, 512)
SKIP = {"master_gain"}  # set after a test render instead


def load(path):
    return json.loads(Path(path).read_text())


class Layout:
    """Which parameters the model sees and how its output vector is laid out."""

    def __init__(self, params, categories):
        self.params = [p for p in params if p["id"] not in SKIP]
        self.categories = categories
        self.continuous = [i for i, p in enumerate(self.params) if p["choices"] == 0]
        self.choice = [i for i, p in enumerate(self.params) if p["choices"] > 0]
        self.choice_sizes = [self.params[i]["choices"] for i in self.choice]
        self.width = len(self.continuous) + sum(self.choice_sizes)

    def encode(self, rows, all_ids):
        lookup = {pid: k for k, pid in enumerate(all_ids)}
        cols = [lookup[p["id"]] for p in self.params]
        raw = np.array([[row["v"][c] for c in cols] for row in rows], dtype=np.float32)
        cont = raw[:, self.continuous]
        choices = raw[:, self.choice].astype(np.int64)
        onehots = [np.eye(n, dtype=np.float32)[np.clip(choices[:, j], 0, n - 1)] for j, n in enumerate(self.choice_sizes)]
        x = np.concatenate([cont] + onehots, axis=1)
        cat = np.array([self.categories.index(row["category"]) for row in rows], dtype=np.int64)
        return x, cont, choices, cat


class CVAE(nn.Module):
    def __init__(self, layout):
        super().__init__()
        self.layout = layout
        n_cat = len(layout.categories)
        self.encoder = nn.Sequential(nn.Linear(layout.width + n_cat, HIDDEN[1]), nn.ReLU(),
                                     nn.Linear(HIDDEN[1], HIDDEN[0]), nn.ReLU())
        self.mu = nn.Linear(HIDDEN[0], LATENT)
        self.logvar = nn.Linear(HIDDEN[0], LATENT)
        self.dec1 = nn.Linear(LATENT + n_cat, HIDDEN[0])
        self.dec2 = nn.Linear(HIDDEN[0], HIDDEN[1])
        self.out = nn.Linear(HIDDEN[1], layout.width)

    def decode(self, z, cat_onehot):
        h = torch.relu(self.dec1(torch.cat([z, cat_onehot], 1)))
        h = torch.relu(self.dec2(h))
        return self.out(h)

    def forward(self, x, cat_onehot):
        h = self.encoder(torch.cat([x, cat_onehot], 1))
        mu, logvar = self.mu(h), self.logvar(h)
        z = mu + torch.randn_like(mu) * torch.exp(0.5 * logvar)
        return self.decode(z, cat_onehot), mu, logvar


def split_heads(layout, logits):
    nc = len(layout.continuous)
    cont = torch.sigmoid(logits[:, :nc])
    heads, start = [], nc
    for n in layout.choice_sizes:
        heads.append(logits[:, start:start + n])
        start += n
    return cont, heads


def loss_terms(layout, logits, cont, choices):
    pred_cont, heads = split_heads(layout, logits)
    mse = ((pred_cont - cont) ** 2).sum(1).mean()
    ce = sum(nn.functional.cross_entropy(h, choices[:, j], reduction="none") for j, h in enumerate(heads)).mean()
    return mse * 8.0 + ce


def evaluate(model, layout, x, cont, choices, cat_onehot):
    """Reconstruction accuracy through the mean latent: choice accuracy and continuous error."""
    model.eval()
    with torch.no_grad():
        h = model.encoder(torch.cat([x, cat_onehot], 1))
        logits = model.decode(model.mu(h), cat_onehot)
        pred_cont, heads = split_heads(layout, logits)
        acc = np.mean([(h.argmax(1) == choices[:, j]).float().mean().item() for j, h in enumerate(heads)])
        mae = (pred_cont - cont).abs().mean().item()
    model.train()
    return acc, mae


def family_profiles(analysis_path, categories):
    analysis = load(analysis_path)
    profiles = {}
    for category in categories:
        items = [a for a in analysis if a["category"] == category]
        bands = np.array([a["bands"] for a in items])
        profiles[category] = {
            "bands": np.median(bands, 0).round(2).tolist(),
            "spread": np.maximum(2.0, np.median(np.abs(bands - np.median(bands, 0)), 0) * 1.4826).round(2).tolist(),
            "movement": float(np.median([a["movement"] for a in items])),
        }
    return profiles


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)

    train, factory = load(sys.argv[1]), load(sys.argv[2])
    out = Path(sys.argv[3])
    all_ids = [p["id"] for p in train["params"]]
    categories = sorted({row["category"] for row in train["presets"]})
    layout = Layout(train["params"], categories)

    torch.manual_seed(1)
    random.seed(1)
    x, cont, choices, cat = (torch.tensor(a) for a in layout.encode(train["presets"], all_ids))
    factory_rows = [r for r in factory["presets"] if r["category"] in categories]
    fx, fcont, fchoices, fcat = (torch.tensor(a) for a in layout.encode(factory_rows, all_ids))
    eye = torch.eye(len(categories))

    model = CVAE(layout)
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    epochs, batch = 80, 128
    n = len(x)
    print(f"{n} training presets, {len(layout.params)} parameters -> {layout.width} outputs, {len(categories)} families")

    for epoch in range(epochs):
        perm = torch.randperm(n)
        beta = min(1.0, epoch / 30) * 0.5
        total = 0.0
        for start in range(0, n, batch):
            idx = perm[start:start + batch]
            logits, mu, logvar = model(x[idx], eye[cat[idx]])
            rec = loss_terms(layout, logits, cont[idx], choices[idx])
            kl = (-0.5 * (1 + logvar - mu ** 2 - logvar.exp()).sum(1)).mean()
            loss = rec + beta * kl
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += loss.item() * len(idx)
        if epoch % 10 == 9 or epoch == epochs - 1:
            acc, mae = evaluate(model, layout, fx, fcont, fchoices, eye[fcat])
            print(f"epoch {epoch + 1}: loss {total / n:.3f}  factory recon: choice acc {acc * 100:.1f}%  value error {mae:.3f}")

    # How varied are fresh samples? Count distinct oscillator A tables over 200 draws.
    model.eval()
    with torch.no_grad():
        z = torch.randn(200, LATENT)
        c = eye[torch.randint(len(categories), (200,))]
        _, heads = split_heads(layout, model.decode(z, c))
    table_head = [layout.params[i]["id"] for i in layout.choice].index("fA_table")
    distinct = len(set(heads[table_head].argmax(1).tolist()))
    print(f"200 random samples use {distinct} different Osc A tables")

    # Export the decoder.
    weights = []
    shapes = []
    for layer in (model.dec1, model.dec2, model.out):
        w = layer.weight.detach().numpy().astype(np.float32)  # (out, in)
        b = layer.bias.detach().numpy().astype(np.float32)
        weights += [w.ravel(), b.ravel()]
        shapes.append([int(w.shape[0]), int(w.shape[1])])
    blob = np.concatenate(weights).astype("<f4").tobytes()
    out.with_suffix(".bin").write_bytes(blob)

    spec = {
        "latent": LATENT,
        "categories": categories,
        "layers": shapes,
        "continuous": [layout.params[i]["id"] for i in layout.continuous],
        "choices": [{"id": layout.params[i]["id"], "n": layout.params[i]["choices"]} for i in layout.choice],
        "profiles": family_profiles(sys.argv[4], categories),
    }
    out.with_suffix(".json").write_text(json.dumps(spec))
    print(f"wrote {out.with_suffix('.bin')} ({len(blob) / 1e6:.2f} MB) and {out.with_suffix('.json')}")


if __name__ == "__main__":
    main()
