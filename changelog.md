# 0.2.2

- **Fixed: the learning pass could run before the level was simulating.**
  Taking over the game loop on the first frame after `PlayLayer::init` made every
  manual update a no-op: 0 crashes, 0% progress, a 28,800-step route of nothing.
  The bot now waits for the level to actually start running.
- Added a sanity probe before spending the budget: it steps the game 60 ticks
  and reports how far the player moved. If the game does not advance it stops
  and prints exactly what to send to the developer, instead of learning nothing.
- Fixed `resetLevel()` during a pass silently flipping the bot out of learning
  mode (which would have let the pass die on the first impact).
- Console no longer prints nonsense budgets ("104166.67 s"); it reports
  simulated steps, and separates "ran out of budget" from "hit the per-attempt
  length cap".

# 0.2.1

- Verified: builds clean on GitHub Actions (Geode v5.11.2, Win64).
- Docs: added SETUP-GITHUB.md (browser-only upload and build) and BUILDING.md.

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
