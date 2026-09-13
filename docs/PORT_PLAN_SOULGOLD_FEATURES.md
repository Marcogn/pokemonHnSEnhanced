# Port plan: Soulgold features → Heart & Soul

Target repo: `marcogn/pokemonhnsenhanced` (this repo, referred to as **HnS**)
Source repo: `marcogn/soulgold` (referred to as **SG**), read-only reference
Branch for all work: `claude/soulgold-to-heart-soul-features-3kqrcd`

Features requested, in the order they should be implemented:

1. Overworld speed-up (1x–4x, hold a button for 1x)
2. Battle speed-up (1x–3x, hold a button for 1x)
3. Dark / Light UI with an options toggle (battle UI + Bag)
4. Party menu UI from Soulgold

Hard constraint from the request: **do not break the build.**

---

## 0. Facts established by inspection (read this before touching anything)

These were verified in the working tree; do not re-derive them, but do re-verify
any of them that a change of yours would invalidate.

### 0.1 The two projects do NOT share a base

| | HnS | SG |
|---|---|---|
| Base | "Modern Emerald" (pret pokeemerald + patches) | pokeemerald-expansion 1.15.2 (`include/constants/expansion.h`) |
| Battler API | legacy `gActiveBattler` global (`src/battle_main.c:3146` era code) | `enum BattlerId battler` parameter passing |
| Move data | `gBattleMoves` | `gMovesInfo` + `GetMoveName()` etc. |
| Item data | `ItemId_GetPocket()` / `gItems` | `GetItemPocket()` / `gItemsInfo` |
| Species data | `gSpeciesInfo` (present in both) | `gSpeciesInfo` |
| Gfx compression | LZ77 only (`LZDecompressWram` / `LZDecompressVram`) | LZ77 **and** `smol` (`tools/compresSmol`, `.4bpp.smol`, `.bin.smolTM`) |
| `COMPOUND_STRING` | **not available** | available |
| Options screen | `src/options_plus_menu.c` (3 sub-pages, 2125 lines) | `src/option_menu.c` (3 pages, 2018 lines) |

**Consequence:** nothing can be copied verbatim from SG. Every hunk needs an
adaptation pass. Budget for this in every phase.

### 0.2 Build system

* Sources are globbed: `Makefile:180` `C_SRCS_IN := $(wildcard $(C_SUBDIR)/*.c ...)`.
  New `src/*.c` files are picked up automatically — no Makefile edit needed.
  Files named `*.inc.c` are excluded (`Makefile:181`).
* `Makefile:43` sets `MODERN ?= 0`, but the **agbcc (`MODERN=0`) build is already
  broken in HnS today**, independently of this work: `src/scrcmd.c:2734`,
  `src/easy_chat.c:1486`, `src/region_map.c:282` and three other files use C99
  declarations in `for` initialisers, which agbcc (C89) rejects. `README.md:145`
  states this explicitly: *"use the modern compiler with `make modern`.
  Compiling using the old compiler won't work."*
  → **The build of record is `make modern`.** Do not attempt to restore the
  agbcc build as part of this work, and do not use it as a gate.
* `make modern` does **not** pass `-Werror` (`Makefile:110`); only the dead
  `MODERN=0` path does (`Makefile:103`). Warnings will not fail the build — read
  them anyway.
* `Makefile:426` already passes `--print-memory-usage` to the linker. Capture
  that line before and after every phase (see §1.2).

### 0.3 This container has no ARM toolchain

`arm-none-eabi-gcc` is absent and `$DEVKITARM` is unset. **You cannot compile
here.** That makes "do not break the build" a discipline problem, not a CI
problem. See §1 for the mandatory guardrails that replace a compiler.

### 0.4 Save data

* `src/save.c:81`: `STATIC_ASSERT(sizeof(struct SaveBlock2) <= SECTOR_DATA_SIZE)`
  with `SECTOR_DATA_SIZE == 4084` (`include/save.h:6`). There is headroom, but
  the assert is real — if you overflow it the build fails at compile time.
* HnS is a *released* hack. Inserting bitfields into the middle of
  `struct SaveBlock2` (`include/global.h:510`) shifts every field after it and
  **invalidates existing player saves**. The offset comments in that struct
  (`/*0x14*/`, `sizeof=0xF2C`) are already stale — do not trust them.
* **Therefore: store the new options in unused `VAR_`s, not in `SaveBlock2`.**
  Vars live in `SaveBlock1`, are already allocated, and default to 0 in old
  saves — which maps cleanly to "1x / off". This is also what SG does
  (`VAR_OVERWORLD_SPEEDUP`, `VAR_BATTLE_SPEED`).
  Free vars confirmed in `include/constants/vars.h`: `0x40A1`, `0x40A8`,
  `0x40B8`, `0x40BB`, `0x40DB`, `0x40DC`, `0x40FB`, `0x40FC`, plus
  `VAR_UNUSED_HNS_VAR4..7` (`0x409C`–`0x409F`).
  Free flags confirmed in `include/constants/flags.h` (`FLAG_UNUSED_0x94F`+).
