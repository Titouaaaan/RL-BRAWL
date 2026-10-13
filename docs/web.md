# The game in a browser (web export)

The whole game, exactly as on desktop, as a folder of static files that runs
in the visitor's browser. The web server only sends files: the simulation,
the bots (their neural networks) and the drawing all run on the visitor's
machine, so hosting costs nothing beyond bandwidth, and any number of people
can watch or play at once.

Checked in Chromium (Chrome, Edge, Brave...), Firefox and WebKit (the engine
of Safari) on Windows. Real Safari on a Mac or iPhone hasn't been tried yet.

## Quick start

```
python tools/web_export.py                          # -> build/web_export/
python -m http.server -d build/web_export 8000      # serve it on this PC
```

Open http://localhost:8000. The first load takes a few seconds (about 17 MB
compressed, see [Files](#files)). Opening `index.html` directly from disk
doesn't work: browsers only run WebAssembly fetched over HTTP.

## What's in it

Everything the desktop menu has: Player vs Bot (all levels, including
"Latest bot"), Player vs Player (both players on one keyboard, or
controllers), Bot vs Bot, Character Select, Controls, Replays (every
replay in `replays/`), Training (every run's charts and stats). Settings
(character choices) are saved in the browser.

The few differences come from the browser itself:

| Desktop | Web | Why |
|---|---|---|
| Quit button, Esc on the main menu quits | not shown | Quitting would only freeze the page. |
| Training screen rebuilds summaries (`run_summary.py`) | shows the summaries as they were at export time | No Python in a browser. |
| Command-line options (`play.bat --vs fighter`, `replay FILE`, `--p1 3`) | the menu equivalents | No command line. The scripted bots (fighter, spacer...) are only reachable from the command line on desktop too. |
| Files read live from the repo | a snapshot packed at export time | See [Updating](#updating-the-content). |

Mouse, keyboard and controllers all work. A controller that is connected but
not in use can still steer the menu if it's sending input (a held trigger, a
drifting stick): see [Troubleshooting](#troubleshooting). Phones and tablets
can watch (Bot vs Bot, Replays), but there are no on-screen controls to play.

## For the agent putting this on a website: start here

You don't need to build anything or know the game. What you need to know:

- **The files.** The finished web version is the folder `build/web_export/`
  (`index.html` plus about 10 files). It is **not in git**: `build/` is ignored,
  and so are the training runs and replays it packs. It is built on the
  owner's Windows training PC (`D:\RL-BRAWL\build\web_export\`) by
  `python tools/web_export.py --deploy`, which also copies it to the website
  machine (the owner's Raspberry Pi 5: Next.js frontend + FastAPI backend).
  Don't build it on the server: that needs the toolchain below plus the PC's
  `runs/` and `replays/` folders.
- **Where it lands:** the folder `~/rl-brawl-web/` on the Pi (whatever
  `WEB_DEPLOY` says, see [Deploying](#deploying-from-the-pc)). Each deploy
  replaces the whole folder at once (copied next to it, then swapped), so
  **serve that folder directly and don't put your own files in it.**
- **Serve it as a static route, outside the Next.js build**, so that a deploy
  is live immediately with nothing to rebuild or restart:
  - in the FastAPI backend: `app.mount("/rl-brawl",
    StaticFiles(directory="/home/titouan/rl-brawl-web", html=True),
    name="rl-brawl")`. `StaticFiles` re-reads the folder on every request,
    so a deploy that swaps it is picked up at once;
  - or in the reverse proxy in front of the site, if there is one (nginx:
    `location /rl-brawl/ { alias /home/titouan/rl-brawl-web/; }`).

  (Avoid copying it into Next.js `public/`: `next start` only serves files
  that were there at `next build`, so every game update would need a rebuild.)
  Then make `/rl-brawl/` reachable from the site's domain (same origin or
  not: the iframe works either way).

  The owner's site does it the FastAPI way, under a different path and with
  a few header changes: see [On titouanguerin.com](#on-titouanguerincom).
- **HTTPS is required.** Godot's web build only starts in a "secure context":
  a page served over `https://`, or from `localhost`. Over plain `http://`
  to an IP or hostname it stops with "Secure Context - Check web server
  configuration (use HTTPS)". If the site is already HTTPS and the game is
  served from the same domain, nothing to do. To test on the Pi before it's
  public, tunnel it to the PC's localhost: `ssh -N -L 8767:localhost:<port>
  <user>@<pi>` and open http://localhost:8767/.
- **The page component** (Next.js, App Router or Pages, no client code needed):
  ```tsx
  export default function RlBrawl() {
    return (
      <iframe
        src="/rl-brawl/index.html"
        title="RL-BRAWL"
        style={{ width: "100%", aspectRatio: "16 / 9", border: 0 }}
        allow="fullscreen; gamepad; autoplay"
      />
    );
  }
  ```
- **How to check it works:** open the page and the browser console (F12). It
  should print `Godot Engine v4.7.2` and `Build configuration: Emscripten
  4.0.20, single-threaded, GDExtension support`, with no red errors except
  WebKit's harmless `glBlitFramebuffer` ones. Then in the game: Bot vs Bot >
  Start: two fighters move and fight.

The next section has the details: serving requirements (MIME types,
compression, caching, headers).

## Putting it on a website

The output is plain static files. Any web server or static host works; no
server-side code or database is needed, and the game asks for no special
headers. But a site that already sends security headers can block it: a
`Content-Security-Policy` must let WebAssembly compile, and anti-framing
headers must let the site frame its own game (see
[On titouanguerin.com](#on-titouanguerincom) for the exact list).

1. **Serve** the exported folder under a path such as `/rl-brawl/`
   (`--deploy` puts it on the server; see the start-here section for where to
   serve it from). Keep the files together; they load each other by relative
   path.
2. **Embed** it in a page with an iframe. The game fills whatever size it's
   given (it resizes with the frame; 16:9 looks best):
   ```html
   <iframe src="/rl-brawl/index.html" title="RL-BRAWL"
           style="width: 100%; aspect-ratio: 16 / 9; border: 0"
           allow="fullscreen; gamepad; autoplay"></iframe>
   ```
   `gamepad` lets controllers work inside the frame. Keyboard input goes to the
   game once the visitor has clicked into it (browsers give keys to the
   focused frame). Linking straight to `index.html` (full page) also works.
3. **Check the server sends the right types:** `.wasm` as `application/wasm`
   (browsers refuse to compile it otherwise), `.pck` as anything (e.g.
   `application/octet-stream`). Next.js, nginx, Caddy and Python's
   `http.server` already do this.
4. **Compression** (recommended): 65 MB raw becomes about 17 MB with gzip or
   Brotli. Most servers compress automatically (Next.js with `next start`:
   gzip on by default; behind nginx: `gzip on; gzip_types application/wasm
   application/octet-stream application/javascript;`).
5. **Caching:** the file names never change between exports, so after
   uploading a new export, browsers may keep the old one. Either serve
   `/rl-brawl/` with `Cache-Control: no-cache` (the browser re-checks, and
   only downloads what changed), or put each export in a new folder
   (`/rl-brawl/2026-10-13/`).

Needed: HTTPS (or `localhost`), see the start-here section. Not needed: the `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy`
headers that many Godot web guides mention. They are only for the
multi-threaded build; this one is single-threaded on purpose.

## On titouanguerin.com

What the owner's site actually needed, so changes on this side can be
checked against it. Status: built and tested in a development copy of the
site on 2026-10-13, not live yet. The site's own repo documents its side in full;
this is the part that touches the game.

**Addresses.** The page is `/rl-brawl` (a normal page of the site: title,
sections, a note that phones and tablets can only watch, then the game in an
iframe). The game's files are under `/rl-brawl/play/`, so the iframe loads
`/rl-brawl/play/index.html`. (Not `/rl-brawl/` as in the examples above:
that's the page's own address.)

**Serving.** The site's FastAPI backend mounts `~/rl-brawl-web/` with
`StaticFiles` at `/rl-brawl/play`, and Next.js forwards `/rl-brawl/play/*` to
it, the same way it already forwards the site's API. A `--deploy` is
therefore live at once, with no site rebuild. The mount uses
`check_dir=False`, so the backend still starts when the folder is missing
(the frame then shows "Not Found").

**Headers, set by the backend.** Next.js does not add its configured headers
to responses it forwards, so the backend sets the game's own:

| Header | Value | Why |
|---|---|---|
| `Content-Security-Policy` | `script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'` | Without `'wasm-unsafe-eval'` the browser refuses to compile any `.wasm` and the game never starts. `'unsafe-inline'`: the loader script inside `index.html`. |
| | `img-src 'self' data: blob:` | Godot builds some images in memory (`blob:` URLs); blocked, it logs a CSP error. |
| | `frame-ancestors 'self'` | Only the site's own page may frame the game (not other sites, on the Pi's bandwidth). |
| | `default-src 'self'` | Everything else (the `.pck`, `.wasm`, audio worklets) must come from the same folder. |
| `X-Frame-Options` | `SAMEORIGIN` | Same as `frame-ancestors`, for older browsers. |
| `Cache-Control` | `no-cache` | File names never change between exports: browsers re-check every file and get a `304` (no download) when it hasn't changed. |

The site's own pages gained `frame-src 'self'` so the page may frame the game.

**Compression, also by the backend** (gzip): Next.js only compressed the
`.wasm` and `.js`, not the 16 MB `.pck` (`application/octet-stream`). With
it, a first visit downloads 16.7 MB; the 44 MB engine takes about 3 s to
compress on the Pi.

**Checked** (2026-10-13, headless Chromium on the Pi): the page boots the
game (console: Godot 4.7.2, `single-threaded, GDExtension support`, no CSP
errors), Bot vs Bot with Latest bot on both sides plays.

**Small frames (fixed):** the game's menus are laid out in fixed pixels for a
1280 x 720 window, so in a frame much smaller than that (the site's content
column was about 845 x 475) the main menu got cut off below *Replays*. Fixed
on the site's side (2026-10-13), in its frontend; the game is unchanged. If
another site embeds it, give the frame enough room, or turn on Godot's
stretch mode (Project Settings > Display > Window > Stretch: `canvas_items`,
aspect `expand`), which scales everything to the frame but also stops
desktop full screen from using the extra space.

**What would need a change on the site side** (tell the owner, it's not
automatic):
- loading anything from another domain (fonts, scripts, a CDN): the CSP only
  allows the site itself;
- the multi-threaded build (it needs the COOP/COEP headers above);
- a different folder name than `rl-brawl-web`, or the entry page no longer
  being `index.html` at the folder's root;
- turning on Godot's PWA / service worker export option.

Anything else (new characters, bots, replays, code) is just a re-export with
`--deploy`.

## Deploying from the PC

```
python tools/web_export.py --deploy
```

builds the export, then copies it to the server over SSH: `scp` the folder as
`<folder>.new`, then on the server move the old one away and the new one in,
so a visitor never loads half old, half new files. About 17 MB goes over the
network (scp compresses).

One-time setup on the PC:
1. Where to deploy, as `user@host:folder` (folder relative to the home
   directory), in an environment variable so the address stays out of this
   public repo: `setx WEB_DEPLOY titouan@<pi address>:rl-brawl-web` (then open
   a new terminal). `--deploy user@host:folder` overrides it once.
2. Log in without a password (SSH key): if `ssh <user>@<pi address>` asks for a
   password, add the PC's key to the Pi once, in PowerShell:
   `type $env:USERPROFILE\.ssh\id_ed25519.pub | ssh <user>@<pi address> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys"`
   (it asks for the password one last time). No key yet: `ssh-keygen -t ed25519` first.

## Updating the content

The export is a snapshot of the repo, and **nothing updates the website by
itself**. After any change, run `python tools/web_export.py --deploy`;
visitors get the new version on their next load (see Caching above).
Re-export when:

- **A new training run or new checkpoints** ("Latest bot" = the newest
  `runs/*/agent_latest.bin`): also run `python tools/run_summary.py` first,
  so the Training screen has the new run's charts.
- **New ladder results** (`python tools/ladder.py`): `run_summary.py`, then export.
- **New replays** (anything under `replays/`).
- **New characters, moves, bot levels, or any code change.**

What gets packed is the `PACK` list at the top of `tools/web_export.py`.

## Files

| File | Size | Gzipped | What |
|---|---|---|---|
| `index.html` | 0.01 MB | | The page: a canvas plus the loader. |
| `index.js` | 2.86 MB | 0.36 MB | Godot's JavaScript glue (loading, input, audio). |
| `index.wasm` | 1.51 MB | 0.58 MB | The engine's entry module. |
| `index.side.wasm` | 44.08 MB | 10.55 MB | The Godot engine itself, in the build that can load extensions. |
| `fightgame.web.template_release.wasm32.nothreads.wasm` | 0.35 MB | 0.08 MB | Our C++ game (`core/` + the Godot binding), compiled to WebAssembly. |
| `index.pck` | 15.76 MB | 4.97 MB | Godot's packed data: scenes, scripts, and the repo snapshot (characters, stage art, moves, bots, run summaries, replays). |
| `index.png`, `index.icon.png`, `index.apple-touch-icon.png`, `index.audio*.js` | small | | Splash image, icons, audio worklets. |

Total about 65 MB, about 17 MB compressed. Most of it is the engine; the
game's own code and data are about 16 MB (5 MB compressed).

## Building

### One-time setup

1. **Emscripten 4.0.20**, the compiler that turns C++ into WebAssembly. It
   must be the exact version Godot 4.7.2's web engine was built with (the
   browser console prints it at startup: `Build configuration: Emscripten
   4.0.20`). A different version can fail to load our extension.
   ```
   cd D:\
   git clone https://github.com/emscripten-core/emsdk
   cd emsdk
   emsdk install 4.0.20
   emsdk activate 4.0.20
   ```
   `tools/web_export.py` looks for it next to the repo (`D:\emsdk`); set the
   `EMSDK` environment variable if it's elsewhere.
2. **Godot's export templates for 4.7.2**: the pre-built engine that an
   export reuses. In the Godot editor: Editor > Manage Export Templates >
   Download. (Only `web_dlink_nothreads_release.zip` and `_debug.zip` are
   used; they go in `%APPDATA%\Godot\export_templates\4.7.2.stable\`.)
3. CMake and Ninja (the build tool CMake drives here): CMake comes from pip
   (`python -m cmake`); Ninja is already on this PC (Strawberry Perl).

### What `tools/web_export.py` does

1. **Builds the extension for the web** into
   `godot/bin/fightgame.web.template_release.wasm32.nothreads.wasm`: CMake
   with Emscripten's toolchain file, godot-cpp in `template_release` mode
   with threads off (build folder `build/web/`).
2. **Packs the repo files the game reads** into `godot/repo/`, keeping their
   layout (`assets/characters/...`, `data/moves/...`, `runs/<run>/summary.json`,
   `replays/...`), plus `mtimes.json` with each file's modification date.
3. **Exports** with Godot's command line: `godot --headless --path godot
   --export-release Web build/web_export/index.html`, using the "Web" preset
   in `godot/export_presets.cfg`.
4. **Deletes `godot/repo/`**, so the desktop project never sees the copies.

A full build takes about a minute (godot-cpp is compiled once; later builds
only recompile what changed).

## How it works (and why it was built this way)

The game was already split so that this was possible: one C++ core (the
simulation) and Godot only drawing and reading input. Making it run in a
browser needed four things.

**1. No Python at play time.** The bots used to run in PyTorch, in a Python
process the game talked to over a local network connection. A browser can't
run either. Now the bot's network (173 -> 256 -> 256 -> 45, tanh) runs in C++
inside the game (`core/net.cpp`), from the actor's weights saved as a small
`.bin` (0.5 MB). `ppo.py` writes a `.bin` next to every checkpoint, and
`tools/export_bot.py` converts older ones. `tests/test_net.py` checks the C++
network gives the same outputs as PyTorch. (Scripted bots are still Python,
which is why they stay desktop-only.)

**2. The C++ compiled to WebAssembly.** WebAssembly is a compact machine-code
format that browsers run at close to native speed. Emscripten compiles our
C++ to it. Godot loads our code as a *side module*: a library loaded at
runtime by the engine (the browser's equivalent of the Windows DLL). That
needs the engine build that supports it (Godot's "dlink" templates, hence the
44 MB engine) and our library compiled with the same Emscripten version as
the engine. `bindings/godot/CMakeLists.txt` reuses godot-cpp's
`emsdkHack.cmake` so CMake agrees to build a shared library with Emscripten
(without it the linker treats it as a program and asks for `main()`).

**3. The repo inside the page.** On desktop the game reads files from the repo
folder (`D:\RL-BRAWL\assets\...`). A browser has no such folder, only what was
packed into `index.pck`, which Godot exposes under `res://`. So every file
access goes through `Session.repo_root()` (`godot/session.gd`): the repo
folder on desktop, `res://repo` on the web, filled by the export script.
Details that made this work:
- Move files are read by GDScript and handed to C++ as text
  (`load_moves_from_text`), because C++ file functions can't see inside the
  `.pck`.
- PNGs are packed raw (Godot's "keep" importer, set by the script) so the game
  can load them the same way on both platforms (`Image.load_from_file`).
- Packed files have no dates, but the game sorts by them (newest bot, newest
  replays first): `Session.modified_time()` reads the dates the export saved in
  `mtimes.json`.

**4. A renderer browsers support.** Browsers offer WebGL 2 (a version of
OpenGL), so the project uses Godot's Compatibility renderer, on desktop too
(it's a 2D game; it looks the same, checked frame by frame).

**Single-threaded on purpose.** A multi-threaded Godot web build needs the
server to send special security headers (COOP/COEP) and has been the usual
source of trouble in Safari. The game needs only one thread (the simulation
costs microseconds per frame, a bot decision well under a millisecond), so
the single-threaded build costs nothing and works on any host.

**Determinism holds.** A replay stores only inputs and checks the final state
hash at the end. Replays recorded on Windows (MSVC-compiled simulation)
verify in all three browsers (Emscripten-compiled), so the browser's
simulation matches the training one bit for bit.

## Testing a new export

1. Serve it (`python -m http.server -d build/web_export 8000`), open
   http://localhost:8000.
2. Bot vs Bot with "Latest bot" on both sides: the fight starts and the bots move.
3. Replays: open one, press Up three times (8x speed), wait for the end: it
   must say "Replay verified" in green.
4. Training: open a run; the charts show.
5. Try more than one browser (at least one Chromium-based and Firefox).

The browser console (F12 > Console) shows Godot's messages and any error.

How it was tested during development: Playwright (`pip install playwright`,
`python -m playwright install chromium firefox webkit`) drove the three
browsers headless, clicked through the menu and took screenshots. Two lessons:
a real controller connected to the PC steers the menu during automated tests
(hide it with `page.add_init_script("navigator.getGamepads = () => []")`),
and headed (visible) Chromium/Firefox windows pause when covered by other
windows on Windows, so run them headless.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| The menu shows but clicks do nothing, or the selection jumps around | A controller is sending input (a held trigger, a drifting stick, a pressed button). Browsers pass controllers to pages. | Put the controller down, switch it off, or unplug it. |
| Blank page, nothing loads | Opened from disk (`file://`), or the server sends `.wasm` with the wrong type. | Serve over HTTP; check the console for the failing file. |
| Console: `failed to load GDExtension` / the game's menu shows but "FightGame" errors | The extension was built with a different Emscripten version than the engine, or is missing. | Check the version the console prints, `emsdk activate` it, delete `build/web/`, export again. |
| Linking fails: `undefined symbol: main` | CMake built the extension as a program, not a side module. | `bindings/godot/CMakeLists.txt` must include `emsdkHack.cmake`; delete `build/web/` and export again. |
| Export fails: no export template found | Godot's templates aren't installed. | [One-time setup](#one-time-setup), step 2. |
| "Secure Context - Check web server configuration (use HTTPS)" | The page was opened over plain `http://` from another machine. | Serve it over HTTPS (or test through `localhost`, e.g. an SSH tunnel). |
| `--deploy`: `Permission denied (publickey,password)` | The Pi doesn't know the PC's SSH key. | [Deploying](#deploying-from-the-pc), step 2. |
| The site still shows the old version | Browser cache. | Ctrl+F5; on the server, see Caching in [Putting it on a website](#putting-it-on-a-website). |
| A replay ends with DESYNC | It was recorded on an older version of the game (replays store inputs, not code). | Regenerate replays (`python train/record_vs.py`), export again. |
| "no trained run yet" on Latest bot | No `runs/*/agent_latest.bin` when exporting. | Train, or export older checkpoints (`python tools/export_bot.py runs/<run>`), then export again. |
| Safari: repeated `glBlitFramebuffer` WebGL errors in the console | A known harmless WebKit message with Godot's renderer. | None needed: the game draws correctly. |
