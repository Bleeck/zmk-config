# Recorded tasks

Tasks the user recorded with /record, run in one planned group with /run-recorded. Status: recorded | running | done (<commit>) | dropped (<why>).

| # | Recorded | Status | Task (the user's words) | Reading / notes |
|---|---|---|---|---|
| 1 | 2026-10-21 16:39 | done (b79c80a) | remove the scroll behavior | Likely the trackpad's circular scroll (`&zip_gestures` in bleeckeys.dtsi); could also mean `CONFIG_ZMK_POINTING_SMOOTH_SCROLLING`. |
| 2 | 2026-10-21 16:39 | done (b79c80a) | make the display show the battery for each half | Could mean the left (central) display shows both halves' batteries, or each half's display shows its own. |
| 3 | 2026-10-21 16:39 | done (b79c80a) | add gestures to the track pad when a certain key is pressed. the key that activates the caps mod layer. when that key is pressed swipe left + caps mod key  = win + left arrow (same for the rest of the directions). it should still work with the shift key pressed, and pass an extra shift to the combo. don't add conde that doesn't need to be added. if a scenario is covered by not adding anything, then don't add anything | "Caps mod key" could be the left-thumb `&mo 1` (position 55) alone, or also the right-thumb combo 56+59 that activates layer 1. |
| 4 | 2026-10-21 17:48 | done (b79c80a) | remove the komorebi layer | Unclear whether its triggers go too (the `&lm 11 F20` key on CAPS MOD and the `combo_win_nav_left` combo), or are kept as plain F20 keys. |
