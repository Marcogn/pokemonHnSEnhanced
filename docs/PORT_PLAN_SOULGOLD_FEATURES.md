# Port plan: Soulgold features → Heart & Soul

Target repo: `marcogn/pokemonhnsenhanced` (this repo, referred to as **HnS**)
Source repo: `marcogn/soulgold` (referred to as **SG**), read-only reference
Branch for all work: `claude/soulgold-to-heart-soul-features-3kqrcd`

Features requested, in the order they should be implemented:

1. Overworld speed-up (1x–4x, hold a button for 1x)
2. Battle speed-up (1x–3x, hold a button for 1x)
3. Dark / Light UI with an options toggle (battle UI + Bag)
4. Party menu UI from Soulgold (the SwSh-style "Custom" screen)

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

### 0.3 No ARM toolchain by default — but it installs cleanly if you need it

`arm-none-eabi-gcc` is absent and `$DEVKITARM` is unset out of the box, so
until you check, assume **you cannot compile here.** That is what makes §1's
CI non-optional rather than a nice-to-have: it is the fallback that makes
"don't break the build" enforceable when no local compiler exists.

However: this turned out not to be a hard wall. The exact packages the CI
workflow installs —
`binutils-arm-none-eabi gcc-arm-none-eabi libnewlib-arm-none-eabi libpng-dev`
— install via plain `apt-get install` in this same kind of container (verified
while implementing Phase 1), giving a toolchain that matches the CI runner's
(`gcc-arm-none-eabi 13.2.1`, confirmed identical version). Once installed,
`make tools -j$(nproc)` then `make modern -j$(nproc)` build the real ROM
locally in about a minute, with the exact same `--print-memory-usage` output
CI reports — which is strictly better than waiting on a CI round-trip for
every step of a phase, and is how Phase 1's numbers in this document were
verified before ever pushing.

**Do not assume this persists or generalize it**, though: outbound network
access is an environment policy, not a project guarantee (see this session's
own system prompt) — it may be unavailable in a differently-configured
session or a future one, and even here nothing about the container's state
carries over between sessions. So: try installing it at the start of a
session: if it works, build and verify locally before every push, which is
strictly better than round-tripping through CI for every step; if it doesn't,
fall back to the CI-only workflow this section originally described, and
budget for the round-trip. Either way, CI remains the authoritative gate —
local verification is a faster feedback loop, not a replacement for the green
check on the PR.

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
  **Correction (found during Phase 2, see §3.8 and §7 rule 11): the list this
  paragraph originally gave here was not actually checked against real usage,
  and most of it was wrong.** Systematically grepping every `VAR_UNUSED_0x*`
  and `VAR_UNUSED_HNS_VAR*` name in `include/constants/vars.h` for real
  references (symbol name and raw hex value) across `src/`, `data/` and
  `asm/` found that `0x40DB`, `0x40DC`, `0x40FB`, `0x8014`, and all four of
  `VAR_UNUSED_HNS_VAR4..7` (`0x409C`–`0x409F`) are live — mostly mirrored into
  scripts via `VarSet`/`VarGet` (e.g. `CheckNuzlockeMode()`,
  `src/script.c:561-577`) despite the "Unused" name and comment. **The
  genuinely free vars, re-verified this way, are only: `0x40A1`, `0x40A8`,
  `0x40B8`, `0x40FC`**, plus `0x40BB` (consumed by Phase 1 as
  `VAR_OVERWORLD_SPEEDUP`) and `0x40FC` (consumed by Phase 2 as
  `VAR_BATTLE_SPEED`). Two remain for Phases 3–4: `0x40A1`, `0x40A8`, `0x40B8`.
  **Re-run this same check before picking one** — this list is a starting
  point from one pass, not a guarantee; do not trust it either without
  re-verifying, for exactly the reason this correction exists.
  Free flags confirmed in `include/constants/flags.h`
  (`FLAG_UNUSED_0x94F`+) — not yet re-verified the same way; do so before
  relying on any specific one.
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

### 1.2 Baseline (recorded — CI run #1, commit `8804466`, head of `main`)

The CI added in §1.1 ran clean on the first try: 871 compiler invocations, 0
errors. (An earlier version of this section said "13 warnings" — that count
came from a truncated tail of the CI log and undercounts badly. A full local
`make modern` on this same commit, done once an ARM toolchain turned out to be
installable in the authoring container after all, shows **596** warnings, the
large majority (`~540`) being `libpng` notices — `bKGD: invalid index` /
`iCCP: known incorrect sRGB profile` — emitted while `gbagfx` converts the
project's PNG art on every from-scratch build. The rest are the same handful
of real pre-existing warnings already named here
(`src/party_menu.c:5873,5875`, `src/pokemon.c:7322,7699,7854`, two
`MtSilver_MountainSide` map-data truncation warnings, an `-Wattribute-alias`
pair, an `extern`-and-initialized declaration, two RTC struct-visibility
notices). All of it is pre-existing and unrelated to this PR's diff — verified
by diffing a clean-vs-changed local build, not by re-reading the same
truncated log more carefully.) The linker's `--print-memory-usage` output,
taken as-is from the job log:

```
Memory region         Used Size  Region Size  %age Used
           EWRAM:      261168 B       256 KB     99.63%
           IWRAM:       26368 B        32 KB     80.47%
             ROM:    22505416 B        32 MB     67.07%
```

i.e. **EWRAM has 976 bytes of headroom, IWRAM has 6400, ROM has ~11 MB.**

**This is the most important number in this document.** ROM is not a
constraint for anything in this plan. EWRAM is — with 976 bytes free on
`main` *before any of the four features exist*, this changes §5's risk
rating from "verify no allocation failure" to "the naive port does not fit,
and a specific mitigation is mandatory." See §5.5, which now states the
mitigation. Every phase must re-check this number (the CI artifact reports it
on every build) before merging, not just Phase 4 — a single careless
`EWRAM_DATA` global anywhere is enough to overflow it.

Re-measure after each phase lands and update the table below.

| After phase | EWRAM used | EWRAM free | ROM used |
|---|---|---|---|
| Baseline (`main`) | 261168 B (99.63%) | 976 B | 22505416 B (67.07%) |
| 1 — overworld speed | 261168 B (99.63%, unchanged) | 976 B | 22505896 B (67.07%, +480 B) |
| 2 — battle speed | 261168 B (99.63%, unchanged) | 976 B | 22506852 B (67.08%, +956 B from Phase 1) |
| 3 — dark/light UI | _pending_ | | |
| 4 — party menu | _pending_ | | |

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

   **Skipped in the Phase 1 implementation.** HnS's `battle_transition.c`
   turned out structurally unrelated to SG's — different function names
   throughout, no counterpart at SG's line numbers — so this would not have
   been the described port but a from-scratch design across roughly 15
   hand-tuned scanline-effect functions, each already fine-tuned to specific
   frame counts. Battle transitions run for a second or two; the risk of
   subtly breaking one of those effects outweighed the benefit for what this
   plan rates a low-risk phase. Left as a genuine follow-up, not silently
   dropped.

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
* `src/new_game.c`: set the default — see §6.1. For this option it is
  `VarSet(VAR_OVERWORLD_SPEEDUP, OPTIONS_OVERWORLD_SPEED_1X)`, matching SG.

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

