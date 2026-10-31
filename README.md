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
| 8. Stylized models and sounds | In progress: cookie, toaster oven, house (Low Poly Interior kit) and grandma (Fab, retargeted) done; sounds not started |

Performance (standalone `-game`, 1280×720, Tesla T4, house with 25 ovens + 40 grandmas producing): **~76 fps**
with the rendering settings below, versus ~18 fps with the template's defaults. The defaults (Lumen,
virtual shadow maps, hardware ray tracing) overloaded the render thread, mostly because ray tracing rebuilds
every animated grandma's geometry each frame. Game code costs under 1 ms per frame.

Cookies stay fully physical up to 10,000 (the spec's starting point was 500; raised for realism): 5,000 real
cookies in the house run at ~55 fps, losing about 3 fps per extra 1,000. Past the limit, new cookies join heaps
that are drawn entirely from cookie instances (irregular shapes, two layers, leaning and upside-down cookies).

### Rendering settings (in `Config/DefaultEngine.ini`, which isn't in this repo)

```ini
[/Script/Engine.RendererSettings]
r.DynamicGlobalIlluminationMethod=0   ; was 1 (Lumen)
r.ReflectionMethod=2                  ; was 1 (Lumen) - screen space reflections
r.Shadow.Virtual.Enable=0             ; was 1 - regular cascaded shadow maps

[SystemSettings]
r.RayTracing.Enable=0                 ; ray tracing shaders stay compiled (r.RayTracing=True), just switched off
```

Without global illumination, `Lvl_House` fakes the bounce light: a warm sky light with a warm lower hemisphere,
the sun letting 25% of its light through shadows (Shadow Amount 0.75), and a weak shadowless `FillLight`
from the opposite side. On a faster GPU, flip these back to try Lumen.

To benchmark: run the game standalone (`UnrealEditor.exe CookieGame.uproject /Game/Cookie/Lvl_House -game`)
with **Log Stats** on. The editor's own Play mode caps itself at 3 fps whenever its window isn't in front.

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
   - **Purchased art, not in this repo** (licensed Fab assets):
     - The **Low Poly Interior** kit: Fab → **Add to Project** (`Content/LowPolyInterior/`: house pieces, furniture,
       cookie, toaster). Open `Materials/M_LowPolyInterior_01`, tick **Used with Instanced Static Meshes**, save.
     - The **Grandma 3D Character – Stylized Cartoon Elder Woman**: use the listing's **FBX download**, not
       Add to Project (Fab's auto-converted version has a broken skeleton). Import the `.fbx` into
       `Content/Cookie/Grandma/Model/` named `SK_Grandma`, with materials, textures and a physics asset. Open its
       `material`, tick **Used with Instanced Static Meshes**, save. Then, with the editor closed, run
       `UnrealEditor-Cmd.exe CookieGame.uproject -run=CookieRetarget` to copy the mannequin's idle and falls onto her.
5. Open `<Project>/Source/CookieGame/CookieGame.Build.cs` and add `"DeveloperSettings"`, `"PhysicsCore"`,
   `"MeshDescription"`, `"StaticMeshDescription"`, `"RenderCore"` and `"RHI"` to `PublicDependencyModuleNames`,
   and `"CookieGame/Cookie"` to `PublicIncludePaths`. `"EnhancedInput"` should already be listed; add it if it isn't.
   For the editor-only retarget setup command, also add, inside `if (Target.bBuildEditor) { ... }`,
   `"UnrealEd"`, `"AssetRegistry"`, `"IKRig"`, `"IKRigEditor"` and `"SlateCore"` to `PrivateDependencyModuleNames`.
6. Right-click `CookieGame.uproject` → **Generate Visual Studio project files**, then double-click
   `CookieGame.uproject` and let it rebuild.

## 3. Open the house and play

- **Project Settings → Maps & Modes**: **Default GameMode** = `CookieGameMode`, and both **Editor Startup Map**
  and **Game Default Map** = `/Game/Cookie/Lvl_House`.
- Open `Lvl_House` (its World Settings already use `CookieGameMode`) and press **Play**.

The house is built from the Low Poly Interior kit's modular pieces on its 2.5 m grid (Outliner folders
`House/Walls`, `House/Floors`, `House/Furniture`): a 7 × 5 tile house, white walls with window and doorway
pieces, kit furniture in every room. The yard (`Yard` folder: grass, path, fence) is still flat-colored blocks
using `Content/Cookie/House/MI_House_*`.

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
