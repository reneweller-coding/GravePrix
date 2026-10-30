"""Paints the tabloid's press photos (Das Sensenblatt) locally with FLUX.2 in WSL (SpaceOpera's tools/flux_wsl.py)
and keeps them as assets/news/<kind>_<n>.jpg: black and white, grainy, 640 pixels wide. Fictional people only, no
text, no logos.

    python tools/make_news_photos.py [--force]

One GPU user at a time: start it through tools/gpu_queue.py.
"""
import argparse
import json
import os
import subprocess

from PIL import Image, ImageFilter, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work", "news")
OUT = os.path.join(ROOT, "assets", "news")
FLUX = "/mnt/g/Tools/Games/SpaceOpera/tools/flux_wsl.py"

STYLE = ("1950s tabloid press photograph, black and white, heavy film grain, harsh flash, darkly comic, fictional "
         "people, no text, no letters, no numbers, no logos, no signs, no flags")

PHOTOS = {
    "crash": "A battered home-made race car crashed nose-first into a stack of old tyres beside a dirt race track, smoke "
             "rising from its bonnet, a marshal in a black undertaker's suit and top hat shrugging next to it, night.",
    "funeral": "A funeral procession on a race track: a vintage hearse followed by dented rusty race cars at walking "
               "pace, mourners in black with umbrellas along the fence, a wreath shaped like a steering wheel.",
    "victory": "A grinning race driver in a scorched leather jacket and goggles standing on the bonnet of a dented race "
               "car, holding up a big funeral wreath like a trophy, confetti, a cheering crowd behind a fence.",
    "defeat": "A dejected race driver sitting on a kerb next to a smoking wrecked race car, head in hands, a tow truck "
              "arriving in the background, puddles.",
    "shark": "A loan shark in a pinstripe suit with a toothy grin counting banknotes at a desk in a dim back office, two "
             "burly men with wrenches standing behind him, cigar smoke.",
    "dealer": "A shady used car lot at dusk under strings of light bulbs, a saleswoman in leopard print presenting a "
              "battered armoured race car with spikes on its bumper, blank price tags on the windscreens.",
    "sabotage": "A hooded mechanic sneaking under a race car in a dark garage at night, cutting a cable with pliers, a "
                "torch in his teeth.",
    "feud": "Two race drivers in leather jackets forehead to forehead, shouting at each other in a pit lane, their "
            "dented race cars behind them, mechanics holding them back.",
    "league": "An award ceremony at a race track: an undertaker in a top hat shaking hands with a race driver on a "
              "winners' podium built from coffins, flash bulbs, a crowd in black.",
    "crowd": "Spectators in black coats with umbrellas pressed against a chain-link fence at a dirt race track at "
             "night, race cars blurring past under floodlights, sparks flying.",
}


def linux(path):
    path = os.path.abspath(path).replace("\\", "/")
    return "/mnt/%s/%s" % (path[0].lower(), path[3:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    tasks = []
    for kind, prompt in PHOTOS.items():
        for n in (1, 2):
            raw = os.path.join(WORK, "%s_%d.png" % (kind, n))
            if args.force and os.path.exists(raw):
                os.remove(raw)
            tasks.append({"prompt": prompt + " " + STYLE, "out": raw.replace("\\", "/"), "width": 1024, "height": 768,
                          "seed": 40 + n * 7 + len(kind)})
    todo = [t for t in tasks if not os.path.exists(t["out"])]
    if todo:
        path = os.path.join(WORK, "tasks.json")
        json.dump(todo, open(path, "w", encoding="utf-8"), indent=1)
        r = subprocess.run(["wsl", "-d", "Ubuntu-22.04", "--", "/home/rene/miniconda3/envs/flux/bin/python", FLUX, linux(path)],
                           creationflags=0x08004000 if os.name == "nt" else 0)
        if r.returncode:
            raise SystemExit("flux failed (%d)" % r.returncode)
    for t in tasks:
        if not os.path.exists(t["out"]):
            print("missing", t["out"])
            continue
        name = os.path.splitext(os.path.basename(t["out"]))[0]
        img = ImageOps.grayscale(Image.open(t["out"]))
        img = ImageOps.autocontrast(img, cutoff=1).resize((640, 480), Image.LANCZOS).filter(ImageFilter.SHARPEN)
        img.save(os.path.join(OUT, name + ".jpg"), quality=86)
        print(name)


if __name__ == "__main__":
    main()