(**Table below is as originally planned; §3.8 has what actually landed after
verifying each row against the real HnS source — several rows turned out
wrong or unnecessary. Read §3.8, not just this table.**)

| SG symbol | HnS status | Action |
|---|---|---|
| `InBattleChoosingMoves()` | absent | add to `src/battle_main.c`, `return gBattleMainFunc == HandleTurnActionSelectionState;` — that static exists in HnS. **Landed as planned.** |
| `InBattleRunningActions()` | absent | same pattern with `RunTurnActionsFunctions`. **Not landed — turned out unneeded once the `OPTIONS_BATTLE_SCENE_DISABLED` branch (its only caller) was dropped. See §3.8.** |
| `IsPaletteFadeTransferPending()` | absent | HnS `src/palette.c` has no `sPlttBufferTransferPending`. Add the flag + accessor, mirroring SG `src/palette.c:95-107`. **Wrong — HnS already has this exact flag and mechanism, just no public accessor. Only the 4-line wrapper was added. See §3.8.** |
| `AdvanceRandom()` | absent | HnS uses the vanilla LCG `Random()` (`src/main.c:363`, `src/battle_main.c:2736`). Do **not** port SG's SFC32 RNG. Instead, see §3.4. **Landed as planned (i.e. not touched at all).** |
| `gTestRunnerEnabled` | absent | drop the condition entirely; HnS has no test runner. **Landed as planned.** |
| `gBattleStruct->hasBattleInputStarted` | absent | add a `u8 hasBattleInputStarted:1;` to `struct BattleStruct` in `include/battle.h`. **Not landed — see §3.8, same reason as `InBattleRunningActions()` above.** |
| `gBattleSpritesDataPtr->animationData->captureSuccessAnimActive` | **verify** | HnS has `struct BattleAnimationInfo` at `include/battle.h:543` with `ballThrowCaseId` at `:552`, but the field name may differ. Grep before using; if the field is absent, gate on `gBattleResults.caughtMonSpecies` only. **Confirmed absent; landed on the `caughtMonSpecies` fallback exactly as this row anticipated. See §3.8 for the accepted coarseness.** |
| `BtlController_Complete`, `enum BattlerId` signatures | different | not needed — the speed-up touches only `BattleMainCB2`, not the controllers |

### 3.3 Steps

1. ~~`include/constants/vars.h`: rename `VAR_UNUSED_0x40DC` → `VAR_BATTLE_SPEED`~~
   **Corrected — do not use `0x40DC`, it is not actually free.** See §3.8 and
   §7 rule 11: it is live (`CheckNuzlockeMode()`, `src/script.c:569,571`, and
   a debug script). Use `VAR_UNUSED_0x40FC` → `VAR_BATTLE_SPEED` instead
   (verified genuinely unreferenced). SG's own address (`:240` in its
   `vars.h`) was always coincidental, not load-bearing, so this substitution
   changes nothing else.
