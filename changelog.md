# 0.4.0

- **The tick pump now tries four different ways of stepping the game and
  reports which one works** instead of assuming one. The bindings cannot show
  whether `PlayLayer` overrides `update`, and if it does, calling the base
  implementation walks straight past the code that moves the level - which is
  exactly what "0 steps, 0 units" looked like.
- Added a re-entrancy guard so driving the game through its own vtable is safe.
- `diag:` line now also reports `gameState`, so a level that is not in the
  playing state is visible immediately.
- New setting `diagnose-only`: run the drive test, print a report, stop. No
  learning, no playback - the fastest way to report a problem.
- Stopped blocking for up to 600 frames waiting for the player to move by itself.

# 0.3.0

- **Fixed: the driver assumed a 1/240 s step is accepted.** It is not, on a
  normal GD build - so 60 manual updates moved the player 0 units. The step size
  is now *calibrated*: candidates from 1/240 s upward are driven and measured
  against the game's own step counter and the player's position, and the
  smallest one that actually moves the game is used. If several ticks happen per
  call, that ratio is recorded and used for replay indexing.
- **Fixed: resetting the level before driving it.** A freshly reset level sits in
  its respawn state and does not simulate, which was the other half of "0 units
  moved". The pass now starts from the live state.
- **Fixed: routes could be saved from a pass that proved nothing** (0% and zero
  crashes), poisoning the level forever. Passes like that are no longer saved.
- Routes are now versioned (`LBROUTE2 <steps-per-call>`); unknown or damaged
  files are ignored instead of replayed. Old poisoned routes are dropped
  automatically.
- **Fixed: the button was pressed on every tick** instead of only on changes,
  which is not the same as holding and can re-trigger orbs.
- Added a checkpoint self-test: if this build refuses to restore a saved game
  state, the bot says so and falls back to restarting the level for every
  branch instead of failing silently. Slower, still correct.
- Diagnostics on failure are now always printed, even with "Debug detail" off.

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
