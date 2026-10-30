"""Speaks Ricky Rattle's recorded lines (data/characters.txt, "character announcer_voice") with Qwen3-TTS from
G:\\Tools\\VoiceLab: a voice designed once per language from a description (kept in tools/voices/ricky_<lang>.flac),
then every line cloned from it, levelled, given a little radio colour and written as
assets/voice/<lang>/ricky_<event>_<n>.ogg.

    set PYTHONNOUSERSITE=1
    G:\\Tools\\VoiceLab\\env-qwen\\python.exe tools\\make_announcer.py [--force]

One GPU user at a time: start it through tools/gpu_queue.py. Needs ffmpeg with libvorbis on the PATH.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS = r"G:\Tools\VoiceLab\models"
REFS = os.path.join(ROOT, "tools", "voices")
WORK = os.path.join(ROOT, "work", "voice")
OUT = os.path.join(ROOT, "assets", "voice")
LANGS = {"de": "German", "en": "English"}
DESCRIBE = {
    "de": "A loud, fast German radio sports commentator in his forties, a slightly hoarse showman's voice, grinning "
          "and theatrical, darkly amused, rolling the words like a fairground barker, excited but clear.",
    "en": "A loud, fast English radio sports commentator in his forties, a slightly hoarse showman's voice, grinning "
          "and theatrical, darkly amused, like a fairground barker, excited but clear.",
}


def lines():
    """{lang: [(event, n, text)]} from the announcer_voice block."""
    out = {"de": [], "en": []}
    text = open(os.path.join(ROOT, "data", "characters.txt"), encoding="utf-8").read()
    block = text.split("character announcer_voice", 1)[1].split("\ncharacter ", 1)[0]
    for raw in block.splitlines():
        m = re.match(r"\s+(say|say_en)\s+(\w+)\s+(.*)$", raw)
        if not m:
            continue
        lang = "en" if m.group(1) == "say_en" else "de"
        for n, said in enumerate(re.findall(r'"([^"]*)"', m.group(3)), 1):
            out[lang].append((m.group(2), n, said))
    return out


def below_normal():
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x00004000)


def plausible(wav, sr, text):
    secs = len(wav) / sr
    return 0.045 * len(text) < secs < 0.13 * len(text) + 1.5


def radio(x, sr):
    """A little of the commentary box: the lows and the very highs gone, a touch of drive."""
    x = sosfilt(butter(2, [160, 7000], btype="bandpass", fs=sr, output="sos"), x)
    return np.tanh(x * 1.6) / np.tanh(1.6)


def loud(x):
    """Roughly -16 LUFS for speech: RMS of the voiced part to -19 dBFS, peaks under -1 dB."""
    frames = x[: len(x) // 1024 * 1024].reshape(-1, 1024)
    rms = np.sqrt(np.mean(frames ** 2, axis=1))
    voiced = rms[rms > rms.max() * 0.1] if len(rms) else rms
    level = np.sqrt(np.mean(voiced ** 2)) if len(voiced) else 1e-3
    y = x * (10 ** (-19 / 20) / max(level, 1e-5))
    peak = np.max(np.abs(y))
    return y * min(1.0, 10 ** (-1 / 20) / max(peak, 1e-6))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    below_normal()
    import torch
    from qwen_tts import Qwen3TTSModel
    todo = lines()
    os.makedirs(REFS, exist_ok=True)
    # The voice, once per language.
    need = [l for l in LANGS if not os.path.exists(os.path.join(REFS, "ricky_%s.flac" % l))]
    if need:
        model = Qwen3TTSModel.from_pretrained(os.path.join(MODELS, "Qwen3-TTS-12Hz-1.7B-VoiceDesign"), device_map="cuda:0",
                                              dtype=torch.bfloat16, attn_implementation="sdpa")
        for lang in need:
            text = " ".join(t for _, _, t in todo[lang][:3])
            for attempt in range(3):
                wavs, sr = model.generate_voice_design(text=[text], instruct=[DESCRIBE[lang]], language=[LANGS[lang]])
                if plausible(wavs[0], sr, text) or attempt == 2:
                    break
            sf.write(os.path.join(REFS, "ricky_%s.flac" % lang), wavs[0], sr)
            open(os.path.join(REFS, "ricky_%s.txt" % lang), "w", encoding="utf-8").write(text + "\n")
            print("designed", lang, flush=True)
        del model
        torch.cuda.empty_cache()
    # Every line in that voice.
    model = Qwen3TTSModel.from_pretrained(os.path.join(MODELS, "Qwen3-TTS-12Hz-1.7B-Base"), device_map="cuda:0",
                                          dtype=torch.bfloat16, attn_implementation="sdpa")
    for lang, items in todo.items():
        ref = os.path.join(REFS, "ricky_%s.flac" % lang)
        prompt = model.create_voice_clone_prompt(ref_audio=ref, ref_text=open(ref[:-5] + ".txt", encoding="utf-8").read().strip())
        os.makedirs(os.path.join(WORK, lang), exist_ok=True)
        os.makedirs(os.path.join(OUT, lang), exist_ok=True)
        for event, n, text in items:
            name = "ricky_%s_%d" % (event, n)
            raw = os.path.join(WORK, lang, name + ".wav")
            if args.force or not os.path.exists(raw):
                for attempt in range(4):
                    wavs, sr = model.generate_voice_clone(text=[text], language=[LANGS[lang]], voice_clone_prompt=prompt,
                                                          max_new_tokens=int(12 * (len(text) / 6.0 + 4)))
                    if plausible(wavs[0], sr, text) or attempt == 3:
                        break
                sf.write(raw, wavs[0], sr)
            x, sr = sf.read(raw, dtype="float32")
            if x.ndim > 1:
                x = x.mean(axis=1)
            x = loud(radio(x, sr))
            fd, tmp = tempfile.mkstemp(suffix=".wav")
            os.close(fd)
            sf.write(tmp, x, sr, subtype="FLOAT")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-c:a", "libvorbis", "-q:a", "4",
                            os.path.join(OUT, lang, name + ".ogg")], check=True, creationflags=0x08000000 if os.name == "nt" else 0)
            os.remove(tmp)
            print(lang, name, "%.1f s" % (len(x) / sr), flush=True)
    del model
    torch.cuda.empty_cache()


if __name__ == "__main__":
    main()