* The one option that genuinely cannot be a var is the party-menu style if you
  want it readable before `SaveBlock1` is loaded — see §5.4 for SG's
  magic-byte pattern (`PARTY_MENU_OPTION_SAVE_MAGIC`,
  `src/party_menu_dispatch.c:24`).

### 0.5 Button conflicts (HnS-specific, differs from SG)

SG uses **hold R** to force overworld 1x and **hold L** to force battle 1x.
In HnS:

* Overworld: `R` is read at `src/field_control_avatar.c:133` →
  `input->pressedRButton`, consumed at `:222` only while on a bike.
  `L` is **unused** in the overworld.
* Battle: `R` is the last-used-ball throw and ball-swap modifier
  (`src/battle_controller_player.c:313,321,333,345,351,461`).
  `L+R` together is the quick-run combo (`src/battle_main.c:4305`).
  `L` alone is **unused**.

**Decision: use `L` as the "hold for 1x" modifier in both the overworld and
battle.** Do not use `R`. A player holding `L` and then pressing `R` still gets
the quick-run combo, which is fine and arguably desirable.
Document the chosen button in the option's description text.

---

## 1. Phase 0 — guardrails (do this first, it is not optional)

### 1.1 Add a CI build

HnS has **no** `.github/` directory. SG has `.github/workflows/build.yml`.
Create `.github/workflows/build.yml` in HnS that:

* installs devkitARM (devkitARM r65 — `INSTALL.md:120-132` pins this version),
* runs `make tools`,
* runs `make modern -j$(nproc)`,
* uploads the `.gba` and the linker `--print-memory-usage` output as artifacts.

Use SG's `.github/workflows/build.yml` as the structural reference, but adapt:
HnS has no `config.mk`, no test runner, no `mgba-rom-test-hydra`, and builds
with `make modern`, not `make`.

This is the single highest-value item in the whole plan. Land it as its own
commit **before** any feature work, so every later commit is verified by
something other than reading.

### 1.2 Record a baseline

On a clean checkout of `main`, with CI green, record:

* the linker memory-usage line (ROM / EWRAM / IWRAM used),
* the ROM size.

Append both to `docs/PORT_PLAN_SOULGOLD_FEATURES.md` under a "Baseline" heading,
or to the PR body. After each phase, compare. EWRAM and IWRAM are the tight
resources on GBA; ROM is 32 MiB and unlikely to be the binding constraint, but
Phase 5 adds ~12k lines of code and must be checked.

### 1.3 Commit hygiene

One phase = one commit (or a short series), each independently building.
Never squash Phase 5 into the same commit as Phases 1–4: if the party menu has
to be reverted, the speed-ups and the dark UI must survive.

---

## 2. Phase 1 — Overworld speed-up

**Risk: low. Effort: ~150 lines. Do this first.**

### 2.1 SG reference

| What | Where in SG |
|---|---|
| Constants | `include/overworld.h:31-38` |
| Implementation | `src/overworld.c:1801-1852` (`OverworldSpeedup_AdditionalIterations`, `CB2_Overworld`) |
| Transition reuse | `src/battle_transition.c:301-309` (`GetBattleTransitionSpeedScale`) |
| Option storage | `VAR_OVERWORLD_SPEEDUP` (`include/constants/vars.h:211`) |
| Guard flag | `FLAG_PREVENT_OVERWORLD_SPEEDUP` (`include/constants/flags.h:1533`) |
| Option UI | `src/option_menu.c:1434-1476` |

### 2.2 Target sites in HnS

`src/overworld.c`: `OverworldBasic()` and `CB2_Overworld()` are structurally
identical to SG's (verified). The port is nearly mechanical.

### 2.3 Steps

1. `include/constants/vars.h`: rename `VAR_UNUSED_0x40BB` →
   `VAR_OVERWORLD_SPEEDUP` (keep the same numeric value `0x40BB`).
   Keep the old name as an alias `#define` if anything still references it —
   grep first.
2. `include/constants/flags.h`: rename one `FLAG_UNUSED_0x9xx` →
   `FLAG_PREVENT_OVERWORLD_SPEEDUP` for scripts that need to suppress the
   speed-up (cutscenes, timed sequences).
3. `include/overworld.h`: add the 8 constants from SG `include/overworld.h:31-38`
   verbatim, plus `u8 OverworldSpeedup_AdditionalIterations(u16 speed, bool32 overworld);`
4. `src/overworld.c`: add `OverworldSpeedup_AdditionalIterations()` adapted from
   SG `src/overworld.c:1801`. **Changes required:**
   * Replace `JOY_HELD(R_BUTTON)` with `JOY_HELD(L_BUTTON)` (see §0.5).
   * **Delete** the `FlagGet(FLAG_DEXNAV_SEARCHING)` condition — HnS has no
     DexNav (`src/dexnav.c` does not exist, no `FLAG_DEXNAV_*` in
     `include/constants/flags.h`).
   * Keep the `ArePlayerFieldControlsLocked()` gate in `CB2_Overworld`
     (`include/script.h:37` — confirmed present in HnS).
