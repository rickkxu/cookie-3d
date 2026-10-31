# Cookie Game — Design Spec (House Scope)

A first-person 3D clicker game in Unreal Engine 5.8. The core fun is **escalation you can see**: cookies, toaster ovens, and grandmas physically pile up until the house is overflowing.

This spec covers **the house only**. Do not build city, planet, or any later scale yet.

## Core feel

- Every number in the game is physical. If you own 400 grandmas, there are 400 grandmas in the house.
- Buying things should feel like firing a weapon. The signature moment: holding a key and rapid-firing grandmas across the kitchen in arcs.
- The world grows continuously. **No chapters, no level transitions, no menus that pause the game.**

## Player & controls

Start from the **First Person template**.

| Input | Action |
|---|---|
| WASD + mouse | Walk and look around freely |
| Left-click | Shoot a cookie (+1 cookie) |
| Scroll wheel / number keys 1–9 | Select an item on the hotbar |
| Hold right-click | Rapid-fire buy the selected item |

- **Shooting a cookie:** the cookie appears just in front of the camera at the center of view and launches forward in an arc, landing with physics.
- **Hold-to-buy:** buys one item immediately, then repeats while held. The repeat rate ramps up the longer the button is held (e.g. 2/sec → 10/sec+). Stops when out of cookies.
- **Purchased items launch from the player in an arc** in the look direction, same as cookies.

## Items (house scope)

| Item | Base cost | Produces | Notes |
|---|---|---|---|
| Cookie (click) | free | +1 per click | Left-click |
| Toaster oven | 15 | 0.5 cookies/sec | Clangs and bounces on landing, stacks where it rests |
| Grandma | 100 | 2 cookies/sec | Ragdolls on landing, then gets up and bakes where she landed |

- Each purchase raises that item's price by 15% (cost = base × 1.15^owned). All numbers are starting values; keep them easy to tune in one place.
- **Produced cookies are physical too:** helpers periodically emit a cookie that pops out near them, so production visibly adds to the mess.
- Items must keep producing wherever they land, including on top of other items, in the hallway, or outside.

Optional later house items (don't build until asked): stand mixer, double oven, roommate in an apron.

## HUD

Keep it minimal.

- **Top:** total cookies and cookies per second.
- **Bottom hotbar:** one slot per item showing key number, icon/name, current cost, and how many are owned. The selected slot is highlighted; slots the player can't afford are greyed out.
- No on-screen cookie button. No shop panel.

## The house

- Start in a kitchen with a table in front of the player, inside a small house the player can walk through (kitchen, living room, hallway, a bedroom, a bathroom) with a front door and a small yard.
- Things should naturally overflow: grandmas wedged in the hallway, an oven tower in the living room, cookies spilling out the door.

## Physics & performance (important)

The game will run on a rented cloud GPU, so stay efficient:

- **Freeze settled objects.** Once a cookie, oven, or grandma comes to rest, switch it from simulated physics to a static object. Only recently launched objects simulate.
- **Use instancing** for identical settled objects (instanced static meshes) so hundreds of grandmas stay cheap.
- **Cap physical cookies.** Past a threshold (start around 500), stop spawning new individual cookies and grow pile meshes instead, so the pile still visibly grows.
- **Animate only nearby grandmas.** Distant ones idle or stay still.
- Target a smooth framerate as counts climb; flag anything that drops performance hard.

## Art direction

- **Placeholders first:** simple shapes (cookie disc, oven box, grandma capsule) until mechanics feel good.
- Final look: stylized/cartoony, not photoreal. Cheaper to render and stands out from typical Unreal demos.
- Do not use the "Cookie Clicker" name or copy its art. This is its own game.

## Build order

1. Left-click shoots a cookie in an arc that lands with physics; cookie counter goes up.
2. Hold right-click rapid-fires grandmas (placeholder capsules) in arcs, with ramping rate and costs deducted.
3. Hotbar with scroll wheel and number-key selection; add toaster ovens.
4. Helpers produce cookies over time and emit physical cookies.
5. Freeze-when-settled + instancing; stress test with hundreds of items.
6. Grandma ragdoll on landing, then get up and bake.
7. Build out the full house layout.
8. Swap placeholders for stylized models, add sounds (crunch on cookie shot, clang for ovens, poof on spawn).

After each step, the game should be playable. Keep logic simple and readable; favor whatever approach works most reliably through the Unreal MCP.

## Out of scope (for now)

City, planet, or any scale beyond the house; movement upgrades; saving/loading; menus; multiplayer.
