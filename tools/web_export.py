"""Exports the game for the web (WebAssembly, runs in the visitor's browser). Guide: docs/web.md.

    python tools/web_export.py            # -> build/web_export/ (index.html + files)
    python -m http.server -d build/web_export 8000   # then open http://localhost:8000

1. Builds the extension with Emscripten (the C++ -> WebAssembly compiler). It
   must be the version Godot's web templates were built with: 4.0.20 for
   Godot 4.7.2 (the browser console prints it: "Build configuration:
   Emscripten ..."). Expected in ../emsdk (next to the repo) or $EMSDK.
2. Copies the files the game reads from the repo into godot/repo/ (where
   Session.repo_root() points on the web), then Godot's export packs them.
3. Exports with the "Web" preset (godot/export_presets.cfg) and removes godot/repo/.
Needs Godot's export templates installed (Editor > Manage Export Templates).
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMSDK = Path(os.environ.get("EMSDK", ROOT.parent / "emsdk"))
OUT = ROOT / "build" / "web_export"
PACK = [  # everything the game reads (main.gd, menu.gd), relative to the repo
    "assets/characters/**/*", "assets/maps/*.png", "assets/effects/explosion_1.png",
    "data/moves/*.json", "data/bots/*.bin",
    "runs/*/agent_latest.bin", "runs/*/summary.json",  # "Latest bot" and the Training screen
    "replays/**/*.json",
]


def godot():
    exe = os.environ.get("GODOT") or next(Path.home().glob("Downloads/**/Godot_v4*_console.exe"), None)
    if not exe:
        sys.exit("Godot not found: set GODOT to Godot's _console.exe")
    return str(exe)


def main():
    toolchain = EMSDK / "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
    if not toolchain.exists():
        sys.exit(f"Emscripten not found in {EMSDK} (set EMSDK): see docs/web.md")
    build = ROOT / "build" / "web"
    if not (build / "CMakeCache.txt").exists():
        subprocess.run([sys.executable, "-m", "cmake", "-S", ROOT / "bindings/godot", "-B", build, "-G", "Ninja",
                        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DGODOTCPP_TARGET=template_release",
                        "-DGODOTCPP_THREADS=OFF", "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run([sys.executable, "-m", "cmake", "--build", build, "--parallel"], check=True)

    pack = ROOT / "godot" / "repo"
    shutil.rmtree(pack, ignore_errors=True)
    try:
        mtimes = {}  # packed files lose their dates; the game sorts by them (Session.modified_time)
        for pattern in PACK:
            for f in ROOT.glob(pattern):
                if f.is_file():
                    rel = f.relative_to(ROOT).as_posix()
                    dst = pack / rel
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(f, dst)
                    mtimes[rel] = int(f.stat().st_mtime)
                    if f.suffix == ".png":  # keep the raw .png (Image.load_from_file), don't import it as a texture
                        dst.with_name(dst.name + ".import").write_text('[remap]\n\nimporter="keep"\n')
        (pack / "mtimes.json").write_text(json.dumps(mtimes))
        shutil.rmtree(OUT, ignore_errors=True)
        OUT.mkdir(parents=True)
        subprocess.run([godot(), "--headless", "--path", ROOT / "godot", "--export-release", "Web",
                        OUT / "index.html"], check=True)
    finally:
        shutil.rmtree(pack, ignore_errors=True)
    print(f"exported to {OUT}")


if __name__ == "__main__":
    main()
