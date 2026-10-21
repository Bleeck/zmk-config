# Decisions

## Recorded tasks run, 2026-10-21 (tasks 5-14)

Tasks: 5 (display animations), 6 (BLEECK power-on), 7 (charging bolt), 8 (BT profile wipe and pulse), 9 (right-half icon), 10 (Caps Lock inverts the screen), 11 (volume bar), 12 (low battery blink), 13 (WPM graph), 14 (trackpad counts as activity).

The user's answers:
- Layout (128x32): top row BT symbol + profile left, `L% R%` batteries right with the right-half icon between them; bottom row layer name left, WPM graph right; the volume bar is a 2 px line on the bottom edge that grows left or right from the centre, ~0.6 s, then hides.
- Power-on (6): all three animations, one picked at random on each boot: (a) BLEECK typed with a cursor, hold, slide up while the status screen slides in from below; (b) letters drop in and bounce, then dissolve pixel by pixel into the status screen; (c) BLEECK appears, then a vertical bar sweeps left to right revealing the status screen.
- Task 7 (charging bolt): dropped by the user.
- Layer slide (5): the new name slides in from below when the new layer number is higher, from above when lower.
- Decided by Claude: the matrix rain starts ~2 s before the idle blank (ZMK default idle 30 s, unchanged) and is cut by any activity; low battery blink below 10% applies to each half's number separately; WPM kept as recorded (battery cost noted).

Plan: no probe agent up front; one builder owns `config/boards/shields/bleeckeys/` and runs the chunks in order, building with `local-build` (Docker) after each:
1. Task 14: trackpad touches count as ZMK activity (find the v0.3 mechanism first; no ZMK fork unless nothing else works).
2. Status screen restructure to the agreed layout; layer slide (5); power-on animations (6).
3. Matrix rain at idle (5); Caps Lock invert (10); low battery blink (12).
4. BT wipe and pulse (8); right-half icon (9); volume bar (11); WPM graph (13).

Verification: all three firmware targets build; animations checked on the keyboard by the user (no OLED simulator here).

Findings (builder hand-back, verified by the coordinator):
- Task 14 needed no code: ZMK v0.3 `activity.c` registers `INPUT_CALLBACK_DEFINE(NULL, activity_input_listener)` under `CONFIG_ZMK_POINTING`, and the central re-reports the right half's trackpad events through `input_split.c`, so trackpad use already counts as activity. The recording's note ("only key presses and encoder") was wrong.
- Caps Lock inversion sends the SSD1306 reverse command (0xA6/0xA7) over I2C on the display work queue; Zephyr 3.5's driver has no invert API.
- The BT symbol blinks instead of pulsing (1-bit panel); the keyboard symbol before the layer name was dropped to fit "CAPS M INV" beside the WPM graph; the right battery shows "--" after the link drops instead of ZMK's 0%.
- LVGL heap raised 4096 -> 8192 (estimate, not measured on the board). Left build: flash 414352 B (51%), RAM 84020 B (32%). All three targets build; nothing checked on the hardware yet.
