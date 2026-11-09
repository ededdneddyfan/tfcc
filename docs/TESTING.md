# TFCC regression testing

Goal: anything that works on stock TFC (maps, AMXX plugins, models, sprites,
sounds) works identically with TFCC. The pipeline is layered so that the cheap,
deterministic gates run on every commit and the expensive ones run in CI with
real HLDS.

## Layer 1: movement differential test (`tests/pm`, every commit)

Builds the upstream `pm_shared` (the merge-base with `main`) and the TFCC
`pm_shared` as two libraries, then runs both through the same fuzzed scenarios
(random worlds of floors, ramps, surf slopes, walls and ceilings; random
usercmds, jumps, ducks, 1-50 ms frames) with the release compiler flags (x87)
and again with SSE math.

* **Gate 1:** with no TFCC physinfo key (and with `tfc`, `slj`, `tcrf=0`
  variants) every frame's origin, velocity, flags, hull and view offset must be
  bit-identical to stock. Currently 4 x 450,000 frames, 0 differences.
* **Gate 2:** a scripted `PM_FlyMove` that re-hits the same ramp at fraction 0.
  Stock must reproduce the ramp bug (it zeroes speed in about half the cases),
  TFCC with `tcrf=1` must never zero it, and TFCC without the key must match
  stock exactly.

Run locally: `tests/pm/run.sh` (needs `gcc-multilib`).

## Layer 2: ABI / protocol check (`tests/abi`, every commit)

`tests/abi/check.sh build/cl_dll/client.so`

* every engine entry point (`Initialize`, `HUD_*`, `V_CalcRefdef`, ...) is exported;
* with `STOCK_CLIENT=/path/to/stock/client.so`: every symbol the stock client
  exports is exported by TFCC, and every user message the stock client hooks is
  hooked by TFCC (a missing hook means an AMXX/server message is silently
  dropped).

## Layer 3: server matrix (`tests/server`, CI with real HLDS)

`tests/server/run_matrix.sh` installs nothing itself; CI installs HLDS + TFC with
steamcmd, overlays a server config (default: Seth's
`tfc-pickup-server-configuration`: metamod-p, AMXX, its plugins), and runs every
map twice:

* **stock** profile: the config as-is;
* **tfcc** profile: plus `tfc_fixes_mm`, OTFC movefix (if downloadable), and
  `tfcc_mm`, minus `BugFixesTFC.amxx`.

On each map, `tfcc_selftest.amxx` spawns fake clients that join both teams as
every class and run around firing, priming grenades, dropping items and dying
for `MAP_SECONDS` (default 45). A map fails if the tfcc profile crashes, never
finishes, prints an error the stock profile didn't print on the same map, loses
an AMXX plugin that was running under stock, or doesn't load `tfcc_mm` /
`tfc_fixes_mm`. Results: `matrix-results/report.md` (also in the job summary).

Custom content (maps, plugins, models...) is added by pointing CI at a private
repo: set repo variable `TFCC_CONTENT_REPO` (owner/name) and secret
`TFCC_CONTENT_TOKEN`; its `server/` folder is copied over the config.

## Layer 4: client differential (needs a machine with Steam TFC, planned)

The client cannot run headless in CI with Valve's engine. Plan:

1. Record reference demos on stock TFC (list below).
2. `tests/client/` (to be added) plays each demo twice on Seth's PC with
   `hl.exe -game tfc` and `hl.exe -game tfcc`, taking screenshots at fixed demo
   times and capturing console output (`condebug`).
3. Diff: console lines (errors, missing sprites/models/sounds), HUD screenshots
   (menus, HUD text, status icons, scoreboard), with a small pixel tolerance.

Optionally the same can run in CI under Xash3D FWGS, loading the stock
`client.so` and the TFCC `client.so` side by side.

## Files Seth should provide

Put them in a **private** GitHub repo (they are not redistributable in this
repo) laid out as `server/` (copied over `tfc/` on the test server) and
`client/` (demos and reference binaries), then set `TFCC_CONTENT_REPO` /
`TFCC_CONTENT_TOKEN`.

1. **Stock TFC binaries** from your current Steam install: `tfc/cl_dlls/client.dll`,
   `tfc/cl_dlls/client.so`, `tfc/dlls/tfc.dll`, `tfc/dlls/tfc.so`. Used by the ABI
   check (`STOCK_CLIENT`) and the client differential.
2. **Every custom map you care about**, with all its dependencies: `.bsp`, `.res`,
   `.wad`, map-specific models/sounds/sprites/skies, `.txt`/`.cfg` overviews. Priority:
   maps with jump pads/fans (`trigger_push`), teleporters, flag rooms with vents,
   doors that heal, and anything with custom HUD sprites.
3. **Your full AMXX plugin set** as actually deployed: `.amxx` *and* `.sma`
   sources, `configs/` (plugins.ini, cvars, ini files), and any data files. Highest
   value: plugins that use menus (`show_menu`, `menu_*`), HUD/director messages,
   `TextMsg`/`SayText`/`StatusText`/`StatusIcon`, VGUI menus, `SetView`/cameras,
   custom user messages, physinfo, or that hook movement/grenades.
4. **Metamod plugins** you run besides AMXX (e.g. the `etana` plugins listed in your
   pickup `plugins.ini`, which aren't in the repo) and their configs.
5. **Server configs**: `server.cfg`, `mapcycle.txt`, `motd.txt`, `listenserver.cfg`,
   launch line (`-pingboost`, `sys_ticrate`, `+fps_max`).
6. **Custom client content** players use: player/weapon models, sprites (`hud.txt`
   replacements, crosshairs), sounds, custom `gfx/vgui` and `resource/` files,
   fonts, and a typical competitive `config.cfg`/`userconfig.cfg`.
7. **Reference demos** recorded on stock TFC (`record name`): one per class on a
   busy public/pickup map; menus open across a map change; spectating (free look,
   chase, first person); scoreboard; flag capture + drops; sentry build/destroy;
   deaths at low health; jump pads and ramps. 1-3 minutes each.
8. **The OTFC movefix zip** (`otfc-movefix-0.3.zip`, ships with source). This
   container can't reach officialtfc.win; with it the TFCC ramp-fix mirror can be
   checked line-by-line against the server code so prediction matches exactly.
