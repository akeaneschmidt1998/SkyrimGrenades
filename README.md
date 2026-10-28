# Fire Pots

Craftable bombs for Skyrim SE/AE (SKSE plugin, CommonLibSSE-NG). Pick a trigger, a payload and a
strength; throw them with a wind-up, or set tripwires.

## Bombs

A bomb is **trigger × payload × strength**, and every combination is its own item
("Moderate Fire Bomb (Short Fuse)"): 4 × 7 × 3 = 84 kinds.

| Trigger | What it does | Made with (per mild bomb) |
|---|---|---|
| Impact (thrown) | bursts on contact | 1 Fire Salts |
| Short fuse (thrown) | bounces and rolls, bursts after 1.5 s | 1 Paper Roll, 1 Charcoal |
| Long fuse (thrown) | bursts after 4 s | 2 Paper Roll, 1 Charcoal |
| Tripwire (placed) | set down on the ground ahead of you; bursts when someone comes within ~1.7 m | 1 Linen Wrap, 1 Fire Salts |

| Payload | What it does | Filling (any one, per mild bomb) |
|---|---|---|
| Frag | shrapnel: physical damage and a shove | Nails, Quarried Stone (Hearthfire) or a Soul Gem Fragment |
| Fire | fire damage and burning ground | Fire Salts; or 2 Charred Skeever Hide / Ash Creep Cluster / Burnt Spriggan Wood / Spawn Ash |
| Oil | a slick (60 s) that slows anyone in it; a fire bomb bursting in it sets it ablaze | Dwarven Oil, Troll Fat; or 2 Ash Hopper Jelly |
| Smoke | a lingering cloud: anyone inside is invisible (ends when they attack), loses stamina and gets no stamina regen | Charcoal; or 2 Tundra Cotton |
| Fear | a blinding flash; everyone up to level 10/20/30 flees | Ectoplasm, Glow Dust; or 2 Nightshade / Deathbell |
| Frenzy | everyone up to level 10/20/30 attacks whoever is nearest | Hagraven Feathers, Falmer Ear; or 2 Powdered Mammoth Tusk |
| Concussion | wide stagger (1.75× the radius), very loud | Void Salts, Briar Heart; or 2 Bone Meal |

| Strength | Ingredients | Effect |
|---|---|---|
| Mild | as listed | 2 m blast |
| Moderate | 2× everything (not the container) | 3 m blast, double damage, longer effects |
| Major | 3× | 4 m blast, triple damage, longest effects |

Every bomb also takes **one empty container**: any empty wine bottle, clay or Nordic jug, glazed or
Nordic pot. It's picked for you. **Drinking wine gives its empty bottle back.**

**Getting bombs:** make them anywhere with **H**, or buy them: general goods merchants (fire, frag,
oil, smoke, tripwires), apothecaries (fire, fear, frenzy), court wizards (concussion, fear). Bandits
and Forsworn carry a few. All in `[Loot]`.

## Controls

- **H**: make bombs, anywhere: *what kind* (each shows what it takes) → *payload* (with how many
  you could make) → *filling* (only if you carry more than one) → *strength* (with what each takes)
  → *how many*. It always opens, so it's also the recipe book.
- **Bombs are scrolls**: Scrolls tab; buy, sell, stash, favorite and **equip to a hand**. With a
  bomb in a hand, **hold that hand's attack button** to wind up (the hand comes up like readying a
  spell) and **release** to throw (the spell-release motion). A tripwire is set down instead.