2. `include/constants/global.h`: add battle-speed constants. **Corrected
   naming** (see §3.8): not `OPTIONS_BATTLE_SCENE_*` verbatim from SG — that
   name collides in meaning with HnS's own pre-existing, unrelated
   `optionsBattleSceneOff` bit (whether animations play at all, vs. this
   option's "how fast do they play"). Landed as `OPTIONS_BATTLE_SPEED_1X/2X/3X`
   and `_COUNT`, no `_DISABLED` value (dropped per step 5 below, which turned
   out to make step 3 below unnecessary too). `optionsBattleSceneOff` is
   untouched either way.
3. ~~`include/battle.h`: add `hasBattleInputStarted` to `struct BattleStruct`~~.
   **Not needed — see §3.8.** Its only purpose in SG is gating the
   `OPTIONS_BATTLE_SCENE_DISABLED` branch this step's own next bullet already
   says may be dropped; drop that branch fully and the flag has no reader left.
4. `src/battle_main.c`:
   * add `InBattleChoosingMoves()` ~~/ `InBattleRunningActions()`~~ (the
     second is not needed either, for the same reason as step 3 — its only
     caller was inside the dropped branch) and declare it in
     `include/battle_main.h`;
   * add `static void RunBattleSoftwareTick(void)` containing exactly the five
     calls currently inlined in `BattleMainCB2` (`src/battle_main.c:2253-2257`);
   * add `static bool32 CanRunExtraBattleTick(void)` adapted per §3.2;
   * rewrite `BattleMainCB2()` (`src/battle_main.c:2251`) to the SG loop shape.
     **Preserve HnS's recorded-battle B-button block at the end unchanged**
     (`src/battle_main.c:2259-2266`).
5. Add `GetBattleSpeedScale(void)` — **no `forHealthbar` parameter** (see
   §3.8: SG's own `Rogue_GetBattleSpeedScale(bool32 forHealthbar)` is called
   from exactly one site, always with `FALSE`; the parameter is dead weight
   in SG itself). Put it in `src/battle_main.c` rather than
   `src/battle_controllers.c` to keep the change in one file; declare in
   `include/battle_main.h`. Adapt from SG `src/battle_controllers.c:3028`:
   * `VarGet(VAR_BATTLE_SPEED)` instead of `VarGet(B_BATTLE_SPEED)` (HnS has no
     `include/config/battle.h`);
   * `JOY_HELD(L_BUTTON)` → return 1 (this is SG's behaviour already, and `L`
     alone is free in HnS battles — see §0.5);
   * keep the `InBattleChoosingMoves()` → 1x rule: move selection must stay at
     normal speed or the menu becomes unusable;
   * drop the `OPTIONS_BATTLE_SCENE_DISABLED` branch entirely (not just
     "may" — see step 3 above for why dropping it fully removes the need for
     `hasBattleInputStarted` and `InBattleRunningActions()` too) since HnS
     keeps "battle scene off" as a separate option.
6. **Do not** touch `src/main.c`'s VBlank RNG or `VBlankCB_Battle`
   (`src/battle_main.c:2733`) in the first cut — see §3.4.

### 3.4 RNG: decided — ship without the rework

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

**DECIDED by the author: speed-dependent RNG is acceptable.** Ship Phase 2
without the RNG rework. Do **not** touch `src/main.c`'s VBlank handler or
`VBlankCB_Battle`. Leave both `Random()` calls exactly where they are.

Nothing needs to be written in the option description about this — it is not a
user-visible defect, just a consequence of the frame model.

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
`MENUITEM_BATTLE_SPEED`, `sItemFunctionsCustom`, `sel_battle[]`. **Landed as a
genuine 3-choice option** (1x/2x/3x, no "Off" — that already exists as the
independent `optionsBattleSceneOff`) using the already-existing
`ProcessInput_Options_Three` / a `DrawChoices_Options_Three`-style 3-label
draw function (mirroring `DrawChoices_ButtonMode`'s pattern exactly), not
`ProcessInput_Options_Four` with a 4th slot — matching SG's own real exposed
range of 1x/2x/3x (`src/option_menu.c:1478-1530`) with no invented "Off"
value that would have duplicated `optionsBattleSceneOff`'s meaning.
Default in `src/new_game.c`: **2x**, matching SG — see §6.1, and read §6.2 and
§3.8 (the last paragraph) on why existing saves are deliberately left at 1x
and exactly where the 2x default and its carry-over live.

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

### 3.8 Implementation notes — landed, real deviations from §3.2/§3.3 above

Verified with a real `make modern` build (§0.3): 0 errors, 0 warnings in any
touched file, EWRAM/IWRAM unchanged from the Phase 1 row in §1.2's table, ROM
+956 bytes. Several things §3.2/§3.3 said to add or verify turned out, on
inspection of the actual HnS source, to be either already present or
unnecessary once a design choice above (§3.4's decision) was followed through
completely. Recorded here so the reasoning survives, not just the diff:

* **`VAR_UNUSED_0x40DC` was not free — do not use it.** Despite its name and
  the "Unused Var" comment in `vars.h`, it is read and written by
  `CheckNuzlockeMode()` (`src/script.c:569,571`) to mirror
  `gSaveBlock1Ptr->tx_Challenges_Nuzlocke` for scripts, and read directly by
  `data/scripts/debug.inc:80`. Reusing it would have silently broken the
  Nuzlocke-challenge debug display. This was caught by checking every
  `VAR_UNUSED_0x40xx` candidate's actual usage across `src/`, `data/` and
  `asm/` before picking one — the same check the Phase 1 var (`0x40BB`) had
  already passed, but §3.3's suggested `0x40DC` had not been re-checked
  against this specific codebase. **`VAR_BATTLE_SPEED` uses `0x40FC`
  instead** (confirmed genuinely unreferenced anywhere, including as a raw
  hex literal). Three other vars in that range turned out to be live for the
  same reason (`0x40DB`, `0x40FB`) — treat every "Unused" comment in this
  file as a claim to verify, not a fact, in any later phase too.
* **`hasBattleInputStarted` (§3.2, §3.3 step 3) was not added.** Its only
  purpose in Soulgold is to distinguish "before" from "after" the first move
  selection for the `OPTIONS_BATTLE_SCENE_DISABLED` branch of
  `Rogue_GetBattleSpeedScale` — and §3.3 step 5 already said that branch could
  be dropped, since HnS keeps "battle scene off" as its own independent
  option. Once that branch is gone, nothing reads the flag: `GetBattleSpeedScale`
  can call `InBattleChoosingMoves()` directly with no "has selection started"
  gate needed. Adding the field anyway would have been dead weight on
  `BattleStruct` for no behavioural difference.
* **`InBattleRunningActions()` was not added**, for the same reason — its only
  caller in Soulgold was inside the same dropped branch.
* **`GetBattleSpeedScale` takes no `forHealthbar` parameter.** Soulgold's
  `Rogue_GetBattleSpeedScale(bool32 forHealthbar)` is called from exactly one
  site in Soulgold itself, always with `FALSE` — the `TRUE` path is dead code
  in Soulgold too. Porting the parameter would have copied Soulgold's own
  leftover complexity for no reason; HnS's version takes no parameter.
* **`IsPaletteFadeTransferPending()` did not need a new backing flag.** §3.2
  said "HnS `src/palette.c` has no `sPlttBufferTransferPending`" — that was
  wrong. HnS's `palette.c` already has the identical mechanism
  (`sPlttBufferTransferPending`, set in `UpdatePaletteFade()` from
  `gPaletteFade.multipurpose1`, cleared in `TransferPlttBuffer()`), just
  without a public accessor. Only the four-line wrapper function was needed.
  Had this not been checked, the port would have added a second, competing
  static of the same name and purpose.
* **The capture-animation gate uses `gBattleResults.caughtMonSpecies` only**,
  per §3.2's own documented fallback: HnS's `struct BattleAnimationInfo`
  (`include/battle.h:543`) has no field resembling Soulgold's
  `captureSuccessAnimActive` — its per-animation bits are unnamed
  (`field_9_x1C` etc.). This gate is coarser than Soulgold's (it stays tripped
  for the rest of the battle after any catch, not just during the catch
  animation itself), which matters only in the rare case of a double wild
  battle continuing after one Pokémon is caught — accepted as-is per §3.2.
* **Battle speed's default (2x) is not set the same way Phase 1's overworld
  option's default (1x) is.** Overworld speed needed nothing beyond the
  carry-over in `NewGameInitData()`, because 1x is what an untouched var
  already reads as (0). Battle speed's desired first-ever default is 2x
  (value 1), which is not what 0 means, so it needs an explicit one-time
  write. Tracing where Soulgold itself does this (`src/new_game.c:144`, its
  `SetDefaultOptions()`-equivalent — *not* `NewGameInitData()`, confirmed by
  reading both functions) showed the same split HnS already has: HnS's own
  `SetDefaultOptions()` runs exactly once, only when the save is empty or
  corrupt (`src/intro.c:903-904`), and its `gSaveBlock2Ptr->options... =`
  writes there persist across every later "New Game" on that same file,
  because `NewGameInitData()` never wipes `SaveBlock2` — only `SaveBlock1`
  (where the vars live). So `VAR_BATTLE_SPEED = 2x` is set once in
  `SetDefaultOptions()`, and `NewGameInitData()` carries it across the
  `InitEventData()` reset exactly like Phase 1's overworld speed (snapshot
  before, clamp, restore after) — without that carry-over, the one-time
  default would be wiped by the very first "New Game" that follows it. This
  matches Soulgold's real, verified behaviour (its own `NewGameInitData` does
  the identical snapshot/restore for `VAR_BATTLE_SPEED`) and reproduces the
  asymmetry §6.2 calls for: a save from before this option existed reads 0
  (1x) forever unless the player changes it; any save that ever went through
  `SetDefaultOptions()` gets 2x initially and keeps whatever it's set to
  across subsequent New Games on that file.

