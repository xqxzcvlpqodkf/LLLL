# 0.2.0

- Separate debug console window (Windows) and `learnbot.log` everywhere else,
  written in plain English with a short glossary for non-players.
- Search upgraded: simulation budget (`effort` 1-10), survival-scored candidates
  (hill climbing), up to 3 inputs changed at once, self-widening search window.
- Knows orbs (all ring types), jump pads, pits and the robot/ball/spider modes.
- New settings: `effort`, `max-search-back`, `max-depth`, `max-candidates`,
  `orb-reach`, `pad-avoid-distance`, `pit-jump-distance`, `debug-console`,
  `debug-detail`.
- Tests: 39 assertions, including an orb section a naive player cannot cross.

# 0.1.0

- First version.
- Difficulty gate: refuses medium, insane and extreme demons (unit tested).
- Learning pass: checkpoint-based depth-1 input search with escalating rewinds.
- Live replay of the learned route, stored per level.
- Per-tick heuristic: cube-style jump windows and ship/UFO/wave free-corridor steering.
- Status HUD, pause menu button, arm/disarm keybind.
- Safe mode: nothing from a bot run is saved.
