"""A second pair of machine ears for assets/music: LAION CLAP (CPU, cached locally) compares every piece with the
descriptions in tools/music.json and with a few plain labels (calm/intense, vocals, genres), and the script measures
event density and loudness itself. Writes work/music_clap.txt.

    G:\\Tools\\PythonSkripts\\AudioGenerator\\venv\\Scripts\\python.exe tools\\clap_music.py

Which cue description fits a piece best tells more than any single caption: a story piece that sits nearest the boss
descriptions is a wrong piece.
"""
import glob
import json
import os

import numpy as np
import soundfile as sf
import torch
from transformers import ClapModel, ClapProcessor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL = "laion/clap-htsat-unfused"

PAIRS = [
    ("calm", "calm, quiet, gentle, slow music", "intense, loud, aggressive, fast action music"),
    ("vocals", "a song with a singer and vocals", "instrumental music without any vocals"),
]
GENRES = [
    "trance", "drum and bass", "techno", "funk", "rock", "heavy metal", "chiptune", "orchestral film score",
    "piano ballad", "synthwave", "industrial", "happy hardcore", "accordion folk music", "latin cumbia",
    "ambient", "breakcore", "jazz",
]


def windows(path, sr=48000, length=10.0, count=3):
    """Up to three 10 s windows (start, middle, end) at 48 kHz mono."""
    x, fs = sf.read(path, always_2d=True)
    mono = x.mean(axis=1)
    if fs != sr:
        t = np.arange(0, len(mono) / fs, 1.0 / sr)
        mono = np.interp(t, np.arange(len(mono)) / fs, mono)
    n = int(length * sr)
    starts = np.linspace(0, max(0, len(mono) - n), count).astype(int)
    return [mono[s:s + n].astype(np.float32) for s in starts], mono, sr


def density(mono, sr):
    """Onsets a second (spectral flux peaks) and the loudness in dBFS RMS."""
    hop, size = 512, 2048
    frames = (len(mono) - size) // hop
    win = np.hanning(size)
    prev = None
    flux = []
    for i in range(frames):
        mag = np.abs(np.fft.rfft(mono[i * hop:i * hop + size] * win))
        if prev is not None:
            flux.append(np.maximum(0.0, mag - prev).sum())
        prev = mag
    flux = np.array(flux)
    thr = np.median(flux) + 1.5 * flux.std()
    peaks = np.sum((flux[1:-1] > thr) & (flux[1:-1] >= flux[:-2]) & (flux[1:-1] >= flux[2:]))
    rms = 20 * np.log10(np.sqrt(np.mean(mono ** 2)) + 1e-9)
    return peaks / (len(mono) / sr), rms


def main():
    torch.set_num_threads(max(1, (os.cpu_count() or 4) // 2))
    spec = json.load(open(os.path.join(ROOT, "tools", "music.json"), encoding="utf-8"))
    cues = {c["id"]: c["style"] for c in spec["cues"]}
    ids = list(cues)
    model = ClapModel.from_pretrained(MODEL).eval()
    proc = ClapProcessor.from_pretrained(MODEL)

    def text(t):
        with torch.no_grad():
            e = model.get_text_features(**proc(text=t, return_tensors="pt", padding=True))
        return torch.nn.functional.normalize(e, dim=-1)

    cue_emb = text([cues[i] for i in ids])
    pair_emb = [text([a, b]) for _, a, b in PAIRS]
    genre_emb = text(["%s music" % g for g in GENRES])
    lines = []
    for path in sorted(glob.glob(os.path.join(ROOT, "assets", "music", "*.ogg"))):
        name = os.path.basename(path)[:-4]
        cue = name[:-2] if name.endswith("_2") else name
        wins, mono, sr = windows(path)
        with torch.no_grad():
            a = model.get_audio_features(**proc(audios=wins, sampling_rate=sr, return_tensors="pt"))
        a = torch.nn.functional.normalize(a, dim=-1).mean(dim=0, keepdim=True)
        a = torch.nn.functional.normalize(a, dim=-1)
        sims = (a @ cue_emb.T)[0]
        order = torch.argsort(sims, descending=True).tolist()
        rank = order.index(ids.index(cue)) + 1
        best = ", ".join(ids[i] for i in order[:3])
        probs = []
        for (label, _, _), e in zip(PAIRS, pair_emb):
            p = torch.softmax(100.0 * (a @ e.T)[0], dim=0)[0].item()
            probs.append("%s %.0f%%" % (label, 100 * p))
        g = torch.softmax(100.0 * (a @ genre_emb.T)[0], dim=0)
        top = torch.argsort(g, descending=True)[:3].tolist()
        genres = ", ".join("%s %.0f%%" % (GENRES[i], 100 * g[i].item()) for i in top)
        ons, rms = density(mono, sr)
        lines.append("%-10s rank %2d/%d  nearest: %-30s  %s  onsets %.1f/s  %.1f dB  genres: %s"
                     % (name, rank, len(ids), best, "  ".join(probs), ons, rms, genres))
        print(lines[-1], flush=True)
    os.makedirs(os.path.join(ROOT, "work"), exist_ok=True)
    with open(os.path.join(ROOT, "work", "music_clap.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
