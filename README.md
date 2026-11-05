# Cookie Game

First-person 3D clicker where every cookie, toaster oven, and grandma physically piles up in the house, until
cookie trees, bakeries and factories bury the whole street under mountains of cookies.
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
| 9. Neighborhood: 12-house street around the player's house | Done, verified in screenshots (not walked in a play session yet) |
| 10. Cookie tree, bakery, cookie factory (hotbar 3–5) with cookie spouts | Done; thrown via the stress test, they fly, settle and spew |
| 11. Cookie mountains: uncapped, merging outdoor heaps, all-cookie surface, climbable | Done; a 6M-cookie heap stands 16 m tall and holds the player |
| 12. Demo Mode for filming (one toggle: mountains, producers, faster production) | Done |

Mountain performance (standalone, 1280×720, Tesla T4, a 6-million-cookie mountain in the street with ~450,000
real cookies on its surface, plus 3 factories, a bakery and a cookie tree spewing): **~46–51 fps** with the
settings below. With every producer thrown in at once plus 40 grandmas, 25 ovens and 10,000 loose cookies in the
house, ~27–29 fps (then the game thread, i.e. physics and grandmas, is the limit, not rendering).

Performance (standalone `-game`, 1280×720, Tesla T4, house with 25 ovens + 40 grandmas producing): **~76 fps**
with the rendering settings below, versus ~18 fps with the template's defaults. The defaults (Lumen,
virtual shadow maps, hardware ray tracing) overloaded the render thread, mostly because ray tracing rebuilds
every animated grandma's geometry each frame. Game code costs under 1 ms per frame.