5. `src/overworld.c`: in `CB2_Overworld()`, insert the extra-iteration loop
   after `OverworldBasic()` exactly as SG does. Only
   `AnimateSprites(); CameraUpdate(); UpdateCameraPanning();` are repeated —
   **do not** repeat `ScriptContext_RunScript()`, `RunTasks()`,
   `BuildOamBuffer()` or `UpdatePaletteFade()`. That is deliberate: scripts,
   text and fades stay at real time, only movement accelerates.
6. *(Optional, recommended)* `src/battle_transition.c`: add
   `GetBattleTransitionSpeedScale()` and the `for` loops from SG
   (`src/battle_transition.c:1839, 2697, 2706, 3791`) so transitions are not a
   jarring slow spot. Check HnS's transition functions individually — they are
   not guaranteed to be the same set.

### 2.4 Option entry

Add to `src/options_plus_menu.c`, page `MENU_MAIN` (it is an overworld setting):

* enum member `MENUITEM_MAIN_OVERWORLD_SPEED` in the `MENUITEM_MAIN_*` enum
  (`src/options_plus_menu.c:35-51`), **before** `MENUITEM_MAIN_COUNT`.
* name string: `static const u8 sText_OptionOverworldSpeed[] = _("OW speed");`
  (HnS has no `COMPOUND_STRING`; follow the `sText_Option*` convention already
  in the file).
* entry in `sOptionMenuItemsNamesMain[]`.
* description string + entry in the descriptions table, e.g.
  `"Overworld movement and animation\nspeed. Hold L for 1x."`
* choice strings `sText_Speed1x..4x` and a
  `static const u8 *const sOverworldSpeedStrings[] = {...}`.
* `DrawChoices_OverworldSpeed(int selection, int y)` — reuse the existing
  `DrawChoices_Options_Four(sOverworldSpeedStrings, selection, y, active)`
  helper (see `DrawChoices_Run_Type`, `src/options_plus_menu.c`, for the exact
  pattern, including the `CheckConditions()` call).
* `sItemFunctionsMain[MENUITEM_MAIN_OVERWORLD_SPEED] = {DrawChoices_OverworldSpeed, ProcessInput_Options_Four}`.
* load in the "read current settings" block (around
  `src/options_plus_menu.c:872`): `sOptions->sel[MENUITEM_MAIN_OVERWORLD_SPEED] = VarGet(VAR_OVERWORLD_SPEEDUP);`
* save in the apply block (around `src/options_plus_menu.c:1112`):
  `VarSet(VAR_OVERWORLD_SPEEDUP, sOptions->sel[MENUITEM_MAIN_OVERWORLD_SPEED]);`
* `src/new_game.c`: set the default. SG defaults the overworld to 1x
  (`src/new_game.c:232-233`). Recommend `VarSet(VAR_OVERWORLD_SPEEDUP, OPTIONS_OVERWORLD_SPEED_1X)`
  so new players see stock behaviour until they opt in.

> Note on style: several existing `DrawChoices_*` functions in HnS write to
> `gSaveBlock2Ptr` from inside the draw call. That is a pre-existing smell.
> Use the `sel[]` + apply-on-exit path for the new options; do **not** copy the
> write-while-drawing pattern.

### 2.5 Acceptance

* `make modern` green, memory usage delta ≈ 0.
* In game: 1x is byte-identical behaviour to today.
* 2x/3x/4x accelerate walking, running, surfing and NPC animation.
* Holding `L` drops to 1x instantly.
* Text boxes, scripted cutscenes and fades run at normal speed at every setting.
* `FLAG_PREVENT_OVERWORLD_SPEEDUP` set → forced 1x.

---

## 3. Phase 2 — Battle speed-up

**Risk: medium. Effort: ~200 lines. Depends on Phase 1 only for the option
plumbing pattern.**

### 3.1 Why SG's approach is the right one to copy

SG does **not** patch every animation's frame counters (the original "Rogue"
approach). Instead it runs the battle's *software* tick N times per hardware
frame and lets only the real VBlank upload to hardware. That is far less
invasive and far easier to keep correct. Reference:

| What | Where in SG |
|---|---|
| Main loop | `src/battle_main.c:1779-1824` (`BattleMainCB2`) |
| Tick body | `src/battle_main.c:1826-1834` (`RunBattleSoftwareTick`) |
| RNG parity | `src/battle_main.c:1836-1850` (`AdvanceBattleFrameRng`) |
| Safety gate | `src/battle_main.c:1852-1878` (`CanRunExtraBattleTick`) |
| Scale lookup | `src/battle_controllers.c:3028-3075` (`Rogue_GetBattleSpeedScale`) |
| Helpers | `src/battle_main.c:3136-3144` (`InBattleChoosingMoves`, `InBattleRunningActions`) |
| Fade gate | `src/palette.c:104` (`IsPaletteFadeTransferPending`) |
| VBlank RNG | `src/main.c:391-405` |

### 3.2 What HnS is missing and how to supply it

