# Building LearnBot into a `.geode`

Short version: you need the **Geode CLI** and a **C++ compiler**, or you push it to **GitHub** and let a free cloud build do it. Either way you end up with one file, `yourname.learnbot.geode`, which you drop into Geometry Dash's `geode/mods` folder.

---

## Step 0 — Get the files

Download **`learnbot.zip`** from the workspace and unzip it. You get a folder called `learnbot/` containing:

```text
learnbot/
├── CMakeLists.txt      ← the build recipe
├── mod.json            ← mod name, id, every setting
├── src/                ← the code
├── tests/              ← PC-only tests (no game needed)
└── .github/workflows/build.yml   ← for the cloud build
```

---

## Route A — GitHub builds it for you (no compiler installed)

Best if you do not want to install Visual Studio. This is what the included workflow is for.

1. Create a new **empty** repository on GitHub (private is fine, free).
2. Upload the **contents** of `learnbot/` so `mod.json` sits at the repo root — not a `learnbot/` subfolder. (Easiest: unzip, open the folder, select everything inside, drag into GitHub's "upload files" page.)
3. Push (or use GitHub Desktop → *Add local repository* → *Publish*).
4. Open the repo's **Actions** tab → **Build Geode Mod** → **Run workflow**.
5. When it finishes (~5 minutes), open the run and download the artifact **`learnbot`**. Unzip it: you get `yourname.learnbot.geode`.

If the build fails, the run's log shows exactly which file and line — paste it here and I'll fix it.

> The workflow is set to `sdk: v5.11.2` / `target: Win64`. For Android, add a second job with `target: Android64` (or run it locally, see below).

---

## Route B — Build it yourself on Windows

### 1. Prerequisites

- **Geometry Dash with Geode already installed** (needed if you want the mod auto-installed after building).
- **Visual Studio 2022** with the *"Desktop development with C++"* workload. Nothing else, no C# or game dev options.
  *(Alternative: Clang + Ninja via `scoop install llvm ninja`, then use `geode build --ninja`.)*
- Windows 10 or 11.

### 2. Install the Geode CLI

Pick one:

```powershell
winget install GeodeSDK.GeodeCLI
```

```powershell
scoop bucket add extras
scoop install geode-sdk-cli
```

Close and reopen your terminal, then check:

```powershell
geode --version
```

### 3. Install the SDK and its binaries

```powershell
geode sdk install
geode sdk install-binaries
```

Then confirm the SDK path is visible (reopen the terminal first):

```powershell
echo $env:GEODE_SDK     # PowerShell
echo %GEODE_SDK%        # cmd
```

If that prints a folder path, you're good. If it prints nothing, set `GEODE_SDK` yourself to the folder `geode sdk install` created (usually `%LOCALAPPDATA%\Geode\sdk`).

Optional but recommended, so CMake stops re-downloading dependencies:

```powershell
mkdir C:\geode-cache
setx CPM_SOURCE_CACHE C:\geode-cache
```

### 4. Point it at your Geometry Dash install

```powershell
geode config setup
```

Answer the prompts and choose your GD folder (for Steam: `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash`). This is what makes the built mod install itself automatically.

### 5. Build

```powershell
cd path\to\learnbot
geode build
```

If that errors about a missing compiler/CMake, try:

```powershell
geode build --ninja
```

Either way, the result is:

```text
learnbot\build\yourname.learnbot.geode
```

If you set up the profile in step 4, it is already installed — just start the game.

---

## Route C — Manual CMake (if the CLI build misbehaves)

```powershell
cd path\to\learnbot
cmake -B build
cmake --build build --config RelWithDebInfo
```

The `.geode` appears in `build\` (sometimes `build\RelWithDebInfo\`).

---

## Install the finished `.geode` by hand

1. Find your GD `geode\mods` folder:
   - **Windows:** `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\geode\mods`
   - **Android:** `Android/data/com.robtopx.geometryjump/files/geode/mods`
2. Drop `yourname.learnbot.geode` in there.
3. Start the game. Or: in-game, **Geode → Install mod from file**.

On load it should print to `geode/mods/yourname.learnbot/learnbot.log` and, on Windows, open the **LearnBot** console window.

---

## Android

```bash
# install the Android NDK, then set ANDROID_NDK_ROOT to its path
geode sdk install-binaries -p android64     # use android32 for 32-bit phones
geode build -p android64
```

No console window on Android — read `learnbot.log` instead.

---

## Check the brain works before you build anything

The learning algorithm and the difficulty gate are plain C++ with no game dependency, so they compile on their own. If you have *any* C++ compiler, this works in 5 seconds:

```bash
cd learnbot
./run-tests.sh                 # Linux / macOS / Git Bash
```

```powershell
# Windows without bash
cl /std:c++latest /EHsc /O2 tests\learner_test.cpp /Fe:build\learner_test.exe
build\learner_test.exe
```

You should see `PASS (0 failures)` and 39 `ok` lines. If that fails, the problem is your compiler setup, not the game.

---

## Troubleshooting

| What you see | What it means |
| --- | --- |
| `Unable to find Geode SDK!` | `GEODE_SDK` isn't set. Reopen the terminal, or set it to the folder `geode sdk install` created. |
| `geode: command not found` | CLI not on PATH. Reinstall via winget/scoop, reopen the terminal. |
| `No CMAKE_CXX_COMPILER could be found` | Visual Studio's C++ workload isn't installed, or you have Clang but didn't pass `--ninja`. |
| Build succeeds, no `.geode` in `build\` | Look in `build\RelWithDebInfo\`. |
| Mod doesn't appear in the mod list | Wrong GD version. This targets **2.2081** — if your GD updated to a newer build, Geode marks it incompatible until it catches up. |
| Everything builds but the game crashes on launch | That's my glue code, not the tutorial. Send me the crash log from `geode/crashlogs/`. |
| No console window | It's Windows-only; elsewhere check `learnbot.log`. Turn `debug-console` back on in settings. |

---

## First build will probably need a fix or two

Fair warning, repeated from the README: the algorithm and the difficulty gate were **compiled and tested here (39 assertions, all passing)**, but the Geometry Dash glue — the files that hook the game — has **never been compiled**, because no GD or Geode SDK exists in the environment this was written in.

So treat the first `geode build` as a compile-check. If it throws errors, copy the output (or the GitHub Actions log) and paste it here — they will almost certainly be small things (a renamed field, a missing include) and I'll fix them immediately.
