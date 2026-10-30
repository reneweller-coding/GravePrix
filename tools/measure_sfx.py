"""Measures the synthesised sounds (GraveSfx work/sfx): peak, loudness, DC, clipping, the seams of the loops, clicks,
and for the engines the level and pitch along the rev sweep. Optionally draws spectrograms.

    python tools/measure_sfx.py work/sfx [--spectra work/sfx_spectra]

Loudness: K-weighted (BS.1770) momentary loudness; "maxM" is the loudest 400 ms block, which is what counts for short
effects. "burst" plays a gun 12 times a second for a second (the trigger held down).
Needs numpy and scipy (and Pillow for --spectra); no soundfile.
"""
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter


def biquads(fs):
    """The K-weighting of BS.1770 as two RBJ biquads (a high shelf of +4 dB above ~1.5 kHz, a highpass at 38 Hz)."""
    a = 10 ** (4.0 / 40)
    w = 2 * np.pi * 1500 / fs
    al = np.sin(w) / (2 * 0.7071)
    c = np.cos(w)
    b1 = [a * ((a + 1) + (a - 1) * c + 2 * np.sqrt(a) * al), -2 * a * ((a - 1) + (a + 1) * c),
          a * ((a + 1) + (a - 1) * c - 2 * np.sqrt(a) * al)]
    a1 = [(a + 1) - (a - 1) * c + 2 * np.sqrt(a) * al, 2 * ((a - 1) - (a + 1) * c),
          (a + 1) - (a - 1) * c - 2 * np.sqrt(a) * al]
    w = 2 * np.pi * 38 / fs
    al = np.sin(w) / (2 * 0.5)
    c = np.cos(w)
    b2 = [(1 + c) / 2, -(1 + c), (1 + c) / 2]
    a2 = [1 + al, -2 * c, 1 - al]
    return (b1, a1), (b2, a2)


def kweight(x, fs):
    (b1, a1), (b2, a2) = biquads(fs)
    return lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)


def momentary(x, fs):
    """Momentary loudness (400 ms blocks, 100 ms hop) in LUFS."""
    y = kweight(x, fs)
    block, hop = int(0.4 * fs), int(0.1 * fs)
    if len(y) < block:
        y = np.concatenate([y, np.zeros((block - len(y), y.shape[1]))])
    c = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y ** 2, axis=0)])
    starts = np.arange(0, len(y) - block + 1, hop)
    p = ((c[starts + block] - c[starts]) / block).sum(axis=1)
    return -0.691 + 10 * np.log10(np.maximum(p, 1e-12))


def integrated(x, fs):
    """Integrated loudness (absolute and relative gate)."""
    m = momentary(x, fs)
    p = 10 ** ((m + 0.691) / 10)
    p = p[m > -70]
    if not len(p):
        return -70.0
    rel = -0.691 + 10 * np.log10(p.mean()) - 10
    g = p[-0.691 + 10 * np.log10(p) > rel]
    return -0.691 + 10 * np.log10(g.mean())


def db(v):
    return 20 * np.log10(max(1e-9, float(v)))


def read(path):
    fs, x = wavfile.read(path)
    if x.dtype == np.int16:
        x = x.astype(np.float64) / 32768.0
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = x[:, None]
    return fs, x


def clicks(x):
    """The worst jump of the second difference against its neighbourhood (a click stands far out of it)."""
    m = x.mean(axis=1)
    d2 = np.abs(np.diff(m, 2))
    if len(d2) < 4096:
        return 0.0
    w = 1024
    c = np.concatenate([[0.0], np.cumsum(np.pad(d2, w) ** 2)])
    local = np.sqrt((c[2 * w + 1:] - c[:-2 * w - 1]) / (2 * w + 1))[: len(d2)] + 1e-5
    return float(np.max(d2 / local))


def seam(x):
    """How the loop's end meets its start: where the step over the seam, and the error of carrying the slope on over
    it, rank among all the steps inside the loop (percent; a click at the seam would rank at 100)."""
    d1 = np.abs(np.diff(x, axis=0))
    d2 = np.abs(np.diff(x, 2, axis=0))
    jump = np.abs(x[0] - x[-1])
    pred = np.abs(x[0] - (2 * x[-1] - x[-2]))
    r1 = max(float((d1[:, c] < jump[c]).mean()) for c in range(x.shape[1]))
    r2 = max(float((d2[:, c] < pred[c]).mean()) for c in range(x.shape[1]))
    return 100 * r1, 100 * r2


