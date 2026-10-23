# LearnBot

An autoplayer for Geometry Dash 2.2 that **learns a route by simulation**: it plays with hits disabled, notes the exact tick where it would have died, rewinds to a checkpoint a little before that point, tries earlier inputs until one survives, keeps the fix, and saves the result per level. It then plays the learned route back live.

It knows about **spikes, pits, orbs (rings), jump pads, and the cube/robot/ball/spider modes**, plus corridor steering for ship, UFO, wave and swing. It opens a **separate console window** that explains what it is doing in plain English — no programming knowledge needed.

It **refuses to run on medium, insane or extreme demons** — that rule is enforced before the bot does anything, and it is unit tested.

> **Honest verdict first.** The learning algorithm is real and tested (33 assertions, run on a PC against a physics simulator). The Geometry Dash glue around it is written against the verified v5 bindings but **has not been compiled or run in-game** — no GD or Geode SDK was available where this was built. Expect to spend a session calibrating. See [What is and is not verified](#what-is-and-is-not-verified).

---

## The gate

| Level | Runs? |
| --- | --- |
| Any non-demon (easy → insane) | ✅ |
| Easy demon | ✅ (toggleable) |
| Hard demon | ✅ (toggleable) |
| **Medium demon** | ❌ |
| **Insane demon** | ❌ |
| **Extreme demon** | ❌ |
| A demon whose tier can't be read | ❌ (refuses rather than guesses) |

Tiers come straight from `GJGameLevel::m_demon` / `m_demonDifficulty`. The check runs in `PlayLayer::init`, before any hooking of inputs, and the HUD says why it refused (`LearnBot: BLOCKED (Extreme Demon)`).

---

## The debug window

On Windows, enabling the mod opens a **separate console window** next to the game titled *"LearnBot - what the bot is thinking"*. On every other platform the identical lines are written to `geode/mods/yourname.learnbot/learnbot.log`.

It is written for a person, not a programmer — including people who have never played Geometry Dash:

```text
=== Level check ===
Level: "Some Easy Level"
Allowed. I will start thinking as soon as the level starts.

=== Thinking about this level ===
I am about to replay the level inside the game, as many times as it
takes. The game may freeze for a moment - that is me working.
Getting further: 18% of the level.
Crashed at 22.4%. Working out what to change.
Fixed it. I changed 1 input and it now survives 0.50 s further.
Getting further: 63% of the level.
I cannot get past 71% with the ideas I have. Saving what I
learned so far - next attempt will start from there.

=== Result ===
Best: 71.3% of the level.
Crashes seen: 14  |  fixed: 11  |  backed up and retried: 3
Route length: 9321 steps (38.84 s of level).
Now playing that route back with normal physics.
```

The first thing it prints is a short glossary (`%` = how far it got, *step* = 1/240th of a second, *orb* = the ring you press in mid-air, *pad* = the spring that fires by itself), so the rest is readable without knowing anything about the game.

**Settings:** `debug-console` turns the window on/off, `debug-detail` adds the numbers behind each message (tick indices, simulated step counts) for when you are actually debugging.

Because the messages are flushed as they are written, the console keeps updating *during* the freeze while the game window is stuck — that is the best view of what a learning pass is doing.

---

## How the learning works

```text
   ┌─ restore checkpoint ────────────────────────────────┐
   │                                                     │
   ▼                                                     │
 play forward, one input per 240 Hz tick  ──► completed ─┴─► save route, replay it
   │
   └─ a hit is detected (noclip swallowed it)
        │
        ├─ REPAIR: for back = 1..search-back:
        │     rewind to the checkpoint before (death - back)
        │     flip that one input, simulate lookahead ticks
        │     survived?  ──► keep the flip, drop the rest of the route
        │
        └─ none worked?
              REWIND: restart at (death% - rewind%), drop the tail,
                      force one random input, try again
                      (each further failure doubles the distance,
                       eventually restarting from 0%)
```

Four details that matter:

- **The bot owns the game loop while learning.** `GJBaseGameLayer::update` is intercepted and the original is called by hand, one 1/240 s tick at a time, with no rendering in between. That turns "play the level" into a synchronous function call, which is what makes rewind-and-compare possible. (This is the same technique the frame-window scanners use to replay a level thousands of times.)
- **The "percent − 2" restart is the rewind step**, and rewinds escalate: 2%, 4%, 6%… A depth-1 flip cannot escape a route that is wrong *everywhere*, so backing up progressively further is what actually gets the learner unstuck. This behaviour is covered by a test that starts from a deliberately garbage route.
- **Candidates are scored by how far they survive**, not just pass/fail, so the search hill-climbs: if nothing clears the lookahead, the idea that gets furthest is still kept and the next fix starts from there.
- **The search window widens itself.** A missed orb kills you a hundred ticks later, so searching only the last few ticks finds nothing. The window starts at `search-back` and doubles up to `max-search-back` before it gives up — that is what turned orb sections from "impossible" into "solved" during development.
- **Everything is per-tick and seeded**, so a given seed reproduces a given route exactly (also tested).

---

## What it understands

The heuristic is only the *starting guess*; the learner refines the timing. But a better guess means fewer simulations, so it knows about the things that make GD gameplay:

| Thing | What the bot does |
| --- | --- |
| Spikes, saws, blocks | Jumps when a hazard is inside the jump window (`jump-clearance-min..max`) |
| Pits / missing ground | Jumps when the ground ends within `pit-jump-distance` |
| **Orbs (rings)** | Presses while inside the ring's reach: yellow, pink, blue, green, black/drop, dash, gravity-dash, spider, teleport and custom rings all take the same "press it" treatment |
| **Jump pads** | Knows they fire on their own, so it *stops* jumping `pad-avoid-distance` before one instead of fighting it |
| Ship / UFO / wave / swing | Steers for the middle of the largest free vertical gap ahead (`flying-lookahead`) |
| Cube / robot / ball / spider | Ground rules; robot holds longer, ball and spider are treated as "press to change direction", which the search then fixes |

Object types come from GD's own `GameObjectType`, not a hand-written ID table, so community orbs and pads work too.

Still not modelled: portals (the game applies them for you, but the heuristic does not plan around them), duals, and slope-specific physics. The learner can still brute-force through those — it just gets less help.

---

## What is and is not verified

**Tested here, on this machine** (`tests/learner_test.cpp`, `g++ 14 -std=c++23`, 39 assertions):

```text
[sanity] a decent heuristic can clear the toy course
[learn] naive heuristic + learner clears the course
    status=complete best=100.0% deaths=8 repairs=8 rewinds=0
[orbs] a pit that needs a mid-air orb press
    ok    without orb knowledge, the pit cannot be crossed
    status=complete best=100.0% deaths=1 repairs=1 rewinds=0
[learn] a garbage preloaded plan still gets repaired
    status=complete best=100.0% deaths=10 repairs=6 rewinds=4
[depth] two inputs may change at once        -> complete
[depth] depth 1 is still usable (cheap mode) -> complete
[budget] a tiny budget stops the pass instead of hanging
[events] the pass reports what it is doing
[determinism] same seed, same result
[gate] the shipped rule: no medium/insane/extreme demons   (12 checks)
[flying] free-gap maths for ship/wave steering              (7 checks)
PASS (0 failures)
```

The learner, the difficulty gate and the flying-steering geometry are plain C++ with no Geode dependency, exercised against a 240 Hz toy platformer. That is where the algorithmic risk was, and it is covered.

**Not verified — the parts to calibrate first:**

1. **The glue compiles?** Written against the v5 binding headers (`markCheckpoint`, `loadFromCheckpoint`, `handleButton`, `m_objects`, `m_currentStep`, …) but never compiled. Send me the compiler errors and I'll fix them.
2. **`m_currentStep` as the replay index.** The learned route is indexed by physics tick. If the live replay is consistently one tick off, that is what `inject-offset` is for (−1 or +1).
3. **Objects vs player coordinates.** `scan::ahead` compares `GameObject::getObjectRect()` against `PlayerObject::getPosition()`, assuming they share a coordinate space (they should — both live under the object layer). If the bot jumps at nonsense times, check this first.
4. **Determinism.** GD is deterministic at a fixed 240 Hz tick unless the level uses random triggers, timers or chance objects. Those break the assumption that a rewound branch replays identically.

---

## Build

New to this? **[BUILDING.md](BUILDING.md)** walks through it click by click, including a GitHub route that needs no compiler at all. The short version:

```bash
# once
geode sdk install
geode sdk install-binaries
geode config setup        # pick your GD folder, so builds self-install

# then, inside the learnbot folder
geode build               # → build/yourname.learnbot.geode
```

Run the tests without any Geode SDK:

```bash
./run-tests.sh          # or: g++ -std=c++23 -O2 -o build/learner_test tests/learner_test.cpp && ./build/learner_test
```

Both Linux/macOS (`g++`, `clang++`) and MSVC (`cl /std:c++latest`) can build the test target; `.github/workflows/` is included for cloud builds if you want the `.geode` artifact.

## Install & use

1. Install the `.geode` and start the game.
2. Mod settings → **Enable LearnBot** (or press the keybind, default <kbd>F2</kbd>, or the *LearnBot* button in the pause menu).
3. Start a level. On the first tick the bot runs its learning pass — **the game will freeze for a moment**, then reset and play the learned route back.
4. Progress is stored per level in `geode/mods/yourname.learnbot/routes/<levelID>.plan`. Replaying a level reuses the stored route and only learns what is still missing.

---

## Settings

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | off | Arm the bot (applies on the next level start) |
| `block-medium-demon` / `block-insane-demon` / `block-extreme-demon` | **on** | The gate |
| `block-easy-demon` / `block-hard-demon` | off | Optional extra blocking |
| `block-completed-levels` | off | Skip your own 100%s |
| `effort` | 6/10 | Thinking budget for a pass: 1 ≈ a few seconds, 10 ≈ ten minutes |
| `max-search-back` | 480 ticks | Widest search window; raising it fixes "cause long before the crash" deaths |
| `max-depth` | 2 | Inputs changed at once (1 = cheap, 3 = slowest/smartest) |
| `max-candidates` | 128 | Ideas tested per crash |
| `debug-console` / `debug-detail` | on / off | Separate console window, and technical lines in it |
| `lookahead-ticks` | 96 ticks | How long a candidate fix must survive |
| `rewind-percent` | 2.0 | The "percent − 2" restart distance (doubles on repeat failures) |
| `checkpoint-every` | 120 ticks | Rewind-point interval; smaller = faster searches, more state |
| `max-ticks` | 28800 | Hard cap on one pass (2 minutes of level) |
| `max-rewinds` | 24 | Give up on a section after this many attempts |
| `noclip-while-learning` | **on** | Required; the pass must survive hits to record them |
| `noclip-fallback` | off | Also skip hits during live playback (definitely cheating) |
| `safe-mode` | **on** | Never save progress from a run the bot touched |
| `show-hud` | on | Status line in the corner |
| `inject-offset` | 0 | Shift every learned input ±ticks — the replay calibration knob |
| `jump-clearance-min` / `-max` | 12 / 60 | Cube jump window (game units; a block is 30) |
| `flying-lookahead` | 140 | How far ahead ship/wave aim for the middle of the free corridor |
| `orb-reach` | 18 | How close counts as "touching a ring" |
| `pad-avoid-distance` | 90 | Stop jumping this close to a jump pad |
| `pit-jump-distance` | 40 | Jump when the ground ends within this distance |
| `toggle-keybind` | F2 | Arm / disarm |

**Cost knobs.** The pass stops when it runs out of *simulated* ticks, which is what `effort` controls (1 → 0.5 M, 6 → 25 M, 10 → 300 M; 240 ticks = one second of gameplay). Everything else — `max-search-back`, `max-depth`, `max-candidates` — makes each crash more expensive but smarter. Raising depth to 3 and effort to 10 is the "give it ten minutes" configuration.

---

## Limitations, plainly

- **Shallow search, by design.** Depth 2 with a wide window and a big budget is where it is smartest; raising `max-depth` to 3 multiplies the cost. Sections that need several coordinated changes at once are still out of reach — the rewind/mutate step is the escape hatch, not a solution.
- **Orbs and pads are handled; portals and duals are not.** See [What it understands](#what-it-understands). Sections that are *built* around portals or duals will be brute-forced rather than understood.
- **Demon-level gameplay is exactly what this cannot do**, which is a happy accident of the gate you asked for: the levels it is banned from are the ones it would fail anyway.
- **It is cheating.** Even with `safe-mode`, running a bot on a rated level to submit a completion is against the rules everywhere that matters. Use it on your own copies, in practice, for research.
- **One learning pass runs on the main thread** — that is the freeze. There is no way around it: GD is not thread safe.
- **Saved routes are tied to the level's exact object data.** Editing a level invalidates its route (it will re-learn).

---

## Files

```text
learnbot/
├── mod.json · CMakeLists.txt · logo.png · about.md · changelog.md · LICENSE
├── run-tests.sh
├── src/
│   ├── main.cpp            settings → g_bot cache
│   ├── State.hpp           the cache
│   ├── Hooks.cpp           PlayLayer init/death/reset, GJBaseGameLayer tick pump, pause button
│   ├── Console.hpp/.cpp    the debug window + the plain-English translator
│   ├── Bot.hpp/.cpp        gate → learn pass → live replay → route persistence → HUD
│   ├── GameSim.hpp         learnbot::Sim over a live PlayLayer (checkpoints, input, ticks)
│   ├── Heuristic.hpp       the per-tick "decent player" rule
│   ├── Scan.hpp            nearby hazards/solids from PlayLayer::m_objects
│   └── core/               ← GD-free, tested
│       ├── Learner.hpp     the search algorithm
│       ├── Gate.hpp        the difficulty rule
│       └── Intervals.hpp   free-gap maths for ship/wave
├── tests/
│   ├── learner_test.cpp    33 assertions, no game needed
│   └── ToyWorld.hpp        240 Hz toy platformer
└── scripts/check-settings.py
```

MIT. Not affiliated with RobTop Games or the Geode team.
