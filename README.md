# Cookie Game

First-person 3D clicker where every cookie, toaster oven, and grandma physically piles up in the house.
The full design spec is in [`CLAUDE.md`](CLAUDE.md).

## Status

| Build step | State |
|---|---|
| 1. Left-click shoots a cookie in an arc, counter goes up | Done, playtested |
| 2. Hold right-click rapid-fires grandmas, ramping rate, costs deducted | Done, playtested |
| 3. Hotbar (scroll wheel / 1–9), toaster ovens that bounce | Done, playtested |
| 4. Helpers produce cookies over time and pop out physical cookies | Done |
| 5. Freeze-when-settled, instancing, cookie piles, stress test | Done |
| 6. Grandmas ragdoll on landing, get up, bake; only nearby ones animate | Done, playtested (Quinn mannequin stand-in) |
| 7. House: kitchen, living room, hallway, bedroom, bathroom, front door, fenced yard | Done, playtested (placeholder blocks, no roof yet) |
| 8 | Not started |

Performance (UE 5.8 editor PIE on a Tesla T4, template level): empty scene ~48 fps; 30 ovens + 60 grandmas
producing ~20 fps. The GPU sits at 99% and the render thread is the limit, not game code (<1 ms/frame).
Lowering engine scalability (Lumen, virtual shadow maps) is the likely fix and hasn't been tried yet.

## 1. Rent a machine

You need a Windows cloud PC with a GPU. See [`docs/RENT_COMPUTE.md`](docs/RENT_COMPUTE.md),
then run [`scripts/setup-windows.ps1`](scripts/setup-windows.ps1) on it.

## 2. Create the project (on the rented machine)

1. In the Epic Games Launcher, open Unreal Engine 5.8.
2. **Games → First Person**, choose **C++**, and name the project exactly **`CookieGame`**.
   The code uses the `COOKIEGAME_API` macro, which depends on that name.
3. Close the editor once it opens.
4. Copy these files from this repo into the new project folder:
   - `Source/CookieGame/Cookie/` → `<Project>/Source/CookieGame/Cookie/`
   - `Content/Cookie/` → `<Project>/Content/Cookie/` (placeholder materials and the house level, `Lvl_House`)
   - `Content/__ExternalActors__/Cookie/` and `Content/__ExternalObjects__/Cookie/` → the same paths in
     `<Project>/Content/` (the house level's actors; World Partition stores each one as its own file)
   - `CLAUDE.md`, `README.md`, `docs/`, `scripts/`, `.gitignore` → `<Project>/`
5. Open `<Project>/Source/CookieGame/CookieGame.Build.cs` and add `"DeveloperSettings"`, `"PhysicsCore"`,
   `"MeshDescription"`, `"StaticMeshDescription"`, `"RenderCore"` and `"RHI"` to `PublicDependencyModuleNames`,
   and `"CookieGame/Cookie"` to `PublicIncludePaths`. `"EnhancedInput"` should already be listed; add it if it isn't.
6. Right-click `CookieGame.uproject` → **Generate Visual Studio project files**, then double-click
   `CookieGame.uproject` and let it rebuild.

## 3. Open the house and play

- **Project Settings → Maps & Modes**: **Default GameMode** = `CookieGameMode`, and both **Editor Startup Map**
  and **Game Default Map** = `/Game/Cookie/Lvl_House`.
- Open `Lvl_House` (its World Settings already use `CookieGameMode`) and press **Play**.

The house was built from engine cube blocks through the editor (folders `House/Walls`, `House/Floors`,
`House/Furniture`, `Yard` in the Outliner), colored with the `Content/Cookie/House/MI_House_*` materials.
Move or resize any block in the editor; step 8 swaps them for stylized models.

The template's own character, weapon, and pickups go unused. `ACookieCharacter` replaces them.

## Controls (so far)

| Input | Action |
|---|---|
| WASD + mouse, Space | Move, look, jump |
| Left-click | Shoot a cookie (+1) |
| Scroll wheel / 1–9 | Select hotbar slot (1 = Toaster Oven, 2 = Grandma) |
| Hold right-click | Rapid-fire buy the selected item (cost ×1.15 per purchase) |

To test buying quickly, set **Project Settings → Game → Cookie Game → Starting Cookies** to e.g. 10000.
Every tunable number lives on that settings page (`Cookie/CookieGameSettings.cpp` holds the defaults).

Stress test: open the console (`~`) during play and run `CookieStress 100 300` (ovens, grandmas) to rain free items
around you. Set **Stress Test → Log Stats** to print fps, thread timings (game / render / GPU ms) and object counts to the Output Log every 2 seconds.
Measure with the editor window focused, or turn off **Editor Preferences → Use Less CPU when in Background**,
otherwise the editor throttles itself to ~3 fps.

## Code map

| File | What it does |
|---|---|
| `CookieTypes.h` | Item enum + "how it looks and flies" structs |
| `CookieGameSettings` | All tuning values (costs, rates, launch speeds, placeholder shapes) |
| `CookieGameMode` | Cookie bank, owned counts, cost formula, passive production |
| `CookieCharacter` | First-person movement, input (built in code, no assets), shoot, hold-to-buy |
| `CookieLaunchable` | A thrown physics object (cookie / oven / grandma placeholder). Freezes into an instance once still |
| `CookieSettledManager` | Everything at rest, as instanced meshes; settled items keep popping cookies; cookie piles past the cap |
| `CookieGrandma` | Ragdoll grandma: flies, gets up (a fall animation played backwards), bakes. Frozen into instances when not nearby |
| `CookieHUD` | Cookie count, crosshair, item hotbar |

## Working with Claude on the rented machine

Unreal can't run in a cloud container without a GPU, so do the build-and-test loop on the rented PC:
open a terminal in the project folder and run `claude`. It will read `CLAUDE.md`, can compile, read
build errors, and carry on from the next build step. If you also set up an Unreal MCP server there, Claude can
drive the editor directly (place actors, build the house layout).