---

## 4. Phase 3 — Dark / Light UI

**Risk: medium. Effort: ~10 files, mostly palette data. The hard part is asset
archaeology, not logic.**

### 4.1 Scope — DECIDED: battle + Bag only, exactly as in Soulgold

In SG this is not a general theme engine. It is a targeted re-palette of:

* the battle message box, command menu and move-description window,
* the healthboxes (a **whole-palette swap** plus a small 4bpp index remap for
  the pixels that must not follow the swap — see §4.3),
* the level-up stat window and a few battle-script windows,
* the Bag screen (background palette, pocket indicators, HM icon, TM/HM info
  text).

It does **not** touch the overworld, the Pokédex, the summary screen, the PC,
or the party menu — and per the author's decision it must not be extended to
them in this phase.

### 4.2 SG reference map

| Area | SG location |
|---|---|
| Option bit | `include/global.h:633` `optionsDarkBattleUi:1` |
| Colour constants | `src/battle_bg.c:36` `DARK_BATTLE_UI_BG_COLOR`, `:58-70` |
| Battle window pals | `src/battle_bg.c:804-843` (`LoadBattleMenuWindowGfx`, `LoadBattleMoveDescriptionWindowGfx`) |
| Window pal index | `include/battle_bg.h:5` `BATTLE_WINDOW_DARK_BG_PAL_INDEX 8` |
| Healthbox remap | `src/battle_interface.c:46-54` (index constants), `:905-1005` (`IsDarkHealthbox`, `RemapHealthbarGfxIndexes`, `CopyHealthbarGfx`, `CopyStatusIconGfx`) |
| Healthbox pal pick | `src/battle_gfx_sfx_util.c:86-100` (`LoadBattleInterfacePalettes`) |
| Dark healthbox palette | `graphics/battle_interface/dark_healthbox.pal` |
| Message text | `src/battle_message.c:2231` |
| Player controller | `src/battle_controller_player.c:1939-1986` |
| Level-up window | `src/battle_script_commands.c:7287-7296`, `:12120-12171`, `:16544-16557` |
| Day/night interaction | `src/palette.c:1386` |
| Bag | `src/item_menu.c:62-64`, `:643-681` (palettes), `:1148-1156`, `:1310`, `:1394-1399`, `:1850-1859`, `:3660-3666`, `:3765` |
| Bag dark palettes | `graphics/bag/menu_male_dark.pal`, `graphics/bag/menu_female_dark.pal` (367/369 bytes) |
| Option UI | `src/option_menu.c:1614-1615`, help text at `:411` |

### 4.3 How the dark healthbox actually works (simpler than it looks)

The palettes were dumped and compared; here is the real mechanism, so nobody
reinvents it.

**SG's `dark_healthbox.pal` is `ball_status_bar.png`'s palette with entries
1–4 darkened and everything else byte-identical:**

| idx | SG light | SG dark |
|---|---|---|
| 0 | 0,0,0 | 0,0,0 |
| 1 | 65,65,65 | **12,12,12** |
| 2 | 227,227,227 | **43,43,43** |
| 3 | 186,186,186 | **35,35,35** |
| 4 | 154,154,154 | **29,29,29** |
| 5–15 | — | unchanged |

Indices 1–4 are the healthbox frame and text, so swapping the palette darkens
the whole box for free. `LoadBattleInterfacePalettes` (SG
`src/battle_gfx_sfx_util.c:86-100`) just picks the other palette under the same
`TAG_HEALTHBOX_PAL`.

The **index remap** exists only for the pixels that must *not* follow that
swap — the HP bar and the status icon, which would become unreadable. SG's
constants (`src/battle_interface.c:46-54`):

```
HEALTHBAR_LIGHT_COLOR_1  1   →  HEALTHBAR_DARK_COLOR_1   5
HEALTHBAR_LIGHT_COLOR_2  3   →  HEALTHBAR_DARK_COLOR_2   8
STATUS_ICON_LIGHT_COLOR_1 4  →  STATUS_ICON_DARK_COLOR_1 5
STATUS_ICON_LIGHT_COLOR_2 2  →  STATUS_ICON_DARK_COLOR_2 6
```

SG's indices 5 and 8 are `(114,108,79)` and `(217,183,0)` — the olive/yellow HP
shades, which stay legible on a dark box.

### 4.3.1 What differs in HnS

HnS ships **two** healthbox styles, chosen by
`gSaveBlock2Ptr->optionsNewBattleUI` (`include/global.h:571`). They are Gen 3
and Gen 4 art with **separate palettes**, loaded in
`GetHealthBoxHealthBarPalettes()` (`src/battle_gfx_sfx_util.c:113-127`):
`gBattleInterface_BallStatusBarPalGen4` ←
`graphics/battle_interface/ball_status_bar.png`, and `...PalGen3` ←
`ball_status_bargen3.png`.

Their palettes were dumped. Entries 0–4 are what matter, and:

| idx | HnS Gen4 | HnS Gen3 | SG |
|---|---|---|---|
| 0 | 0,0,0 | 0,0,0 | 0,0,0 |
| 1 | 65,65,65 | 65,65,65 | 65,65,65 |
| 2 | 227,227,227 | 255,255,255 | 227,227,227 |
| 3 | 186,186,186 | 222,214,222 | 186,186,186 |
| 4 | 154,154,154 | 189,189,189 | 154,154,154 |

**Gen4 is index-identical to SG at 0–4**, so SG's darkened values transfer
verbatim. Gen3 uses the same *roles* at the same indices, just lighter shades —
darken them to the same targets (12/43/35/29) and it works the same way.

Entries 5–8 differ between all three (HnS Gen4: 5=`123,148,131`, 7=`113,113,113`,
8=`48,97,219`; HnS Gen3: 5=`123,148,131`, 6=`82,106,98`, 7=`32,57,0`,
8=`57,82,65`). So the **`*_DARK_COLOR_*` targets cannot be copied from SG** —
pick, per style, indices whose colours stay readable on the dark box. Verify by
looking at the actual PNGs, not by reasoning about the numbers.