| SG symbol | HnS status | Action |
|---|---|---|
| `InBattleChoosingMoves()` | absent | add to `src/battle_main.c`, `return gBattleMainFunc == HandleTurnActionSelectionState;` — that static exists in HnS |
| `InBattleRunningActions()` | absent | same pattern with `RunTurnActionsFunctions` |
| `IsPaletteFadeTransferPending()` | absent | HnS `src/palette.c` has no `sPlttBufferTransferPending`. Add the flag + accessor, mirroring SG `src/palette.c:95-107`. **Verify HnS's `UpdatePaletteFade` sets `gPaletteFade.multipurpose1` the same way before copying this.** If it does not, gate extra ticks on `gPaletteFade.active` alone and note the reduced safety in a comment. |
| `AdvanceRandom()` | absent | HnS uses the vanilla LCG `Random()` (`src/main.c:363`, `src/battle_main.c:2736`). Do **not** port SG's SFC32 RNG. Instead, see §3.4. |
| `gTestRunnerEnabled` | absent | drop the condition entirely; HnS has no test runner |
| `gBattleStruct->hasBattleInputStarted` | absent | add a `u8 hasBattleInputStarted:1;` to `struct BattleStruct` in `include/battle.h` |
| `gBattleSpritesDataPtr->animationData->captureSuccessAnimActive` | **verify** | HnS has `struct BattleAnimationInfo` at `include/battle.h:543` with `ballThrowCaseId` at `:552`, but the field name may differ. Grep before using; if the field is absent, gate on `gBattleResults.caughtMonSpecies` only. |
| `BtlController_Complete`, `enum BattlerId` signatures | different | not needed — the speed-up touches only `BattleMainCB2`, not the controllers |

### 3.3 Steps

1. `include/constants/vars.h`: rename `VAR_UNUSED_0x40DC` → `VAR_BATTLE_SPEED`
   (SG uses the same address, `include/constants/vars.h:240` — convenient but
   coincidental).
2. `include/constants/global.h`: add `OPTIONS_BATTLE_SCENE_1X..4X`,
   `_DISABLED`, `_COUNT` from SG `include/constants/global.h:211-216`.
   HnS keeps its own separate `optionsBattleSceneOff` bit — the two are
   independent; leave `optionsBattleSceneOff` alone.
3. `include/battle.h`: add `hasBattleInputStarted` to `struct BattleStruct`.
   Append it to an existing bitfield run so the struct does not grow; `BattleStruct`
   is heap-allocated, not saved, so layout changes are safe here.
4. `src/battle_main.c`:
   * add `InBattleChoosingMoves()` / `InBattleRunningActions()` and declare them
     in `include/battle_main.h`;
   * add `static void RunBattleSoftwareTick(void)` containing exactly the five
     calls currently inlined in `BattleMainCB2` (`src/battle_main.c:2253-2257`);
   * add `static bool32 CanRunExtraBattleTick(void)` adapted per §3.2;
   * rewrite `BattleMainCB2()` (`src/battle_main.c:2251`) to the SG loop shape.
     **Preserve HnS's recorded-battle B-button block at the end unchanged**
     (`src/battle_main.c:2259-2266`).
5. Add `GetBattleSpeedScale(bool32 forHealthbar)` — put it in `src/battle_main.c`
   rather than `src/battle_controllers.c` to keep the change in one file;
   declare in `include/battle_main.h`. Adapt from SG
   `src/battle_controllers.c:3028`:
   * `VarGet(VAR_BATTLE_SPEED)` instead of `VarGet(B_BATTLE_SPEED)` (HnS has no
     `include/config/battle.h`);
   * `JOY_HELD(L_BUTTON)` → return 1 (this is SG's behaviour already, and `L`
     alone is free in HnS battles — see §0.5);
   * keep the `InBattleChoosingMoves()` → 1x rule: move selection must stay at
     normal speed or the menu becomes unusable;
   * you may drop the `OPTIONS_BATTLE_SCENE_DISABLED` branch since HnS keeps
     "battle scene off" as a separate option.
6. **Do not** touch `src/main.c`'s VBlank RNG or `VBlankCB_Battle`
   (`src/battle_main.c:2733`) in the first cut — see §3.4.

### 3.4 RNG: the one real correctness trap

In HnS, `Random()` is called once per VBlank in `src/main.c:363` **and** once in
`VBlankCB_Battle` (`src/battle_main.c:2736`). Those calls are what make "waiting
at the battle menu changes the outcome" true. They are tied to *hardware*
frames, not logical ticks. Running 3 logical ticks per hardware frame therefore
consumes **the same** RNG values as 1 tick would — battles at 3x will resolve
differently from battles at 1x, but each speed is internally consistent and
neither is broken.

SG solves this by moving the burns to the logical frame boundary
(`AdvanceBattleFrameRng`, `src/battle_main.c:1836`). That is the correct fix but
it requires touching the global VBlank handler, which is the highest-blast-radius
file in the project.

**Recommendation: ship Phase 2 without the RNG change**, document the behaviour
in the option description ("battle outcomes may differ between speeds"), and
only revisit if the author considers speed-dependent RNG a defect. If they do,
the change is: move both `Random()` calls out of the VBlank handlers and into an
`AdvanceBattleFrameRng()` called once per logical tick in `BattleMainCB2`,
guarded by `!(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_RECORDED))`.
**Do it as a separate, clearly-labelled commit** so it can be reverted alone.

