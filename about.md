# LearnBot

An autoplayer that learns its own route instead of replaying yours.

## How it works

- Plays the level with hits disabled, so it can see exactly where it would die.
- Rewinds to a checkpoint shortly before each death, tries earlier inputs until one survives, and keeps the fix.
- Backs up progressively further ("restart from percent - 2") when a single fix is not enough.
- Saves the learned route per level and plays it back live.
- Understands spikes, pits, orbs (rings), jump pads, and the cube/robot/ball/spider modes, plus ship/UFO/wave corridor steering.
- Opens a separate console window that explains what it is doing in plain English - readable even if you have never played Geometry Dash.

## Difficulty gate

LearnBot **will not run on medium, insane or extreme demons**. That check happens before the bot touches anything and is shown in the HUD when it refuses. Easy and hard demons are allowed; both can be blocked too in the settings.

## Please read

This is cheating. It refuses the hardest tiers by design, it never saves progress from a run it took part in, and it is meant for your own levels, practice copies and research — not for ratings, leaderboards or showcase claims.

It also only understands simple gameplay: ground jumps and ship/wave corridor following. It will not solve orbs, pads, duals or spider sections.
