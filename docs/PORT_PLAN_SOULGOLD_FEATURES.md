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
| 3 — dark/light UI | 261168 B (99.63%, unchanged) | 976 B | 22507816 B (67.08%, +964 B from Phase 2) |
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

### 4.6 Implementation notes — landed, real deviations from §4.2/§4.3/§4.4 above

Verified with a real `make modern` build (§0.3): 0 errors, 0 warnings in any
touched file, EWRAM/IWRAM unchanged from the Phase 2 row in §1.2's table, ROM
+964 bytes (all `const` palette data, no new `EWRAM_DATA`). As with Phase 2,
several §4.2/§4.3/§4.4 assumptions — carried over from Soulgold's own,
different battle-UI architecture — turned out not to match HnS's actual
source once traced, and the real design ended up simpler in some places and
requiring more call sites in others:

* **HnS has one text palette per window role, not Soulgold's separate
  message/command palette split.** Tracing `sTextOnWindowsInfo_Normal[]` in
  `src/battle_message.c` (the table that actually drives
  `BattlePutTextOnWindow()`'s `fgColor`/`bgColor`/`shadowColor` per window)
  showed HnS uses palette slot 0 for the message box and slot 5 for the
  command menu, move selection *and* the level-up window all together —
  Soulgold's `BATTLE_MESSAGE_TEXT_PAL_NUM`/`BATTLE_COMMAND_PAL_NUM` split does
  not exist in HnS. This eliminates §4.4 step 7's separate text-colour
  branches in `battle_message.c`/`battle_controller_player.c`/
  `battle_script_commands.c` entirely: with slot 0 driven by
  `gBattleTextboxPalette`/`gBattleTextboxPalette_Dark` and slot 5 by
  `gBattleWindowTextPalette`/`gBattleWindowTextPalette_Dark` (both loaded in
  `battle_bg.c`), every window that reads colours from those two slots — the
  level-up window included, with no separate table needed for it — gets the
  dark treatment for free at the palette level. No text-colour source changes
  were needed anywhere.
* **`LoadBattleMoveDescriptionWindowGfx()` does not exist in HnS** — grepped
  and confirmed absent from both `src/` and `include/`. HnS has no separate
  move-description window load path distinct from `LoadBattleMenuWindowGfx()`
  (§4.4 step 3's second target), so only that one function needed the palette
  swap.
* **§4.4 step 8's day/night blend exclusion is not needed — verified, not
  just skipped.** Grepped for `TimeMixBattleSpritePalette` and
  `UpdatePalettesWithTime` across `src/palette.c` and every `src/battle*.c`
  file: neither exists, and nothing in the battle scene's palette path calls
  into a time-of-day blend. HnS's battle UI has no day/night palette tinting
  at all, so there is no blend step to exclude the dark colours from.
* **`BATTLE_WINDOW_DARK_BG_PAL_INDEX` (§4.4 step 2) was deliberately not
  added.** The window *border* (palette slot 1, loaded by
  `LoadUserWindowBorderGfx(..., BG_PLTT_ID(1))` in `LoadBattleMenuWindowGfx()`)
  is shared with the game-wide, player-customisable "Frame" option
  (`gSaveBlock2Ptr->optionsWindowFrameType`) — darkening it for the Dark UI
  option would fight with whatever frame the player has chosen elsewhere.
  Scope was narrowed to what §4.1 actually asked for (battle + Bag, matching
  Soulgold's own scope): only the text palette (slot 5) and the two
  textbox/background slots (slot 0) are swapped; the border is left alone in
  both modes.
* **The real `optionsNewBattleUI` branch count in `battle_interface.c` is 15,
  not §4.4 step 6's estimated ~24.** All 15 were read; only two needed the
  dark-routing treatment beyond `GetHealthBoxHealthBarPalettes()`
  (`src/battle_gfx_sfx_util.c`, already handled by §4.4 step 5):
  `GetStatusSummaryBarSpritePal()` and `GetStatusSummaryBallsSpritePal()`,
  which select the identical `gBattleInterface_BallStatusBarPalGen3/4` and
  `gBattleInterface_BallDisplayPalGen3/4` constants (just under the
  `TAG_STATUS_SUMMARY_*` sprite tags instead of `TAG_HEALTHBOX_PAL`/
  `TAG_HEALTHBAR_PAL`) — no new remap/copy helper table was needed, the same
  four existing palette pairs are simply reused. The other 13 branches select
  sprite sheets/tilemaps/coordinates that don't involve colour at all.
* **No `graphics_file_rules.mk` entries were needed for any of the 8 new
  `.pal` files.** `Makefile:271-277` has generic pattern rules
  (`%.gbapal: %.pal`, `%.gbapal: %.png`, `%.lz: %`) that cover a bare
  `.pal → .gbapal[.lz]` conversion with no per-file rule required — confirmed
  by the fact that the build picked up all 8 files with zero
  `graphics_file_rules.mk` changes.
* **New `.pal` source files must use CRLF line endings, not LF.** `gbagfx`
  rejected the first build attempt (`LF line endings aren't supported.`) for
  every one of the 8 new files, which were authored with plain `\n`.
  Byte-inspecting an existing, working `.pal` (`graphics/bag/menu_male.pal`)
  confirmed every line ends `\r\n`. Fixed by converting all 8 new files
  after the fact — this is a build-tool requirement worth flagging for any
  future `.pal` file added by hand rather than exported from an image editor.
* **HnS's bag male/female palettes are identical**, matching the light
  versions (`graphics/bag/menu_male.pal`/`menu_female.pal` are themselves
  byte-identical in HnS) — so `menu_male_dark.pal` and `menu_female_dark.pal`
  were authored with the same content, as §4.4 step 9 anticipated checking.
* **The dark colour values not already fixed by Soulgold's own proven
  healthbox palette** (ball_display, ball_displaygen3, and both bag
  palettes) were derived by reverse-engineering Soulgold's own darkening
  ratio from its shipped healthbox dark palette (65→12, 227→43, 186→35,
  154→29 all match `round(x × 3⁄16)` exactly) and applying that same ratio
  mechanically to every remaining gray/chrome palette index that needed a new
  value, leaving colourful accent indices (HP bar colours, ball icon colours)
  untouched. This is a principled, reproducible substitute for the visual
  judgement that would normally decide these values, and is called out here
  since it cannot be verified by an automated build the way the mechanical
  parts of this port can.
* **`VAR_DARK_UI` needed no explicit default write in
  `SetDefaultOptions()`**, unlike Phase 2's `VAR_BATTLE_SPEED`: Light (0) is
  exactly what an untouched var already reads as, matching Phase 1's
  overworld-speed default rather than Phase 2's — confirmed by reading
  `SetDefaultOptions()` directly rather than assuming either pattern applied.
  `NewGameInitData()` still needs the snapshot/restore carry-over (same as
  Phases 1–2), since `InitEventData()` wipes `SaveBlock1` vars on every New
  Game regardless of what the default was.

### 4.7 Post-release fix — dark palette values were over-darkened

The user played a real build and reported the Dark UI option breaking the
battle healthbar and making the Bag background "completamente scuro" (looks
like flat black with no visible frame). Re-comparing against Soulgold's real
shipped data (not the mechanical extrapolation from §4.6) found two separate
mistakes, fixed by re-tracing what Soulgold *actually* does rather than
reasoning about the numbers again:

* **The `× 3⁄16` ratio reverse-engineered in §4.6 is only valid for the one
  palette Soulgold actually ships a dark variant for — the healthbox frame**
  (`ball_status_bar`/`ball_status_bargen3`, entries 0–4). Applying the same
  ratio to the Bag palettes crushed their 5-step bevel-gray ramp
  (255/205/164/123/98) down to a 18–48 band that reads as flat black on a GBA
  screen — the ramp needs to stay a ramp to look like a frame at all. Fixed
  by re-deriving the Bag's dark values directly from Soulgold's own
  `menu_male_dark.pal`/`menu_female_dark.pal`, index by index: Soulgold
  darkens its equivalent chrome/gradient indices to roughly 50–90% of their
  original brightness (preserving the ramp's contrast) and reserves the
  extreme ×3⁄16-ish crush for exactly one flat single-tone fill color. HnS's
  bag chrome indices got the same treatment: ~55–60% brightness kept instead
  of ~19%, while the untouched item-icon/accent indices (already
  byte-identical to light, correctly) were left alone.
* **Soulgold has no dark variant for the healthbar or the status-summary
  ball row at all.** Traced `LoadBattleInterfacePalettes`,
  `sSpritePalettes_HealthBoxHealthBar[]`, `sStatusSummaryBarSpritePal` and
  `sStatusSummaryBallsSpritePal` in Soulgold's `battle_gfx_sfx_util.c`/
  `battle_interface.c`: only `TAG_HEALTHBOX_PAL` (the frame) swaps for dark;
  `TAG_HEALTHBAR_PAL` and the status-summary tags are always loaded from the
  plain light palette, unconditionally, even when Soulgold's own Dark UI
  option is on. HnS had invented `ball_display_dark`/`ball_displaygen3_dark`
  (the actual HP/EXP gauge fill) with no Soulgold precedent to copy — this is
  the likely source of the reported gauge corruption, since those values were
  never validated against anything real. Fixed by mirroring Soulgold exactly:
  `GetHealthBoxHealthBarPalettes()`, `GetStatusSummaryBarSpritePal()` and
  `GetStatusSummaryBallsSpritePal()` now always use the light
  `gBattleInterface_BallDisplayPalGen3/4` and
  `gBattleInterface_BallStatusBarPalGen3/4` for the bar/balls regardless of
  the Dark UI setting, and the now-unused `ball_display_dark.pal`/
  `ball_displaygen3_dark.pal` files and their `graphics.c`/`graphics.h`
  declarations were deleted rather than kept as dead ROM data.
* `ball_status_bar_dark.pal`/`ball_status_bargen3_dark.pal` (the healthbox
  frame itself) were **not** touched — re-verified against Soulgold's shipped
  `dark_healthbox.pal` and confirmed still byte-correct (mod GBA 5-bit
  quantization), matching what §4.3/§4.6 already established.
* `text_dark.pal` (`gBattleWindowTextPalette_Dark`, palette slot 5) had a
  separate, unrelated bug: indices 11–14 were inverted (a mid-gray became
  white, white became near-black) instead of darkened, which was never a
  Soulgold-derived value to begin with. Fixed to a plain proportional darken
  (~60% brightness) like the rest, with no inversion.
* Verified with `make modern`: 0 errors, 0 new warnings, ROM size decreased
  slightly (removed dead palette data) instead of growing.


### 4.8 Faithful re-port (2026-09-14) — what transfers from Soulgold and what does not

The Dark UI was re-ported against Soulgold line by line and then verified in
mGBA (`VAR_DARK_UI` toggled from the in-game Options menu and, for the
screenshots, poked directly into `gSaveBlock1Ptr->vars[]`). Result: **the
control flow ports verbatim, the palette *indices* do not.**

Ported verbatim from Soulgold, unchanged:

* `include/battle_bg.h`: `BATTLE_COMMAND_PAL_NUM 13`,
  `BATTLE_WINDOW_DARK_BG_PAL_INDEX 8`, `_FG_ 14`, `_SHADOW_ 13`.
* `src/battle_bg.c`: `sBattleMessageTextPalette`, `sDarkBattleCommandPalette`,
  `sDarkBattleUiBgColor/TextColor/TextShadowColor`, the four window templates
  moved off `paletteNum = 0` onto slots 12/13, and the whole of
  `LoadBattleMenuWindowGfx` including its per-index `LoadPalette` overrides.
* `src/battle_controller_player.c`: the four move/action cursor sites
  (`... |= BATTLE_COMMAND_PAL_NUM << 12`, destroy uses
  `(BATTLE_COMMAND_PAL_NUM << 12) | 0x16`).
* `src/battle_script_commands.c`: `sLevelUpWindowTextColors` and the yes/no
  cursor pair. (`BattleCreate/DestroyPostCatchMenuCursorAt` and
  `...CatchOrNotCursorAt` have no HnS counterpart — omitted, not forgotten.)
* `src/battle_message.c`: `IsDarkBattleCommandWindow` + the
  `FillWindowPixelBuffer`/text-colour override in `BattlePutTextOnWindow`.
  `B_CATCH_OR_NOT` / `B_WIN_POST_CATCH_MENU` omitted for the same reason.
* `src/item_menu.c`: all six dark-Bag palettes, `PrepareDarkBagHmIconGfx`,
  `COLORID_TMHM_INFO_DARK`, and the five call sites.

Deliberately **not** ported, with reasons:

* `src/palette.c` — `TimeMixBattleBgPalette` is expansion DNS code; HnS has no
  such function.
* `src/battle_interface.c` — `IsDarkHealthbox`, `RemapHealthbarGfxIndexes`,
  `CopyHealthbarGfx`, `CopyStatusIconGfx`. These exist in Soulgold only
  because its `dark_healthbox.gbapal` repurposes palette entries 1/3 (used by
  the healthbar gfx) and ships status-icon art authored in *dark* indices.
  HnS' healthbox art is different (and doubled: Gen 3 / Gen 4 skins), its
  status icons are light-authored, and its bar colours do not collide.
  Probing palette RAM live confirmed no remap is needed.

**The one thing that does not transfer: which palette index is the window
body.** Soulgold's battle textbox uses index 15 for the message-box fill, so
overriding `BG_PLTT_ID(0) + 15` is enough there. Live probing showed HnS' own
`textbox.gbapal` does the same for the message box — good — but *not* for the
Bag, where the item-list panel is BG index 9 and its frame index 10. A literal
port therefore leaves the Bag's list panel cream-coloured. The fix is at the
asset level, in HnS' own `graphics/bag/menu_{male,female}_dark.pal`:
index 9 -> `40 40 40` (panel body), index 10 -> `189 156 90` (gold frame),
index 26 -> `40 40 40`.

Same class of problem on the healthbox: HnS draws the mon name, level and HP
numbers with sprite-palette **index 1**, which the dark healthbox palettes had
set to `12 12 12` — near-black text on a near-black box. Fixed by setting
index 1 to `251 251 251` in both `ball_status_bar_dark.pal` and
`ball_status_bargen3_dark.pal`.

Also reverted from the first attempt: `gBattleTextboxPalette_Dark` (a whole
second textbox palette) is gone. Soulgold always loads the light
`gBattleTextboxPalette` and only overrides individual indices; now HnS does
too, and the asset and its `extern` were deleted.

Method note, worth reusing: to find which palette index paints a given region,
write saturated marker colours into `gPlttBufferFaded` **and**
`gPlttBufferUnfaded` (BG base `0x0201cf7c` / `0x0201cb7c` in the current
build; OBJ starts `+0x200`), run three frames, screenshot. One run identifies
every index at once. To decode many indices in one shot, set entry *i*
to `RGB(i, 0, 31 - i)` and read *i* back from the rendered red channel. `gSaveBlock1Ptr->vars[]` is at SaveBlock1 **+0x1490** in
this build — *not* the `/*0x139C*/` comment in `include/global.h`, which is
stale; derive it by diffing memory around an in-game option toggle.

### 4.9 Second round of index collisions (2026-09-14)

Three more places where HnS' art uses pixel indices a Soulgold-shaped palette
leaves at 0, found by playtest and confirmed with the probe above:

* **Pocket indicator squares.** Soulgold's `sDarkBagPocketIndicator*Palette`
  fill only entries 0/1/9 because its tiles (`0xC` / `0x34`) use them. HnS'
  tiles (`0x17` / `0x2B`) use 10 and 12-14, so the active square rendered
  black and the inactive ones near-black. Both palettes now carry the
  indicator colour on every entry except 0 (the tile background); that also
  makes them immune to any later tile change.
* **Bag palette index 10.** Darkening it to `189 156 90` in §4.8 flattened the
  Poké Ball icon, whose highlight is index 10 and whose ring is index 14/20 —
  same colour, no shape. Now `232 203 133`, brighter than the ring.
* **Move-category icon.** `sSplitIcons_Pal` / `sSplitIconsEmpty_Pal` paint the
  backdrop behind the icon white (index 7, shaded by 9). Invisible on the
  light move window, a white box on the dark one that also covered the first
  `P` of `PP`. `MoveSelectionDisplaySplitIcon` now overrides those two entries
  with `DARK_BATTLE_UI_BG_COLOR`, which moved from `src/battle_bg.c` to
  `include/battle_bg.h` so `battle_controller_player.c` can use it.

Also checked and **not** a problem: the day/night blend. HnS blends BG palettes
0-12 (`PALETTES_MAP`) and untagged sprite palettes with the time of day, so the
dark UI slots are in scope — but a night-time playtest (Bag, wild battle,
command window, move list, healthboxes) showed no tinting artefacts, so
Soulgold's `TimeMixBattleBgPalette` guard stays unported. Note for the future:
HnS' day/night code lives in `src/overworld.c` (`UpdatePalettesWithTime`,
`UpdateSpritePaletteWithTime`) and `src/palette.c`
(`UpdateTimeOfDayPaletteFade`), not under the Soulgold name.

To force night in the harness, run mGBA under a TZ that puts local time in the
night window, e.g. `TZ=Pacific/Kiritimati`; check `gTimeOfDay` (`0x030037ce`,
0 = night).

### 4.10 The healthbox skin matters — test BOTH (2026-09-14)

The round above was tested only with `optionsNewBattleUI == 0` (Gen 3
healthboxes) and looked clean. A playtest on **Gen 4** showed a stark white box
border and a white block where the selection cursor should be. Both are the
same index collision, and both are skin-specific, so **every dark-UI change has
to be checked on both healthbox skins.**

* **Healthbox text vs. border.** `AddTextPrinterAndCreateWindowOnHealthbox`
  prints with `color[1] = 1`. In the *Gen 3* art entry 1 is text only (the box
  borders on 7/8), so `251 251 251` there is correct. In the *Gen 4* art entry 1
  is text **and** the outer border, so the same value turned the border white.
  The Gen 4 dark palette now puts the border back on `12 12 12` (Soulgold's own
  value) and moves the text to entry 5 — spare in that skin, and the same entry
  Soulgold frees in `dark_healthbox.gbapal`. `HEALTHBOX_DARK_TEXT_PAL_INDEX`
  selects it, only when the dark UI and the Gen 4 skin are both on.
* **Selection cursor.** Soulgold draws the cursor out of
  `BATTLE_COMMAND_PAL_NUM`. HnS cannot: its cursor tiles (BG0 tiles 1 and 2) use
  entry 1 as the *tile background*, and entry 1 of the command palette is
  already the "What will X do?" prompt's text colour — one of them always
  loses. The first attempt at this (dark entry 1) duly fixed the cursor and
  blanked the prompt. The cursor now has its own slot,
  `BATTLE_CURSOR_PAL_NUM 11` (free for the whole battle, verified by dumping all
  16 BG palettes mid-battle), with 1 = background, 7 = arrow edge, 9 = arrow
  body, 14 = the erase tile 0x16.
* **Bag list and description text.** `WIN_ITEM_LIST` uses BG palette 1 and the
  list menu draws with `cursorPal = 1` / `cursorShadowPal = 3`, i.e. palette
  entries 17 and 19. The dark palette had 17 = `0 0 0` and 19 = `128 128 120`,
  so every glyph on the Bag screen was a black fill with a light outline —
  legible on the cream panel, invisible on the dark one. Swapped:
  17 = `251 251 251`, 19 = `8 8 8`.

Not changed, and worth recording because it was asked about: the battle text
sits close to the window edge in light mode too. `git diff main` shows no
window geometry (`tilemapLeft/Top`, `width/height`) or text placement
(`.x/.y`, letter/line spacing, font) touched anywhere in this branch — that
layout is HnS' own and predates the dark UI.

### 4.11 Bag starfield — verified, and what it is not (2026-09-14)

The branch behind PR #2 was rebuilt on top of the merged `main` (dark UI, SwSh
menu, speed-ups all come from `main`; the branch's own older copies of those
were dropped). What is left that is unique to it is exactly six files: the
**starfield background** behind the Bag.

Verified against Soulgold's source and assets:

* **Mechanism matches.** Same four-BG setup, BG3 on `charBaseIndex = 3` (shared
  with BG2), `mapBaseIndex = 28`, `priority = 3`, shown with `ShowBg(3)`. The
  starfield tilemap is full-screen in Soulgold too; what makes it visible only
  in the left column is the *foreground* tilemap's transparent tiles, not the
  starfield's shape.
* **Self-contained.** The 14 star tiles live in a separate copy of the tileset
  (`menu_with_stars.png`, 128x40 vs 128x32) with only **tile 2** blanked to
  transparent. `gBagScreen_Gfx` itself is untouched, so
  `battle_pyramid_bag.c` — which loads it with a different tilemap that needs
  those tiles opaque — still works.
* **Two deviations from Soulgold, both deliberate.** Soulgold's stars sit on
  the Bag's own palette banks; here they get their own palette at
  `BG_PLTT_ID(2)`, which is free on this screen. And the tilemap goes straight
  to VRAM (`LZDecompressVram` to `BG_SCREEN_ADDR(28)`) rather than through a
  second `SetBgTilemapBuffer`, since nothing scrolls it.
* **Playtested** in the emulator on all five pockets and the item submenu, in
  both Light and Dark themes: consistent, no bleed-through into the item list
  panel or the description window, no regression from the merged dark UI.

**What it is not.** This is a background, not a port of Soulgold's Bag screen.
Rendering Soulgold's own `menu.png` + `menu.bin` + `menu_male.pal` side by side
with HnS' Bag makes that plain: Soulgold's layout, panel shapes, header, key
item box and proportions are all different. Porting *that* means replacing HnS'
Bag artwork and tilemap wholesale and re-deriving the dark theme on top of it —
a much larger job than this branch, and not what this branch does.

### 4.12 Two things the first pass got wrong — the scroll and the palette

Playtest feedback: *"lo sfondo è statico in Heart and Soul ma mi sembra che
Soulgold sia in movimento"*, and the dark theme looked off. Both correct, both
confirmed in Soulgold's source.

* **It scrolls.** `ChangeBgY(3, 128, BG_COORD_ADD)` — 0.5 px per frame, the
  value being 8.8 fixed point — is called from four of Soulgold's per-frame Bag
  tasks (`Task_BagMenu_HandleInput`, `Task_HandleSwappingItemsInput`,
  `Task_ItemContext_SingleRow`, `Task_ItemContext_MultipleRows`) plus once in
  `SwitchBagPocket`. HnS has all five functions under the same names, so the
  same call now sits in the same five places. The tilemap is a full 32x32 map on
  a 256x256 BG, so it wraps seamlessly. The BG3 tilemap going straight to VRAM
  rather than through `SetBgTilemapBuffer` does not matter here: `ChangeBgY`
  only writes the hardware offset.
* **The palette has four variants, not one.** Soulgold's starfield sits on the
  Bag's own palette **bank 0**, so it inherits male/female and light/dark for
  free. It cannot here: HnS' bank 0 at the exact indices the star tiles use
  (1, 2, 3, 6, 8, 10) holds greys and gold, not a night sky — which is why the
  stars were given their own bank in the first place, and that decision stands.
  What was missing is that the *variants* then have to be selected explicitly.
  `scrolling_bg.pal` turned out to be Soulgold's male light bank 0 copied
  verbatim, so the other three were extracted the same way from
  `menu_male_dark.pal`, `menu_female.pal` and `menu_female_dark.pal`, and
  `LoadBagMenu_Graphics` now picks between them with the same gender test the
  Bag's own palette uses.

Measured in the emulator: sky `(24, 24, 66)` in light and `(8, 16, 33)` in dark,
matching Soulgold's `(26, 31, 66)` / `(8, 16, 32)` to within 5-bit rounding, and
the star field visibly advances frame to frame.

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

### 5.8 RESOLVED (2026-09-14): SwSh crashed on open — NULL sprite callbacks

**Root cause: seven of the SwSh `SpriteTemplate`s omit `.callback` (and some
also omit `.anims` / `.affineAnims`), which is harmless on
pokeemerald-expansion but fatal on HnS's base.**

`CreateSpriteAt` in pokeemerald-expansion (`../soulgold/src/sprite.c`)
substitutes defaults for missing template fields:

```c
sprite->anims       = template->anims       ? template->anims       : gDummySpriteAnimTable;
sprite->affineAnims = template->affineAnims ? template->affineAnims : gDummySpriteAffineAnimTable;
sprite->callback    = template->callback    ? template->callback    : SpriteCallbackDummy;
```

HnS's `gflib/sprite.c` assigns them straight through, with no fallbacks. So a
template that leaves `.callback` unset produces a sprite whose callback is
NULL, and `AnimateSprites` — identical in both projects — calls it
unconditionally on the very next frame:

```
AnimateSprites+0x4c:  ldr r3, [r6, #28]   ; r3 = sprite->callback  (NULL)
                      bl  _call_via_r3    ; "bx r3"  ->  PC = 0
```

`bx` to 0 executes the BIOS's ARM reset vector as Thumb, which faults to the
undefined-instruction vector (0x00000004) and ends up back at the cartridge
entry point (0x08000000) — i.e. the console appears to "reset to the boot
screen", exactly the reported symptom. It reproduces 100% of the time because
the hover-cursor sprite is created during menu setup.

**Fix**: set `.anims` / `.affineAnims` / `.callback` explicitly on every
SpriteTemplate in `src/data/swsh_party_menu.h`, using the same defaults
expansion would have substituted (`gDummySpriteAnimTable`,
`gDummySpriteAffineAnimTable`, `SpriteCallbackDummy`). Minimal blast radius —
only the ported data changes, no shared engine code is touched. A note above
the first template records the constraint for anyone adding templates later.

**Correction to an earlier diagnosis in this document's history:** a previous
session concluded the crash was a hardware *Prefetch Abort* inside a `CpuSet`
BIOS call. **That was wrong**, and worth recording so nobody repeats it. The
"evidence" was a trace showing `PC = 0x0000000C` right after `CpuSet`. But
`CpuSet` is just `svc 11`, and on ARM the SWI vector is at 0x00000008, which
*reads as 0x0C* because of the +4 instruction-pipeline offset. **Every BIOS
call in the game passes through that value**, so treating `pc < 0x20` as a
fault signature produced a false positive on essentially any frame. The
correct, unambiguous reset signature is `PC` entering the ROM entry region
(`0x08000000`–`0x080000C0`); detecting only that leads straight to the real
call chain above in a single trace.

Also disproved along the way, so they need not be re-investigated: the heap is
healthy at the moment of the crash (peak 65144 of 114672 bytes across 25
blocks, every block's magic intact, no overflow past the end into `gSprites`,
largest free block ~49 KB — no exhaustion and no fragmentation); every
compressed asset's declared decompressed size matches its destination buffer
exactly; and the port itself is faithful to Soulgold — `ShowPartyMenu`,
`InitPartyMenu`, `AllocPartyMenuBg`, `DecompressGraphics`,
`InitPartyMenuWindows`, `LoadPartyMenuWindows`, `LoadPartyMenuBoxes` and
`ResetPartyMenu` are byte-identical to the originals apart from the deliberate
`.smol`→`.lz` asset re-encode and the heap-allocated `gComfyAnims`.


### 5.9 Pokédex action: ported the "open at species" entry point

The port had reduced `CB2_OpenPartyPokedex` to a bare `CB2_OpenPokedexPlusHGSS()`,
so the Pokédex action opened at the top of the list and returned to the field
instead of opening that mon's page and coming back to the party menu.

Soulgold gets this from `OpenPokedexPlusHGSSAtSpecies(species, callback)`, which
HnS's `pokedex_plus_hgss.c` did not have. Ported it, minimally: four EWRAM
statics (~12 bytes), a `TrySelectPokedexListDexNum()` helper, the entry point
itself, and three hooks — select the entry in `LoadPokedexListPage`'s PAGE_MAIN
branch, jump to the info screen in `Task_OpenPokedexMainPage`, honour the return
callback in `Task_ClosePokedex`. All of it is inert unless
`OpenPokedexPlusHGSSAtSpecies` is called, so the normal Pokédex path is
unchanged.

Checked at the same time, and **not** broken: the HM / field-move path. The SwSh
menu builds its action list and dispatches to `CursorCb_FieldMove` exactly as
`party_menu.c` does, and both read the same shared `sFieldMoveCursorCallbacks`
table in `src/data/party_menu.h`. FLASH showing up for a low-level starter is
HnS's own "HMs overwrite" challenge option (slot 1 may use Fly/Flash when the HM
is in the bag), identical in both menus.

---

## 6. Defaults, and compatibility with existing saves

### 6.1 What each option defaults to

Soulgold's values, taken from `../soulgold/src/new_game.c`:

| Option | SG new-game default | HnS setting | Status |
|---|---|---|---|
| Overworld speed | 1x (`:232-233`) | `VarSet(VAR_OVERWORLD_SPEEDUP, OPTIONS_OVERWORLD_SPEED_1X)` | **Landed (Phase 1)** — needed no explicit default write at all; 0 already means 1x. |
| Battle speed | **2x** (`:144`) | `VarSet(VAR_BATTLE_SPEED, OPTIONS_BATTLE_SPEED_2X)` | **Landed (Phase 2)** — see below, not where this section originally said. |
| Dark UI | Light / off (`:143`) | (no write needed) | **Landed (Phase 3)** — like overworld speed, needed no explicit default write; 0 already means Light. |
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