### 3.5 Things that will break if you are careless

* **Link battles.** `CanRunExtraBattleTick()` must return `FALSE` for
  `BATTLE_TYPE_LINK` — link pacing is frame-locked. SG does this; keep it.
* **Palette fades.** A fade needs a hardware transfer between updates. If you
  run extra ticks during a fade, fades visibly skip steps. Keep the
  `gPaletteFade.active` gate.
* **Capture animation.** The ball-shake/star sequence samples sprite state per
  frame and aliases badly when accelerated. Keep the
  `gBattleResults.caughtMonSpecies` gate.
* **HnS-specific battle features.** HnS has ball prompts, quick-run combos,
  `optionsFastBattle`, `optionsEvenFasterJoy`. These already manipulate battle
  timing (`src/battle_anim_utility_funcs.c:546`,
  `src/battle_script_commands.c:2681,4606`). Test the speed-up with each of
  those toggles both on and off. `optionsFastBattle` + 3x is the combination
  most likely to expose a text-too-fast-to-read problem.
* **Callback re-entrancy.** SG re-checks `gMain.inBattle`, `gMain.callback1` and
  `gMain.callback2` after every tick, because a task can leave the battle
  mid-loop. Copy those checks verbatim; removing them causes use-after-free.

### 3.6 Option entry

Same recipe as §2.4, but on the `MENU_CUSTOM` (battle) page:
`MENUITEM_BATTLE_SPEED`, `sItemFunctionsCustom`, `sel_battle[]`,
`ProcessInput_Options_Four` (expose 1x/2x/3x + Off-equivalent, or add a
three-option helper — SG exposes only 1x/2x/3x, `src/option_menu.c:1478-1530`).
Default in `src/new_game.c`: `VarSet(VAR_BATTLE_SPEED, OPTIONS_BATTLE_SCENE_1X)`.

### 3.7 Acceptance

* `make modern` green.
* 1x is byte-identical to today's behaviour, including with
  `optionsFastBattle` / `optionsEvenFasterJoy` / battle-scene-off.
* 2x/3x accelerate animations, message delays and HP-bar drain.
* Move selection and the Bag/party sub-menus stay at 1x.
* Holding `L` drops to 1x.
* Wild capture sequence, Mega/form-change scenes, evolution-after-battle and
  the level-up/learn-move flow all complete without visual corruption at 3x.
* A link battle (if testable) still runs at 1x and does not desync.

---

## 4. Phase 3 — Dark / Light UI

**Risk: medium. Effort: ~10 files, mostly palette data. The hard part is asset
archaeology, not logic.**

### 4.1 Scope, honestly stated

In SG this is not a general theme engine. It is a targeted re-palette of:

* the battle message box, command menu and move-description window,
* the healthboxes (HP bar + status icons, via a 4bpp **index remap**, not a
  palette swap),
* the level-up stat window and a few battle-script windows,
* the Bag screen (background palette, pocket indicators, HM icon, TM/HM info
  text).

It does **not** touch the overworld, the Pokédex, the summary screen, the PC,
or the party menu. Setting expectations with the author up front is part of this
phase.

### 4.2 SG reference map

| Area | SG location |
|---|---|
| Option bit | `include/global.h:633` `optionsDarkBattleUi:1` |
| Colour constants | `src/battle_bg.c:36` `DARK_BATTLE_UI_BG_COLOR`, `:58-70` |
| Battle window pals | `src/battle_bg.c:804-843` (`LoadBattleMenuWindowGfx`, `LoadBattleMoveDescriptionWindowGfx`) |
| Window pal index | `include/battle_bg.h:5` `BATTLE_WINDOW_DARK_BG_PAL_INDEX 8` |
| Healthbox remap | `src/battle_interface.c:46-54` (index constants), `:905-1005` (`IsDarkHealthbox`, `RemapHealthbarGfxIndexes`, `CopyHealthbarGfx`, `CopyStatusIconGfx`) |
| Healthbox pal pick | `src/battle_gfx_sfx_util.c:91` |
| Message text | `src/battle_message.c:2231` |
| Player controller | `src/battle_controller_player.c:1939-1986` |
| Level-up window | `src/battle_script_commands.c:7287-7296`, `:12120-12171`, `:16544-16557` |
| Day/night interaction | `src/palette.c:1386` |
| Bag | `src/item_menu.c:62-64`, `:643-681` (palettes), `:1148-1156`, `:1310`, `:1394-1399`, `:1850-1859`, `:3660-3666`, `:3765` |
| Bag dark palettes | `graphics/bag/menu_male_dark.pal`, `graphics/bag/menu_female_dark.pal` (367/369 bytes) |
| Option UI | `src/option_menu.c:1614-1615`, help text at `:411` |

### 4.3 The part that is NOT a copy: healthbox index remapping

SG's `RemapHealthbarGfxIndexes` hardcodes palette indices
(`HEALTHBAR_LIGHT_COLOR_1 = 1`, `HEALTHBAR_DARK_COLOR_1 = 5`,
`STATUS_ICON_LIGHT_COLOR_1 = 4`, `STATUS_ICON_DARK_COLOR_1 = 5`). Those numbers
are specific to SG's healthbox art.

