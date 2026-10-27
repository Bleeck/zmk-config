# Handoff

Last updated 2026-10-27. Branch `dev`, level with `origin/dev` at `a0889e1`. Merging into `master` is the user's job; Claude never pushes to `master`.

## The keyboard

bleecKEYS: a split keyboard on nice!nano v2 boards running ZMK v0.3 (Zephyr 3.5, LVGL 8), with the version pinned in `config/west.yml`.
- **Left half:** the central (it holds the keymap). It has a 128x32 SSD1306 OLED, an EC11 knob (30 detents) and two 18650 cells.
- **Right half:** the peripheral. It has a Cirque Pinnacle trackpad and no display. The right display was turned off on purpose in `22869a8`.
- **Windows name:** the PC still lists the keyboard as "Chocofi", a name left over from an old pairing.

Every keymap or config change only needs the **left** half reflashed. The right half needs flashing only when its own files change (`bleeckeys_right.*`, the trackpad driver settings that run on the right half).

## Files

All under `config/boards/shields/bleeckeys/`:
- `bleeckeys.keymap`: 12 layers. Layer 11 KOMOREBI (combo 54 + 55, Sym L + big left thumb key) drives the komorebi window manager from the left half: each key is `&f20 <key>` (a one-param macro that taps the key while holding F20), and `%USERPROFILE%\komorebi.ahk` maps F20 + key (plus Shift / Ctrl held on the layer) to komorebic commands. The knob on that layer (`&komorebi_knob`) sends F20 + PgDn / PgUp for next / previous workspace. G and V are `&none`; the right half is transparent. Combos are near the top; each layer's knob action is in its `sensor-bindings`.
- `bleeckeys.dtsi`: matrix transform, encoder (`steps = <60>`, `triggers-per-rotation = <30>`), OLED, battery.
- `bleeckeys_left.conf`, `bleeckeys_left.overlay`: display settings and the trackpad input processors (swipe on SYM layers 4/5, tap/drag).
- `status_screen.c`: the OLED status screen. It shows BT profile, both batteries ("--" while the right half is not connected) and the layer name. It also handles the layer-name slide, BT wipe and blink, the volume line, the low-battery blink and the Caps Lock inversion.
- `display_fx.c` / `.h`: the BLEECK boot animation (one of three at random) and the matrix rain shown before the screen blanks.
- `swipe.c`, `trackpad.c`: our own input processors.
- `battery.c`: battery percentage, 0% at 3.0 V.

`bleeckeys-layouts.dtsi` (the physical layout) is out of date: it describes 48 keys, but the keymap has 61. Nothing uses it for drawing yet.

## Building

Double-click `Build firmware.cmd`, or run `pwsh -File local-build/build-local.ps1`. It needs Docker Desktop. Each build goes to a new `firmware/<yyyy-MM-dd_HH-mm-ss>/` folder and old builds are kept. The ZMK workspace is cached in the Docker volume `zmk-config-bleeckeys-workspace`; `-Clean` rebuilds it from scratch.

If Docker Desktop fails to start with "sailor-ingest.sock ... cannot be accessed", the cause is stale socket files in `%LOCALAPPDATA%\Docker\run`. Windows will not delete them while they're locked. The fix: quit Docker, rename that folder, or reboot first. On 2026-10-22 Claude's attempt to rename it was blocked by the permission check, so it's the user's step.

## Recent changes (newest first)

- `a0889e1`: knob gives one step per bump. The encoder has 30 detents with 2 edges each, so it's set to 60 steps. Confirmed on the board.
- `8d9ede1`: knob does Ctrl+Tab / Ctrl+Shift+Tab while CAPS MOD (layer 1) or SYM RIGHT (layer 5) is held. Volume on every other layer.
- `6b4532b`: right top-row key next to the trackpad (position 4, the "0" keycap) is now right click.
- `01759dc`: missed key presses on the left half are fixed. The display now runs on its own low-priority thread, and the OLED I2C bus runs at 400 kHz. See `DECISIONS.md`. Confirmed on the board.
- `f88d27b`: right-half icon and WPM graph removed at the user's request.
- `b1fb976`: dated firmware folders.
- `08f9aed`: recorded tasks 5-14 (OLED animations).

## Records

- `.claude/RECORDED.md`: tasks recorded with /record. Nothing is open: 1-14 are done except 7, which the user dropped. Tasks 9 and 13 were built, then removed later in `f88d27b`.
- `.claude/DECISIONS.md`: why things are the way they are. It covers the recorded-tasks run, the icon and WPM removal, and the missed-keypress cause.

## Layer browser

https://claude.ai/artifact/XNRfJsaZ8XsJEo8GsaPtDc: a private page that draws both halves and shows each layer, how to reach it, and what each key does.
- **Local copy:** `docs/bleeckeys-layers.html` is the same page, made standalone. Open it in any browser, no internet needed except for the fonts.
- **Data:** it carries its own copy of the keymap (the `LAYERS` array, extracted from `bleeckeys.keymap`). After a keymap change, update both the local file and the published page.
- **Thumb clusters** (positions corrected by the user on 2026-10-27; the page had 52/54, 51/53, 57/59, 58/60 swapped):
  - left: big key 55 = CAPS MOD; middle row 52 Sym R (outer), 54 Sym L; bottom row 51 Fn R (outer), 53 Fn L.
  - right: big key 56 = Space; middle row 57 Sym R, 59 Sym L; bottom row 58 Fn R, 60 Fn L.

## Open / not checked

- The LVGL heap (`CONFIG_LV_Z_MEM_POOL_SIZE=8192`) is an estimate and was never measured on the board. No problems have been reported.
- `Keyboard Transformer.xlsx` in the repo root is the user's own file and is untracked. Leave it alone.
- The user asked whether the project folder can be moved: yes. The repo has no hard-coded paths. A Claude session and its memory are tied to the old path, so open a new session in the new folder and start from this file.