Cookies stay fully physical up to 10,000 (the spec's starting point was 500; raised for realism): 5,000 real
cookies in the house run at ~55 fps, losing about 3 fps per extra 1,000. Past the limit, new cookies join heaps
that are drawn entirely from cookie instances (irregular shapes, two layers, leaning and upside-down cookies).

### Rendering settings (in `Config/DefaultEngine.ini`)

```ini
[/Script/Engine.RendererSettings]
r.DynamicGlobalIlluminationMethod=1   ; Lumen (software, no ray tracing) - bounce light, the biggest step to realism
r.ReflectionMethod=1                  ; Lumen reflections
r.Shadow.Virtual.Enable=1             ; virtual shadow maps: Nanite cookie mountains cast shadows cheaply
                                      ; (20 -> 46 fps with a 6M-cookie mountain; regular cascades redraw all
                                      ; ~450k surface cookies per cascade)

[SystemSettings]
r.RayTracing.Enable=0                 ; ray tracing shaders stay compiled (r.RayTracing=True), just switched off
```

Lighting in `Lvl_House`, tuned for Lumen: a neutral sky light at intensity 1.5 with a black lower hemisphere
(Lumen occludes it indoors, so rooms get darker away from windows), a midday sun (50 degrees up, 5900 K,
10 lux) with full-strength shadows for a blue daytime sky, default sky atmosphere brightness, light-blue height
fog (density 0.015; with black fog the horizon turned into a dark band), and inside, point lights at the lamps
in every room (`House/Lights`, small radius so each stays roughly in its room). It's daytime, so the lamps are
on but daylight leads: ceiling lights 3-4 cd at ~4200 K (bathroom 4500 K), hall 2.5, floor lamp 2.5, bedside
lamps 2, porch 1. At evening strength (9-11 cd, warmer) they swamped the daylight and turned rooms orange. No
lamp casts shadows: with the four ceiling lights shadowed, the render thread hit 60-70 ms (~15 fps with 40
grandmas, 25 ovens and 10,000 cookies), since each shadowed point light re-renders every nearby cookie six
times a frame. Off, it was ~28 fps in the same test, and at daytime strength the lamp shadows were faint anyway.
`FillLight` is kept but at intensity 0. A hidden `CeilingLightBlocker` slab (`House/Roof`, just above the
ceiling) casts shadows and blocks Lumen light only, so sun and sky can't leak in from the attic. An unbound
`PostProcess_Look` volume adds contact shadows (ambient occlusion 1.0, radius 120), neutral white balance, +10%
contrast, exposure compensation -0.3 and a soft vignette; it also selects Lumen for GI and reflections, matching
the config above. The living room's wall paint (`SideB` on `MI_Wall_Living_Ext` and `MI_Wall_Living_Hall`) is
a warm tan (0.74, 0.52, 0.32); the earlier terracotta (0.8, 0.4, 0.2) bounced around under Lumen and turned the
whole room orange.

Performance: measured earlier with the roof on (20 grandmas + 25 toasters), the cheap setup (screen-space GI
and reflections, shadowless lamps) ran ~56 fps and Lumen ~37 fps (~43 at reduced quality); see
above for the lamp shadows. To go back to the cheap setup, set both methods above to 2, untick the Global
Illumination and Reflections overrides in `PostProcess_Look`, and raise the exposure compensation to 0.6.

Careful: changing Global Illumination or Reflections under Project Settings → Rendering in the editor writes
straight to `Config/DefaultEngine.ini`.

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
       Open `Meshes/KitchenObjects/Mesh_KitchenObjects_Cookie_01`, tick **Nanite Support**, save: cookie
       mountains draw hundreds of thousands of cookies and need Nanite.
     - The **Grandma 3D Character – Stylized Cartoon Elder Woman**: use the listing's **FBX download**, not
       Add to Project (Fab's auto-converted version has a broken skeleton). Import the `.fbx` into
       `Content/Cookie/Grandma/Model/` named `SK_Grandma`, with materials, textures and a physics asset. Open its
       `material`, tick **Used with Instanced Static Meshes**, save. Then, with the editor closed, run
       `UnrealEditor-Cmd.exe CookieGame.uproject -run=CookieRetarget` to copy the mannequin's idle and falls onto her.
5. Open `<Project>/Source/CookieGame/CookieGame.Build.cs` and add `"DeveloperSettings"`, `"PhysicsCore"`,
   `"MeshDescription"`, `"StaticMeshDescription"`, `"RenderCore"`, `"RHI"` and `"ProceduralMeshComponent"` to
   `PublicDependencyModuleNames` (and enable the **Procedural Mesh Component** plugin in `CookieGame.uproject`),
   and `"CookieGame/Cookie"` to `PublicIncludePaths`. `"EnhancedInput"` should already be listed; add it if it isn't.
   For the editor-only retarget setup command, also add, inside `if (Target.bBuildEditor) { ... }`,
   `"UnrealEd"`, `"AssetRegistry"`, `"IKRig"`, `"IKRigEditor"` and `"SlateCore"` to `PrivateDependencyModuleNames`.
6. Right-click `CookieGame.uproject` → **Generate Visual Studio project files**, then double-click
   `CookieGame.uproject` and let it rebuild.

## 3. Open the house and play

- **Project Settings → Maps & Modes**: **Default GameMode** = `CookieGameMode`, and both **Editor Startup Map**
  and **Game Default Map** = `/Game/Cookie/Lvl_House`.
- Open `Lvl_House` (its World Settings already use `CookieGameMode`) and press **Play**.

The house is a 10 × 7.5 m cottage built from the Low Poly Interior kit (Outliner folders `House/Walls`,
`House/Floors`, `House/Furniture/<Room>`, `House/Doors`, `House/Roof`, `House/Exterior`): kitchen and living
room on the west, a 1 m hallway from the front door, bedroom and bathroom on the east, all densely furnished.
Each wall is a single kit piece (no doubled slabs). `M_TwoToneWall` paints each face of a wall its own room's
color, based on which way the face points (the `MI_Wall_<RoomOnPlusY>_<OtherSide>` instances in
`Content/Cookie/House/Walls/`). Doors hang fully open against the room side, so every doorway has 85 cm or
more clear width for the 60 cm player. The yard (`Yard` folder) has noise-shaded grass (`M_Grass`), a
posts-and-rails fence merged into one mesh, trees, bushes and a mailbox.

Furniture placements are kept as data in `docs/house_furniture.json` (room, label, kit mesh,
position, yaw, scale), which a Unreal MCP script reads to rebuild `House/Furniture`.

### The neighborhood

The `Neighborhood` actor in `Lvl_House` (`ACookieNeighborhood`) generates the street around the house: 11 more
lots (five beside the player's house, six across the road, all 18 m wide), each with a house shell from one of
four designs (a bungalow like the player's, a deep house with a front gable, a narrow two-storey, a long ranch),
painted from an eight-colour palette, with a fence or hedge, path, mailbox, bushes and two to four trees. Shells are
built from the same kit walls, door, roof slabs, ramp gables, porch and window boxes as the player's house; a dark
box behind the windows hides that there are no rooms. Then a road with lane dashes, raised sidewalks and street lamps,
a cul-de-sac with a planted island at the east end, a park past it (open lawn ringed with trees, where factories
and mountains have room), tree lines behind the lots, and a lawn under everything. Every piece is an instance (one
component per mesh and colour), rebuilt from code when play starts; change **Seed** on the actor for a different mix.

Trees and bushes everywhere use faceted low-poly pieces in the kit's style (`Content/Cookie/Foliage`), generated by
[`scripts/gen_foliage_meshes.ps1`](scripts/gen_foliage_meshes.ps1) as OBJ files and imported: jittered leaf clumps,

The bakery and factory are generated the same way by [`scripts/gen_building_meshes.ps1`](scripts/gen_building_meshes.ps1)
(OBJ + MTL, one slot per material instance, named after it), imported, then given their material instances by slot
name and convex collision. Thrown cookie trees lock pitch and roll (`bStayUpright`), so they fly and land standing.
a tapered limb, and a drooping pine tier, composed into `SM_Tree_Round` and `SM_Tree_Pine`.

### Big producers and mountains

| Item | Cost | Cookies/s | Model (`Content/Cookie/Items`) | Where the cookies go |
|---|---|---|---|---|
| Cookie tree | 1,500 | 15 | `SM_CookieTree`: the round tree with cookies on its canopy | Drop out of the canopy onto a heap round the trunk |
| Bakery | 25,000 | 120 | `SM_BakeryShop`: pink shopfront with display windows, scalloped striped awning, big cookie sign (the game's cookie), Dutch-gable false front, chocolate roof with icing drips | Tumble out of the front door onto a heap in front of it |
| Cookie factory | 400,000 | 1,000 | `SM_CookieFactory`: brick hall, sawtooth glass roof, smokestacks, a 9 m game cookie standing on the roof as its sign, a massive yellow launch chute | Launched out of the chute ~29 m into the air, landing on a mountain ~50 m away |
| Cookie cloud | 6,000,000 | 8,000 | `SM_CookieCloud`: ~50 m of faceted white puffs with a lumpy, rounded belly, 38 game cookies studded over the top, sides and underside, glowing `M_CookieCloud` so the underside stays white | Rains from all across the underside onto a heap below |

Bought clouds don't land: they glide ~55 m ahead, turn broadside to the player and hover ~40 m above whatever is underneath
(`bFloats`, `FloatDistance`, `FloatHeight`), then stay there. The cloud shell comes from the same generator script.

The models are merged meshes built in the editor from kit pieces, engine shapes and the foliage pieces, with convex
collision so they can be thrown. Each item's **Spouts** (in its settings) say where cookies leave, where they land, how
long they fly and how many are drawn flying per second; past that rate each drawn cookie stands for several, so a
factory's 1,000 a second stay cheap. Every cookie is banked the moment it launches and joins the spout's heap when
it lands, so the bank always equals what's physically out there (heaps, loose cookies, and cookies in the air).

Heaps outside have no size limit, merge when they overlap, and swallow houses. Their whole visible surface is real
cookies: two layers laid on the heap's shape (a rounded cone with lumps), drawn as Nanite instances that shadow each
other; a 6-million-cookie heap (29 m across, 16 m tall) has ~450,000. Under them, a procedural core in the heap's exact
shape gives it a shadow, fills any gap with cookie colour, and is solid, so you can climb mountains (the player walks
on 52-degree slopes). Growing heaps re-lay their cookies a few milliseconds per frame (**Piles → Pile Build Budget**).
Indoor heaps (inside the player's house) stay capped at 3,000 cookies each and spread across the room instead.

### Filming the mountain demo

Tick **Project Settings → Game → Cookie Game → Demo → Demo Mode** and press Play: the street starts with three
cookie mountains (12M by the house, 4M behind it, 10M in the park) and 4M cookies in the bank, four factories,
two bakeries and two cookie trees already spewing, at 3× production. Untick it for the normal game. 47 fps on the T4.

The scene itself is editable under the same heading (only used while Demo Mode is on):

- **Demo Placements**: items standing from the start, already producing (each counts as owned).
- **Demo Heaps**: heaps already there at start (not counted in the bank).
- **Demo Starting Cookies**: the bank at start (4,000,000). Spending thins the heaps as usual.
- **Demo Production Multiplier**: speeds up all production (3 by default), e.g. 10 to watch mountains grow in minutes.

The built-in scene is set in `UCookieGameSettings`'s constructor. Editing the lists in Project Settings saves your
version to `Config/DefaultGame.ini`; delete the `Demo...` lines there to go back to the built-in one.
Big heaps take a few seconds to lay out their cookies when play starts. In a standalone run,
`-ExecCmds="EnableCheats, BugItGo -3800 -1650 300 6 0 0, Walk"` puts the camera on the road looking at that mountain.

The template's own character, weapon, and pickups go unused. `ACookieCharacter` replaces them.

## Controls (so far)

| Input | Action |
|---|---|
| WASD + mouse, Space | Move, look, jump |
| Left-click | Shoot a cookie (+1) |
| Scroll wheel / 1–9 | Select hotbar slot (1 Toaster Oven, 2 Grandma, 3 Cookie Tree, 4 Bakery, 5 Cookie Factory, 6 Cookie Cloud) |
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
| `CookieGameMode` | Cookie bank, owned counts, cost formula, stress test, demo placements and heaps, stats log |
| `CookieCharacter` | First-person movement, input (built in code, no assets), shoot, hold-to-buy |
| `CookieLaunchable` | A thrown physics object (cookie / oven / grandma placeholder). Freezes into an instance once still |
| `CookieSettledManager` | Everything at rest, as instanced meshes; settled items keep popping cookies; big producers' spouts; ghost (non-physics) flying cookies; heaps and mountains (surface layout, cores, merging) |
| `CookieNeighborhood` | Generates the street: house shells, yards, road, lamps, cul-de-sac, park, tree lines |
| `CookieGrandma` | Ragdoll grandma: flies, gets up (a fall animation played backwards), bakes. Frozen into instances when not nearby |
| `CookieHUD` / `SCookieHUDWidget` | Slate HUD in Lilita One (`Content/Fonts`, OFL): gradient cookie count with a cookie and a per-second pill; a portrait card per item (picture, cost, owned, +X/s, key badge; grey when unaffordable, glowing when selected, pulsing on purchase); "+1" click, "+N" purchase and falling "-cost" pop-ups (rapid-fire purchases add up) |
| `CookieIconStudio` | Renders the HUD pictures from the real models (and the cookie) once at the start of play, high above the map |

## Working with Claude on the rented machine

Unreal can't run in a cloud container without a GPU, so do the build-and-test loop on the rented PC:
open a terminal in the project folder and run `claude`. It will read `CLAUDE.md`, can compile, read
build errors, and carry on from the next build step. If you also set up an Unreal MCP server there, Claude can
drive the editor directly (place actors, build the house layout).