**HnS has two healthbox styles**, selected at runtime by
`gSaveBlock2Ptr->optionsNewBattleUI` (`include/global.h:571`, consumed in ~24
places across `src/battle_interface.c` and `src/battle_gfx_sfx_util.c`).

So before writing any code:

1. Open HnS's healthbox and status-icon graphics (find them via the INCBINs in
   `src/battle_interface.c` / `src/graphics.c`) and determine, **for each of the
   two styles**, which palette indices carry the light-background pixels.
2. Decide whether "Dark" applies to both styles or only one. Simplest defensible
   answer: implement dark for both, with a separate index pair per style.
3. If the indices cannot be made to work for one style, gate the Dark option
   off for that style via the existing `CheckConditions()` mechanism in
   `src/options_plus_menu.c` rather than shipping something visually broken.

Do not skip step 1. Guessing the indices produces garbled healthbars that look
like a hardware bug.

### 4.4 Steps

1. `include/constants/vars.h`: rename a free var → `VAR_DARK_UI`
   (0 = Light, 1 = Dark). **Do not** add a `SaveBlock2` bit (§0.4).
   Add a tiny inline accessor, e.g. in `include/battle_bg.h`:
   `static inline bool8 IsDarkUiEnabled(void) { return VarGet(VAR_DARK_UI) != 0; }`
   and use it everywhere instead of scattering `VarGet` calls.
   *(If the author prefers a `SaveBlock2` bit for consistency with their other
   options, it must be appended at the very end of the struct, after
   `optionsGenOneRecharge`, and guarded by a magic byte — see §5.4.)*
2. `include/battle_bg.h`: add `BATTLE_WINDOW_DARK_BG_PAL_INDEX`.
   Verify the value `8` is the right slot in HnS's battle window palette before
   copying it — HnS's `src/battle_bg.c` is 1925 lines vs SG's 1239 and has been
   modified.
3. `src/battle_bg.c`: add the dark palette tables and the branch in
   `LoadBattleMenuWindowGfx()` / `LoadBattleMoveDescriptionWindowGfx()`.
4. `src/battle_interface.c`: add the index constants and the four remap/copy
   helpers, and route the existing healthbar/status-icon copies through them.
   Cross-check against every `optionsNewBattleUI` branch — there are ~24; each
   one that copies healthbox gfx needs the dark path too.
5. `src/battle_message.c`, `src/battle_controller_player.c`,
   `src/battle_script_commands.c`: apply the text-colour branches. These are
   small and mechanical, but the line numbers will not match SG's — locate by
   function, not by line.
6. `src/palette.c`: HnS's day/night blending applies to the battle UI palettes.
   SG excludes the UI edge colour from the blend when dark is on
   (`src/palette.c:1386`). Find the equivalent blend site in HnS
   (`UpdatePalettesWithTime` / `TimeMixBattleSpritePalette` area) and add the
   same exclusion, or the dark UI will be tinted orange at dusk.