**Decision (per the author: do it like Soulgold, don't over-engineer):**
implement Dark for **both** styles — the toggle must not silently do nothing
depending on the other toggle. That costs two `.pal` files and a two-entry
constant table, nothing more. Do **not** add a `CheckConditions()` gate, a
third style, or a theme abstraction.

### 4.4 Steps

1. `include/constants/vars.h`: rename a free var → `VAR_DARK_UI`
   (0 = Light, 1 = Dark). **Do not** add a `SaveBlock2` bit (§0.4).
   Add a tiny inline accessor, e.g. in `include/battle_bg.h`:
   `static inline bool8 IsDarkUiEnabled(void) { return VarGet(VAR_DARK_UI) != 0; }`
   and use it everywhere instead of scattering `VarGet` calls.
   A `SaveBlock2` bit is ruled out — see §0.4 and §6.2.
2. `include/battle_bg.h`: add `BATTLE_WINDOW_DARK_BG_PAL_INDEX`.
   Verify the value `8` is the right slot in HnS's battle window palette before
   copying it — HnS's `src/battle_bg.c` is 1925 lines vs SG's 1239 and has been
   modified.
3. `src/battle_bg.c`: add the dark palette tables and the branch in
   `LoadBattleMenuWindowGfx()` / `LoadBattleMoveDescriptionWindowGfx()`.
4. Create the two dark palettes per §4.3.1:
   `graphics/battle_interface/dark_healthbox.pal` (from `ball_status_bar.png`,
   the Gen 4 style) and `graphics/battle_interface/dark_healthboxgen3.pal`
   (from `ball_status_bargen3.png`). Add the INCBINs next to the existing ones
   in `src/graphics.c:353-357` and the externs in `include/graphics.h`.
5. `src/battle_gfx_sfx_util.c`: extend `GetHealthBoxHealthBarPalettes()`
   (`:113-127`) so the `TAG_HEALTHBOX_PAL` entry picks the dark palette of the
   *current* style when the option is on. This is SG's
   `LoadBattleInterfacePalettes` change folded into HnS's existing style
   branch — four lines, no new structure.
6. `src/battle_interface.c`: add the index constants (as a two-entry table
   indexed by `optionsNewBattleUI`, per §4.3.1) and the four remap/copy helpers,
   and route the existing healthbar/status-icon copies through them.
   Cross-check against every `optionsNewBattleUI` branch — there are ~24; each
   one that copies healthbox gfx needs the dark path too.
7. `src/battle_message.c`, `src/battle_controller_player.c`,
   `src/battle_script_commands.c`: apply the text-colour branches. These are
   small and mechanical, but the line numbers will not match SG's — locate by
   function, not by line.
8. `src/palette.c`: HnS's day/night blending applies to the battle UI palettes.
   SG excludes the UI edge colour from the blend when dark is on
   (`src/palette.c:1386`). Find the equivalent blend site in HnS
   (`UpdatePalettesWithTime` / `TimeMixBattleSpritePalette` area) and add the
   same exclusion, or the dark UI will be tinted orange at dusk.
9. Bag: create `graphics/bag/menu_male_dark.pal` and
   `graphics/bag/menu_female_dark.pal`. You can start from SG's files but HnS's
   bag art differs (HnS has no `scrolling_bg.bin`, no `key_item_box.png`, and has
   `select_button_hold.png` instead of SG's directional variants) — the palettes
   must match **HnS's** `graphics/bag/menu.png`. Then port the `src/item_menu.c`
   branches. Register the new `.pal` files in `graphics_file_rules.mk` if HnS's
   rules require explicit entries (check — `.pal` often passes through).
10. Option entry on the `MENU_CUSTOM` page: `MENUITEM_BATTLE_DARK_UI`, two
    choices `Light` / `Dark`, `ProcessInput_Options_Two`. Place it adjacent to
    `MENUITEM_BATTLE_NEW_BATTLEUI` so the two UI toggles read as a group.
11. `src/new_game.c`: default to Light, matching SG (`src/new_game.c:143`). See §6.1.

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

### 5.2 DECIDED: port the SwSh "Custom" menu (Soulgold's default skin)

The author chose the Sword/Shield-style screen — Soulgold's default, i.e.
`PARTY_MENU_OPTION_CUSTOM`. The HGSS/BW skins are **not** in scope.
What that entails:

* Port `src/comfy_anim.c` + `include/comfy_anim.h` (290 + 114 lines; only
  depends on `math_util`, which HnS already has). Logic is low risk — **but do
  not port its static `gComfyAnims` pool as-is; §5.5 has real EWRAM numbers
  showing the naive port does not fit, and a mandatory heap-allocation change.**
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

**Not in scope:** Soulgold's equal-column layout and the HGSS/BW skins
(`graphics/party_menu/hgss`, `graphics/party_menu/bw`,
`src/hgss_party_menu.c`, the `PARTY_MENU_STYLE` compile-time switch). Do not
port them. They are recorded here only as the documented fallback if the SwSh
port stalls: they would cost roughly 10–15% of the effort, but they are a
different feature, not a cheaper version of this one.

**Scope discipline for this phase:** two styles only — HnS-classic (the
existing `src/party_menu.c`, unchanged) and SwSh. Two entries in the dispatch
switch, not three.

### 5.3 Ordering within Phase 4

1. Port `comfy_anim` standalone, **with `gComfyAnims` heap-allocated from the
   start, not as a static array** (§5.5's mandatory mitigation — do this now,
   not as a fix-up after the naive version fails to fit). Build, check EWRAM
   against §1.2's table. Commit.
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

Use a free `VAR_` (`VAR_PARTY_MENU_STYLE`) like the other options, **plus SG's
magic-byte idea adapted to a var**: reserve value 0 as "never chosen" and store
`style + 1`, so an existing save that has never seen the option is
distinguishable from one deliberately set to style 0.

```c
static u8 GetPartyMenuStyle(void)
{
    u16 stored = VarGet(VAR_PARTY_MENU_STYLE);

    if (stored == 0 || stored - 1 >= PARTY_MENU_STYLE_COUNT)
        return PARTY_MENU_STYLE_DEFAULT;   // see §6.1
    return stored - 1;
}
```

SG does the same job with a `SaveBlock1` field plus a separate magic byte
(`src/party_menu_dispatch.c:22-38`,
`PARTY_MENU_OPTION_SAVE_MAGIC == 0xA5`). The var-plus-offset form above is
equivalent, needs no struct change, and therefore cannot disturb existing
saves. Do not add fields to `SaveBlock1` for this.

### 5.5 ROM / RAM budget — EWRAM headroom is the blocking constraint, not ROM

**Read §1.2 before starting this phase.** The baseline build has **976 bytes**
of free EWRAM (99.63% used) and ~11 MB of free ROM. ROM is a non-issue. EWRAM
is not something to "verify" at the end — the naive port does not fit, and a
specific change to SG's design is mandatory before this phase can land.

`src/swsh_party_menu.c`'s *working state* is heap-allocated, correctly:
`sPartyMenuInternal`, `sPartyMenuBoxes`, `sPartyBgGfxTilemap`,
`sPartyBgTilemapBuffer`, `sPartyBg3TilemapBuffer` are `EWRAM_DATA` pointers, not
buffers, so `struct PartyMenuInternal`'s contents (including its
`u16 palBuffer[BG_PLTT_SIZE / 2]` and six `struct ComfyAnim`) live on the heap
and cost nothing statically. That part is fine as designed.

**What is not fine: two categories of *static* `EWRAM_DATA` the file adds
regardless of whether the menu is ever opened.**

1. **`comfy_anim.c`'s own pool:**
   `EWRAM_DATA struct ComfyAnim gComfyAnims[NUM_COMFY_ANIMS] = {0};`
   (`../soulgold/src/comfy_anim.c:5`, `NUM_COMFY_ANIMS == 8`,
   `../soulgold/include/comfy_anim.h:84`). `struct ComfyAnim` is
   `ComfyAnimConfig` (a tagged union of the easing/spring configs, ~28 bytes
   plus a tag) + a 4-byte state union + `position`/`velocity`/`delayFrames`/
   `completed`/`inUse` (5×4 bytes) ≈ **56 bytes each, ~448 bytes for the pool
   of 8.** That alone is **46% of all remaining EWRAM**, permanently, whether
   or not the player ever opens a party menu.
2. **`swsh_party_menu.c`'s ~35 other top-level `static EWRAM_DATA` scalars and
   small arrays** (sprite/window IDs, saved-state snapshot fields, tilemap
   pointers not already counted above, `sSelectFrameSpriteIds[7]`,
   `sMessageWindowSpriteIds[16]`, `sMultiuseWindowSpriteIds[6]`, etc.) — on the
   order of another **~140–200 bytes**, depending on struct packing.

Combined, that is comfortably **more than the entire 976-byte headroom**,
before a single line of the ~35 missing-symbol adapters (§5.2) has added
anything of its own, and before Phases 1–3 have spent any of that headroom
themselves (they are designed not to — see the "no new static `EWRAM_DATA`"
rule in §7 — but re-measure after each of them anyway, per §1.2's table).

**Mandatory mitigation: `gComfyAnims` must be heap-allocated, not static.**
This is a real, scoped change to SG's design, not a suggestion:

* Change `include/comfy_anim.h` / `src/comfy_anim.c` so `gComfyAnims` becomes
  `EWRAM_DATA struct ComfyAnim *gComfyAnims = NULL;`, allocated with `AllocZeroed`
  (or the project's equivalent) when the party menu (or whatever else ends up
  using comfy anims) is entered, and freed when it exits — following the same
  lifecycle as `sPartyMenuInternal` itself. `GetAvailableComfyAnim()`,
  `AdvanceComfyAnimations()` and every other function that indexes
  `gComfyAnims[i]` are unaffected by this change; only the storage class moves.
  If comfy anims end up used **only** by the party menu (true as of this port —
  check again if a later feature reuses them), the cleanest home for the
  pointer and its allocation is inside `struct PartyMenuInternal` itself, which
  removes the global entirely.
* For the ~35 smaller statics in `swsh_party_menu.c`: fold as many as
  reasonably possible into `struct PartyMenuInternal` (heap-allocated already)
  instead of leaving them as file-level statics. Not all of them can move
  (some are read from contexts where `sPartyMenuInternal` may already be freed
  — check each one), but every one that moves is EWRAM given back.
* After these changes, **re-measure with the real linker output** (§1.2's
  table) rather than estimating. The arithmetic above is close enough to know
  the naive port is not viable, not precise enough to certify the fixed
  version — that requires an actual `make modern` build, which only exists in
  CI (§0.3).

If, after both mitigations, the build still does not fit: the next lever is
trimming `swsh_party_menu.c`'s own statics further (e.g. `sMoveWindowIds` /
`sMoveTypeSpriteIds` are `MAX_MON_MOVES`-sized arrays that could be computed
per-use instead of cached) — not reducing `NUM_COMFY_ANIMS` below what the UI
needs (2 for the cursor + up to `PARTY_SIZE` for per-slot animation is the
actual requirement SG's code encodes, not an arbitrary round number).

Verify there is no allocation failure when the party menu is opened from
inside a battle, which is the tightest *heap* moment (separate from the EWRAM
headroom problem above, which exists whether or not the menu is ever opened).

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
* The SwSh menu is what a new game and a pre-change save both get (§6.2), and
  the classic HnS menu is still reachable and **byte-identical to today** when
  selected in Options.
* Every entry point works in the new style: field, in-battle switch, item use,
  Daycare, Move Relearner/Tutor/Deleter, trade, Battle Pyramid held items,
  multi-battle showcase, contest, Teleport/Fly field moves.
* Headbutt and the Fly/Flash action-list rules behave identically to the classic
  menu.
* Followers are restored correctly after Teleport and after a warp.
* Switching the option and re-opening the menu takes effect without a reset.

### 5.8 KNOWN ISSUE (2026-09-14): SwSh crashes on open — undiagnosed, default changed to HnS classic

**Status: reproduced live, root cause NOT found. `PARTY_MENU_STYLE_DEFAULT` is
temporarily `PARTY_MENU_STYLE_HNS` (include/constants/global.h) until this is
fixed.** SwSh stays selectable in Options for anyone who wants to help debug
it live; it is not safe as the out-of-the-box default.

**Symptom**: opening the party menu (Start → Pokémon) while
`VAR_PARTY_MENU_STYLE` resolves to SwSh resets the game to the copyright/boot
screen, every time, on the very first frame the menu would render — not
specific to battle vs. overworld, not specific to a particular Pokémon or
save. The HnS classic style does not reproduce this on the same save
(user-confirmed).

**What's already ruled out**, each confirmed by reproducing live in a headless
mGBA (`python-mgba`) instance loaded with a real user `.sav`, not by
inspection alone:

1. `gComfyAnims` never being allocated (`InitComfyAnims()`/`FreeComfyAnims()`
   were never wired into the party menu's init/teardown, despite
   `comfy_anim.h`'s own doc comment saying callers must). This was real and is
   fixed (commit `919699ec`), but does **not** fix this crash — it still
   reproduces on top of that fix.
2. `InitPartyMenuBoxes()`'s unchecked `Alloc()` (also fixed in `919699ec`) —
   not the cause either; the heap allocation succeeds fine this early.
3. `LoadPartyBoxPalette()` (called from `AnimatePartySlot` while rendering the
   selected slot) — temporarily stubbed out; crash still reproduces, just
   slightly earlier in the same sequence.
4. `RenderPartyMenuBoxes()` entirely (all box drawing/blitting, case 13 of
   `ShowPartyMenu`'s state machine) — temporarily stubbed out; crash **still**
   reproduces, now during `CreatePartyMonSpritesLoop`/icon sprite creation
   (case 12). So it is not in the box-rendering/`BlitBitmapRect4Bit` path.
5. `CreatePartyMonSpritesLoop()` (icon/held-item/status sprite creation, case
   12) *also* stubbed out on top of (4) — crash **still** reproduces, now
   during/after `DecompressGraphics()` (case 8) finishing and
   `InitPartySlotAnimations()`/`InitPartySlotScanlineEffect()` running (case
   15-ish). So it is not sprite creation either.
6. Not an EWRAM/VRAM budget overflow found by inspection: every
   `LoadCompressedSpriteSheet`/`LZDecompressWram` destination buffer's fixed
   size was checked against the actual asset's real decompressed size (the LZ
   header's declared size) — `sPartyBgTilemapBuffer`/`sPartyBg3TilemapBuffer`
   (0x800 each) and all five extra sprite sheets (HoverCursor, SelectFrame,
   MessageWindow, MultiuseWindow, StatusIcons) match exactly, no overflow.
   Total extra OBJ sprite-tile VRAM used by SwSh's sheets is ~200 tiles
   (~6.4 KB) against the hardware's 1024-tile budget — nowhere near exhausted.
   `sPartyMenuSpriteCoords` and the various palette-ID/offset tables
   (`sPartyBoxCurrSelectionPalIds1/2/3` etc.) were checked and are in-bounds
   for `sPartyMenuInternal->palBuffer[256]`.

**What the crash actually looks like at the hardware level** (traced with
`core.step()` single-instruction stepping and symbol resolution against the
ELF, see the session's `/tmp/swsh_debug/trace_crash*.py` scripts — not
preserved in the repo, but the technique is worth recreating if you pick this
back up): the CPU takes a genuine ARM7TDMI **Prefetch Abort** exception
(PC lands on vector `0x0000000C`) during a `CpuSet` BIOS call reached via
`LoadPalette`/`CpuCopy16`, with `LR` pointing back into `CpuSet` cleanly and
`r0`/`r1` both individually valid, in-range, correctly-aligned EWRAM
addresses for a 1-halfword 16-bit copy — i.e. the *parameters* to that one
call look fine in isolation. Given point 3 above (stubbing out that exact
call site didn't stop the crash, just relocated it), this almost certainly
means something earlier in `DecompressGraphics()`'s asset-loading sequence
(cases 0-21 of its own internal switch) is corrupting memory — most likely
IWRAM, given the fault manifests as a hardware exception rather than a wrong
value — and the *symptom* simply surfaces wherever the next
BIOS/SWI-dependent call happens to land, which shifts depending on what
else got bisected out. **Next steps for whoever picks this up**: bisect
inside `DecompressGraphics()`'s own switch (cases 0-21) the same way cases
12/13 were bisected here; in particular look hard at anything writing through
a computed/indexed pointer into IWRAM (the interrupt vector table and BIOS
call stack both live in low IWRAM, and corrupting either would produce
exactly this symptom - a hardware exception whose default handler falls
through to something that looks like the game restarting).

**Open discrepancy, not resolved**: forcing `VAR_PARTY_MENU_STYLE` to the
classic value (1) by writing it directly into the loaded save's memory, on
the exact save that crashes with SwSh, *also* crashed in this session's
headless-mGBA testing - even though the disassembly of the compiled
dispatcher (`CB2_PartyMenuFromStartMenu` in `party_menu_dispatch.c`) is
unambiguous: for a stored value of 1 it computes `PARTY_MENU_STYLE_HNS` and
branches to `HnsPartyMenu_CB2_PartyMenuFromStartMenu`, never touching SwSh's
code at all. Live single-instruction tracing to directly confirm which
branch actually executed did not manage to re-locate the dispatcher's own
address before the session's time ran out, so this is unresolved: it could
mean there's a second, real bug that also breaks (or bypasses) the classic
path on this specific save, or it could be an artifact of this session's
test harness (a stale `gSaveBlock1Ptr`-relocation assumption, or a poke that
didn't survive to the moment `VarGet` actually runs). **Before trusting
"classic works" as a blanket statement, verify it fresh** - ideally by
reproducing via the in-game Options menu on real hardware/a real emulator
rather than a memory poke, since that removes this whole class of doubt.

---

## 6. Defaults, and compatibility with existing saves

### 6.1 What each option defaults to

Soulgold's values, taken from `../soulgold/src/new_game.c`:

| Option | SG new-game default | HnS setting | Status |
|---|---|---|---|
| Overworld speed | 1x (`:232-233`) | `VarSet(VAR_OVERWORLD_SPEEDUP, OPTIONS_OVERWORLD_SPEED_1X)` | **Landed (Phase 1)** — needed no explicit default write at all; 0 already means 1x. |
| Battle speed | **2x** (`:144`) | `VarSet(VAR_BATTLE_SPEED, OPTIONS_BATTLE_SPEED_2X)` | **Landed (Phase 2)** — see below, not where this section originally said. |
| Dark UI | Light / off (`:143`) | `VarSet(VAR_DARK_UI, 0)` | Planned (Phase 3) |
| Party menu | `PARTY_MENU_OPTION_CUSTOM` (SwSh) | `VarSet(VAR_PARTY_MENU_STYLE, PARTY_MENU_STYLE_SWSH + 1)` | Planned (Phase 4) |

**Correction, found while landing Phase 2 (§3.8): "set all four in
`NewGameInitData()`, alongside the existing `gSaveBlock2Ptr->options*`
initialisers" was wrong for how HnS is actually structured.** Those
`gSaveBlock2Ptr->options* =` lines live in a *separate* function,
`SetDefaultOptions()`, which runs exactly once — only when the save file is
empty or corrupt (`src/intro.c:903-904`) — not on every "New Game". A default
that must survive being written before the player's first real "New Game"
(as 2x must, since `NewGameInitData()`'s `InitEventData()` zeroes every
`SaveBlock1` var on every run, including a first-ever one) needs **both**
pieces, exactly mirroring SG's own real split (verified: SG's line 144 is in
its `SetDefaultOptions()`-equivalent, not its `NewGameInitData()`):

1. The one-time default write goes in HnS's `SetDefaultOptions()`
   (`src/new_game.c`), alongside its existing `gSaveBlock2Ptr->options... =`
   lines, in the same style (a bare `VarSet(VAR_BATTLE_SPEED, OPTIONS_BATTLE_SPEED_2X); //HnS`).
2. `NewGameInitData()` then needs the carry-over below regardless, or that
   one-time write is destroyed by the very first "New Game" that follows it.

An option whose "default" is 0 anyway (overworld speed's 1x) needs only the
carry-over, since an untouched var already reads 0 with no explicit write —
this is why Phase 1 has no `SetDefaultOptions()` line at all. Check which
case each of Phase 3/4's options falls into before assuming either pattern.

**Port SG's option carry-over regardless of which case applies.** SG
snapshots the player's current option value(s) before wiping the save (into
a `struct NewGameOptions` for several at once, or standalone locals — either
works) and restores them after, so starting a New Game does not silently
reset the player's preferences. HnS's `NewGameInitData()` already does
exactly this for a handful of `FlagGet`/`FlagSet` pairs
(`FLAG_DIFFICULTY_HARD` and similar, near the top and bottom of the
function) — Phases 1 and 2 both extended that same existing local-variable
idiom for their vars rather than introducing SG's separate struct, and later
phases should do the same for consistency with the surrounding code.

### 6.2 Existing saves

Every new option lives in a previously-unused `VAR_`, which reads **0** in a
save made before this work. That is deliberate, and it is what keeps old saves
compatible: nothing in `SaveBlock1`/`SaveBlock2` moves, so no field is
misinterpreted and no `STATIC_ASSERT` changes.

Consequence, per option:

* **Overworld speed** — 0 = 1x. Same as the new-game default. No visible change.
* **Battle speed** — 0 = 1x, whereas a *new* game starts at 2x. This asymmetry
  is exactly what Soulgold does (its 2x default is set only in
  `NewGameInitData`, so its own pre-feature saves also load at 1x). Keep it.
  **Do not add a migration that bumps existing saves to 2x** — silently
  speeding up a returning player's battles is a worse outcome than the
  inconsistency, and the option is one menu away.
* **Dark UI** — 0 = Light. Same as the new-game default. No visible change.
* **Party menu** — 0 means "never chosen" thanks to the `style + 1` encoding
  (§5.4), so the fallback in `GetPartyMenuStyle()` decides what a returning
  player sees. Set `PARTY_MENU_STYLE_DEFAULT` to the SwSh style, matching
  Soulgold's own fallback (`PARTY_MENU_DEFAULT_OPTION == PARTY_MENU_OPTION_CUSTOM`).
  Returning players therefore get the new screen and can switch back in
  Options. If the author later prefers existing saves to keep the classic HnS
  menu, that is a **one-line change** to the fallback constant — no save
  migration, no data change.

**Verification before merging any phase:** load a pre-change save in an
emulator, confirm the player's name, party, badges, bag and playtime are intact
and that the new options read as 1x / Light, then change each option, save,
reload and confirm it persisted.

---

## 7. Cross-cutting rules for the implementer

1. **Never copy a line from `../soulgold` without reading the HnS function it is
   going into.** The APIs differ everywhere (§0.1). A clean-looking copy that
   compiles can still be wrong because the HnS function has different
   preconditions.
2. **Build after every step, not every phase.** With no local toolchain, that
   means: push to the branch and let the Phase 0 CI build it. If Phase 0 is not
   landed yet, land it first.
3. **Do not touch `src/main.c`'s VBlank handler or `VBlankCB_Battle` at all.**
   The RNG rework is ruled out by decision (§3.4).
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
9. **No new top-level `static EWRAM_DATA` (or non-`static` `EWRAM_DATA`)
   anywhere in Phases 1–3 without checking §1.2's table first.** The baseline
   has 976 bytes of free EWRAM (§1.2) — that is not a rounding margin, it is
   close to nothing. Phases 1–3 are designed in this plan to need none, and
   Phase 2 landed with EWRAM exactly unchanged (§3.8: no bitfield was even
   added to `BattleStruct` in the end — see §3.8 for why); Phase 3's new data
   is `const` ROM palettes. If an implementation step seems to need a new
   static EWRAM byte anywhere in those three phases, that is a signal to stop
   and re-read this rule, not to add it and move on. Phase 4 (the one phase
   that does need EWRAM) has its own mandatory mitigation in §5.5 — read it
   before writing `swsh_party_menu.c`.
10. **If you build locally (§0.3), `git status` before committing — a local
    build regenerates `include/constants/map_groups.h` from `data/maps/*.json`
    via the `mapjson` tool, and the regenerated version can drift from what is
    checked in** (observed while verifying Phase 1: one map's generated macro
    name differed, plus a debug comment line the tool appends). This is a
    pre-existing quirk of the project's generated-file setup, unrelated to any
    feature in this plan. `git checkout -- include/constants/map_groups.h`
    before every commit that follows a local build, unless you specifically
    intended to update it (you did not, in any phase this plan describes).
11. **A `VAR_UNUSED_0x*` / `FLAG_UNUSED_0x*` name and comment is a claim, not a
    fact — verify every one before reusing it, every time, even ones this plan
    already names as free.** Phase 2 found that `VAR_UNUSED_0x40DC`, this
    plan's own suggested address for `VAR_BATTLE_SPEED` (§3.3 step 1), is
    actually read and written by `CheckNuzlockeMode()`
    (`src/script.c:569,571`) and read directly by a debug script
    (`data/scripts/debug.inc:80`) — reusing it would have silently broken
    that. §3.8 has the corrected address and the check that catches this:
    `grep` every candidate for real usage across `src/`, `data/` and `asm/`
    (symbol name *and* raw hex value) before picking one, the same way §0.4's
    freed vars/flags were meant to be checked and Phase 1's var already was.

---

## 8. Decisions taken (2026-09-13) — do not re-litigate

| # | Question | Decision |
|---|---|---|
| 1 | Which party menu | **SwSh "Custom"**, Soulgold's default skin. HGSS/BW out of scope. §5.2 |
| 2 | Battle RNG differing between speeds | **Acceptable.** Ship without the rework; do not touch the VBlank handlers. §3.4 |
| 3 | Dark UI scope | **Battle + Bag only**, exactly as Soulgold. No summary/PC/Pokédex. §4.1 |
| 4 | Dark UI vs the two HnS healthbox styles | **Do it Soulgold's way, applied to both styles.** Two dark `.pal` files, a two-entry constant table, nothing more. No gating, no abstraction. §4.3.1 |
| 5 | Defaults | **Soulgold's values for new games, existing saves untouched.** §6.1 |

Nothing in this plan is blocked on further input.

### 8.1 Confirmed by CI run #1 (2026-09-13) — not a decision, a fact

`main` builds green with `make modern`, and has **976 bytes of free EWRAM**
(99.63% used) against ~11 MB of free ROM. This is not something the author
chose; it is what the linker reports on the unmodified project. It changes
Phase 4 from "large but low-risk" to "large, and the naive port does not fit
without the heap-allocation change mandated in §5.5." See §1.2 for the full
table and §7 rule 9 for what this means for Phases 1–3.