- **G**: hold to wind up, release to throw, without equipping anything (no hand animation).
- **V**: next kind of bomb you carry (swaps the one in your hand, if any).
- Throws are soft: a full wind-up lands about 12 m away on flat ground.
- **Throw preview** (arc + landing ring, or a tripwire's trigger ring) is **off by default**; turn it
  on in the MCM. The small wind-up meter shows either way.

## Settings (MCM)

*Mod Configuration → Fire Pots* (SkyUI + MCM Helper): keys, throw speeds, wind-up time, loft,
tripwire reach, the preview, messages, crafting in combat, bottle return, merchants and loot.
Changes apply when you close the menu (merchants/loot on the next game start). Everything else
(ingredient lists, loot lists, models) is in `SKSE/Plugins/FirePots.ini`.

## Getting a build without compiling

Every push builds on GitHub (Actions tab → latest **Build** run → *Artifacts*). Download
**FirePots-mod**: it contains `FirePots.esp`, `Scripts/`, `MCM/` and `SKSE/Plugins/` (DLL + INI), so
install it directly in MO2 / Vortex. **FirePots-pdb** has debug symbols for crash logs.

## Requirements

- **SKSE** and **Address Library for SKSE Plugins** for your game version (Steam 1.7.104: SKSE
  2.3.1+, Address Library *All in One* v13+).
- `FirePots.esp` is ESL-flagged (takes no load order slot); its only master is Skyrim.esm.
- For the menu: **SkyUI** and **MCM Helper**. Without them everything works; you edit the INI instead.
- HearthFire's Nails and Quarried Stone are frag fillings; every SE/AE copy has HearthFire.
- Skyrim VR is not supported.

## Build (Windows)

Same toolchain as DaggerfallCombat: Visual Studio 2022 (*Desktop development with C++*), vcpkg with
`VCPKG_ROOT` set, Git. Then:

```
cmake --preset release
cmake --build --preset release
```

The DLL lands in `dist/SKSE/Plugins/`. Add `-DMOD_OUTPUT_DIR="C:/path/to/MO2/mods/Fire Pots"` to the
configure step to copy it into your mod folder on every build. The ESP is generated with
`dotnet run --project tools/MakePlugin -c Release -- <folder>` (CI does this).

Offline checks (no game needed; `ballistics_test` also builds with plain g++/clang):

```
cmake --build --preset release --target ballistics_test arc_preview
build\release\ballistics_test.exe
build\release\arc_preview.exe .
```

## Test checklist

Log: `Documents/My Games/Skyrim Special Edition/SKSE/FirePots.log`.

1. Log shows `FirePots loaded`, `84 of 84 bombs ready`, `Leveled lists: 15 entries added`,
   `Watching for MCM changes`. The MCM lists *Fire Pots* with three pages.
2. Press H with nothing: the menu opens; *Thrown, impact* → *Fire* → *Fire Salts* → *Mild*: it
   says what one takes (an empty container + 2 Fire Salts: one for the trigger, one for the filling).
3. With an empty wine bottle, 4 Fire Salts: *impact* → *Fire (can make 2)* → *Mild (can make 2)*
   → *1*: one "Mild Fire Bomb (Impact)", a bottle and 2 salts gone.
4. Throw it with G: soft arc, bursts on contact, burning ground. With the preview on (MCM), the arc
   and a ring show while winding up.
5. A short-fuse frag bomb bounces and bursts ~1.5 s after landing.
6. A tripwire (G, hold, release): it's set down a few steps ahead; a bandit walking over it sets it
   off. (Check whether walking over your own sets it off, and tell me.)
7. Oil then fire on the same spot: the slick slows; the fire bomb sets it ablaze (log: `Oil slick
   ... ignited`).
8. Smoke: standing in the cloud you're invisible, stamina drains and doesn't regenerate; attacking
   ends the invisibility.
9. Fear / frenzy on a group of bandits; concussion staggers everything in a wide ring and is loud.
10. Equip a bomb to the left hand, hold right-click, release: the hand's release motion, the bomb
    flies. V swaps the bomb in the hand.

## Known unknowns (only an in-game test can settle these)

- **Tripwires and the player.** They use the engine's proximity trigger (like runes); whether your
  own steps set it off isn't known yet.
- **Fuse bombs' preview** shows the first bounce, not where they roll to.
- **Oil only ignites from fire bombs**, not fire spells, torches or enchanted weapons yet.
- **Hand throws ride on a scroll cast** (0.3 s charge, an invisible do-nothing projectile); the
  mod launches the real bomb when it sees the scroll used up.
- **Borrowed visuals** (`[Visuals]`) may not all look right; the oil slick has no model of its own,
  just a dark stain.

## Next

- A real overhand throw animation through Open Animation Replacer (needs your
  `Skyrim - Animations.bsa`).
- NPC bombers.
- Fire spells and torches igniting oil.