7. Bag: create `graphics/bag/menu_male_dark.pal` and
   `graphics/bag/menu_female_dark.pal`. You can start from SG's files but HnS's
   bag art differs (HnS has no `scrolling_bg.bin`, no `key_item_box.png`, and has
   `select_button_hold.png` instead of SG's directional variants) — the palettes
   must match **HnS's** `graphics/bag/menu.png`. Then port the `src/item_menu.c`
   branches. Register the new `.pal` files in `graphics_file_rules.mk` if HnS's
   rules require explicit entries (check — `.pal` often passes through).
8. Option entry on the `MENU_CUSTOM` page: `MENUITEM_BATTLE_DARK_UI`, two
   choices `Light` / `Dark`, `ProcessInput_Options_Two`. Place it adjacent to
   `MENUITEM_BATTLE_NEW_BATTLEUI` so the two UI toggles read as a group.
9. `src/new_game.c`: default to Light.

### 4.5 Acceptance

* `make modern` green.
* Light is pixel-identical to today, with `optionsNewBattleUI` both on and off.
* Dark: message box, command menu, move-description window, healthboxes, status
  icons, level-up window and Bag all render correctly, with no stray light
  pixels and no palette bleed into Pokémon sprites.
* Shiny healthboxes (which use a different palette) are unaffected — this is
  explicitly called out in SG's own help text.
* Toggling the option mid-save and re-entering a battle applies immediately.
* Dusk/night in-battle does not tint the dark UI.

---

## 5. Phase 4 — Party menu UI

**Risk: high. Effort: by far the largest item — an order of magnitude more than
Phases 1–3 combined. Ship it as its own PR.**

### 5.1 What "the Soulgold party UI" actually is

SG ships **three** party menus selectable at runtime:

* `PARTY_MENU_OPTION_CUSTOM` (0, the default) — the SwSh-style screen in
  `src/swsh_party_menu.c`, **11 816 lines**, with per-slot easing animations,
  a hover cursor, an item mode, button prompts and its own asset set
  (`graphics/party_menu/swsh/`, 31 files).
* `PARTY_MENU_OPTION_BW` (1) and `PARTY_MENU_OPTION_HGSS` (2) — the *same*
  `src/party_menu.c` compiled twice with different layout data and skins
  (`src/hgss_party_menu.c` is 12 lines that `#include "party_menu.c"` with
  `PARTY_MENU_STYLE_HGSS` defined).

Selection is done by `src/party_menu_dispatch.c` (183 lines), which owns the
public API and forwards ~120 functions to a prefixed private copy. The prefixing
is done by `include/party_menu_variant.h` (~130 `#define`s).

**Important:** SG's `src/party_menu.c` is itself already a heavily modified
"equal column" party menu (`PARTY_BOX_EQUAL_COLUMN`,
`BlitBitmapToPartyWindow_Equal`, `sEqualMainSlotTileNums`,
`src/data/party_menu.h:69-100` and `:763-838`). **HnS's `src/party_menu.c` is
the older two-column layout and has none of that** (verified: no
`PARTY_BOX_EQUAL_COLUMN` anywhere in HnS). So even the "cheap" HGSS/BW skins are
not a skin swap — they need SG's layout engine ported first.

### 5.2 Two options — the author should choose before work starts

**Option A — port the SwSh "Custom" menu (what SG shows by default).**
* Port `src/comfy_anim.c` + `include/comfy_anim.h` (290 + 114 lines; only
  depends on `math_util`, which HnS already has). Low risk.
* Port `src/swsh_party_menu.c`, `src/data/swsh_party_menu.h`, and
  `graphics/party_menu/swsh/`.
* Port `src/party_menu_dispatch.c` + `include/party_menu_variant.h`, reduced to
  two variants: HnS-classic (existing `src/party_menu.c`) and SwSh.
* **86 symbols used by `swsh_party_menu.c` do not exist in HnS** (full list
  computed; regenerate it with the script in §5.6). They fall into groups:
  * expansion data accessors — `GetSpeciesAbility`, `GetMoveName`, `GetMovePP`,
    `GetItemPocket`, `GetItemEffect`, `GetItemTMHMMoveId`,
    `GetSpeciesLevelUpLearnset`, `GetSpeciesTeachableLearnset`,
    `GetSpeciesEggMoves`, `IsOnPlayerSide`, `GET_BASE_SPECIES_ID` … → each is a
    1–3 line adapter over HnS's `gBaseStats`-era equivalents. ~35 symbols.
  * decompression — `DecompressDataWithHeaderWram/Vram` → `LZDecompressWram/Vram`,
    **and every `.smol` / `.smolTM` asset must be re-encoded as `.lz`**
    (HnS has no `tools/compresSmol`). ~20 INCBIN lines in
    `src/data/swsh_party_menu.h`.
  * features HnS does not have — `follower_npc` (5 symbols), level/EV caps
    (`GetCurrentLevelCap`, `GetCurrentEVCap`, `GetCurrentExpCapType`),
    `OpenPokedexPlusHGSSAtSpecies`, `pokerus.h`, `TryFormChange` /
    `GetFormChangeTargetSpecies`, `CanChangeMonPokeball`. Each is either
    stubbed out or wired to HnS's own equivalent. Decide per symbol; **stub
    rather than invent**.
  * comfy anim (10 symbols) — supplied by the port above.
  * `COMPOUND_STRING`, `memcpy`, `memset`, `CpuCopy16/Fill16`, `Vector`, `YES` —
    trivial.
* **Plus:** HnS's own party-menu behaviour must be reproduced in the SwSh
  variant or players lose features when they switch styles. Known HnS-specific
  logic in `src/party_menu.c`: the Headbutt handling (`:2113`), the field-move
  action-list rules that special-case `MOVE_FLY` and `MOVE_FLASH`
  (`:2678-2730`), HM-use-without-teaching, and follower removal on Teleport
  (`FieldCallback_PrepareFadeInForTeleport`, `:3981`).

**Option B — port SG's equal-column layout + the HGSS skin only.**
* Port the `PARTY_BOX_EQUAL_COLUMN` layout data and
  `BlitBitmapToPartyWindow_Equal` into HnS's `src/party_menu.c`, plus
  `graphics/party_menu/hgss/` (5 files, 24 KB) re-encoded to LZ77.
* Add the `PARTY_MENU_STYLE` compile-time switch and, optionally, the runtime
  dispatch with two skins.
* Roughly 10–15% of Option A's effort, no new 12k-line file, no comfy anim,
  no missing-symbol adapters beyond decompression.
* **Thematically the better fit**: Heart & Soul is an HGSS demake, and the HGSS
  skin is literally the HGSS party screen.

**Recommendation: confirm with the author which one they meant before starting.**
If no answer is available, do Option B first (it is a strict subset of the
infrastructure Option A needs) and treat Option A as a follow-up.

### 5.3 Ordering within Phase 4 (Option A)

1. Port `comfy_anim` standalone. Build. Commit.
2. Port `include/party_menu_variant.h` + `src/party_menu_dispatch.c` with
   **only one variant** (HnS classic), wired so behaviour is unchanged. Build.
   Commit. This proves the dispatch layer before any new UI exists.
3. Re-encode the SwSh graphics to LZ77, add `src/data/swsh_party_menu.h`. Build
   (assets only, nothing references them yet). Commit.
4. Add `src/swsh_party_menu.c` with the adapters. Expect this to be the long
   part. Build. Commit.
5. Reproduce HnS-specific party behaviour in the SwSh variant. Commit.
6. Add the option entry + default. Commit.

### 5.4 Option storage for the party style

This is the one option where a var may not be enough, because the party menu can
be reached in contexts where you want the style resolved early. SG stores it in
`SaveBlock1` with a magic-byte validator:

```c
// SG src/party_menu_dispatch.c:22-38
if (gSaveBlock1Ptr->optionsPartyMenuStyleMagic == PARTY_MENU_OPTION_SAVE_MAGIC)
    ... use gSaveBlock1Ptr->optionsPartyMenuStyle ...
else
    ... fall back to the default ...
```

Copy that pattern exactly if you add the field to `SaveBlock1`: it is what makes
the change safe for existing saves. `SaveBlock1` has its own
`STATIC_ASSERT` (`src/save.c:82`) — check headroom before adding.
A free `VAR_` is still simpler and is the default recommendation.

### 5.5 ROM / RAM budget

`src/swsh_party_menu.c` is ~12k lines. Compare the linker
`--print-memory-usage` line before and after. The SwSh variant allocates its
working state on the heap (`sPartyMenuInternal`, `sPartyMenuBoxes`,
`sPartyBgTilemapBuffer` are `EWRAM_DATA` *pointers*, not buffers), so EWRAM
growth should be small — but `struct PartyMenuInternal` contains a
`u16 palBuffer[BG_PLTT_SIZE / 2]` and six `struct ComfyAnim`, so the *heap*
allocation grows. Verify there is no allocation failure when the party menu is
opened from inside a battle, which is the tightest heap moment.

### 5.6 Regenerating the missing-symbol list

```sh
cat include/*.h include/constants/*.h gflib/*.h \
  | grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\b' | sort -u > /tmp/hns_syms.txt
grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\s*\(' ../soulgold/src/swsh_party_menu.c \
  | sed 's/[[:space:]]*(//' | sort -u > /tmp/swsh_calls.txt
comm -23 /tmp/swsh_calls.txt /tmp/hns_syms.txt
```

(It reports ~86 names, including a handful of false positives from local
statics and struct members — filter by hand.)

### 5.7 Acceptance

* `make modern` green; memory usage delta recorded and accepted.
* The existing HnS party menu is reachable and **unchanged** on the default
  setting.
* Every entry point works in the new style: field, in-battle switch, item use,
  Daycare, Move Relearner/Tutor/Deleter, trade, Battle Pyramid held items,
  multi-battle showcase, contest, Teleport/Fly field moves.
* Headbutt and the Fly/Flash action-list rules behave identically to the classic
  menu.
* Followers are restored correctly after Teleport and after a warp.
* Switching the option and re-opening the menu takes effect without a reset.

---

## 6. Cross-cutting rules for the implementer

1. **Never copy a line from `../soulgold` without reading the HnS function it is
   going into.** The APIs differ everywhere (§0.1). A clean-looking copy that
   compiles can still be wrong because the HnS function has different
   preconditions.
2. **Build after every step, not every phase.** With no local toolchain, that
   means: push to the branch and let the Phase 0 CI build it. If Phase 0 is not
   landed yet, land it first.
3. **Do not touch `src/main.c`'s VBlank handler** except in the explicitly
   labelled, separately-revertable RNG commit (§3.4).
4. **Do not restructure `struct SaveBlock2`.** Use vars (§0.4).
5. **Do not "fix" the agbcc build** (§0.2). It is out of scope and already
   broken on `main`.
6. **Do not delete or repurpose HnS options** (`optionsFastBattle`,
   `optionsEvenFasterJoy`, `optionsNewBattleUI`, `optionsLRtoRun`,
   `optionsRunType`). The new options sit alongside them.
7. When an SG behaviour depends on something HnS lacks (DexNav, follower NPCs,
   level caps, the test runner), **remove the condition or stub it** — do not
   port the dependency to satisfy one `if`.
8. Every new user-facing string goes through `_("...")`, not
   `COMPOUND_STRING` (§0.1).

---

## 7. Open questions for the author

1. **Party menu:** Option A (SwSh "Custom") or Option B (HGSS/BW skin)? §5.2.
2. **Battle RNG:** is it acceptable that battle outcomes differ between speed
   settings, or should the VBlank RNG be reworked? §3.4.
3. **Dark UI scope:** battle + Bag only (as in Soulgold), or should it extend to
   the summary screen / PC / Pokédex? §4.1.
4. **Dark UI vs `optionsNewBattleUI`:** should Dark be available for both HnS
   healthbox styles, or only one? §4.3.
5. **Default values for new saves:** all new options default to off/1x/Light, or
   should the speed-ups default to 2x as Soulgold does
   (`../soulgold/src/new_game.c:144`)?