def pitch(seg, fs):
    """A rough fundamental by autocorrelation (45..1200 Hz)."""
    s = seg - seg.mean()
    if np.sqrt(np.mean(s ** 2)) < 1e-4:
        return 0.0
    n = len(s)
    f = np.fft.rfft(s, 2 * n)
    ac = np.fft.irfft(f * np.conj(f))[:n]
    lo, hi = int(fs / 1200), int(fs / 45)
    return fs / (lo + int(np.argmax(ac[lo:hi])))


def centroid(seg, fs):
    s = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1 / fs)
    return float((s * f).sum() / max(1e-9, s.sum()))


def spectrogram(x, fs, path, fmax=8000):
    """A spectrogram as a picture (time to the right, 0..fmax upwards, 80 dB of range)."""
    from PIL import Image
    m = x.mean(axis=1)
    n, hop = 2048, 256
    win = np.hanning(n)
    frames = [np.abs(np.fft.rfft(m[i:i + n] * win)) for i in range(0, len(m) - n, hop)]
    if not frames:
        return
    s = 20 * np.log10(np.array(frames).T + 1e-9)
    s = s[: int(fmax / (fs / n))][::-1]
    s = np.clip((s - (s.max() - 80)) / 80, 0, 1)
    Image.fromarray((s * 255).astype(np.uint8)).resize((min(1600, s.shape[1]), 400)).save(path)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    spectra = None
    if "--spectra" in sys.argv:
        spectra = sys.argv[sys.argv.index("--spectra") + 1]
        args.remove(spectra)
        os.makedirs(spectra, exist_ok=True)
    folder = args[0] if args else "work/sfx"
    names = sorted(n for n in os.listdir(folder) if n.endswith(".wav"))
    print("%-22s %5s %2s %6s %6s %6s %6s %6s %s" % ("name", "s", "ch", "peak", "maxM", "LUFS", "DC", "click", "seam"))
    for name in names:
        fs, x = read(os.path.join(folder, name))
        base = name[:-4]
        clip = int((np.abs(x) >= 0.999).sum())
        sm = "%.0f%%/%.0f%%" % seam(x) if base.startswith("loop_") else ""
        print("%-22s %5.2f %2d %6.1f %6.1f %6.1f %6.1f %6.0f %s%s" % (
            base, len(x) / fs, x.shape[1], db(np.abs(x).max()), momentary(x, fs).max(), integrated(x, fs),
            db(np.abs(x.mean(axis=0)).max()), clicks(x), sm, "  CLIP %d" % clip if clip else ""))
        if spectra:
            spectrogram(x, fs, os.path.join(spectra, base + ".png"))
    print()
    for gun in ("mg", "twinmg"):
        p = os.path.join(folder, gun + ".wav")
        if os.path.exists(p):
            fs, x = read(p)
            y = np.zeros((fs + len(x), x.shape[1]))
            for k in range(12):
                i = int(k * fs / 12)
                y[i:i + len(x)] += x
            print("burst %-16s peak %6.1f  maxM %6.1f" % (gun, db(np.abs(y).max()), momentary(y, fs).max()))
    print()
    print("%-9s %s" % ("engine", "segment RMS dBFS / pitch Hz / centroid Hz"))
    segs = [("idle", 0.3, 0.9), ("wot25", 1.55, 1.75), ("wot50", 2.2, 2.4), ("wot90", 3.25, 3.45),
            ("limit", 3.7, 3.95), ("off70", 4.5, 4.7), ("off20", 5.5, 5.7)]
    for name in names:
        if not name.startswith("engine_"):
            continue
        fs, x = read(os.path.join(folder, name))
        m = x.mean(axis=1)
        out = []
        for label, a, b in segs:
            s = m[int(a * fs):int(b * fs)]
            out.append("%s %5.1f/%4.0f/%4.0f" % (label, db(np.sqrt(np.mean(s ** 2))), pitch(s, fs), centroid(s, fs)))
        print("%-9s %s" % (name[7:-4], " ".join(out)))


if __name__ == "__main__":
    main()
