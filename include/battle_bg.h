#ifndef GUARD_BATTLE_BG_H
#define GUARD_BATTLE_BG_H

#include "event_data.h"

// Dedicated BG palette slots for the battle message window and the command
// window, so the dark UI can recolour them without disturbing the shared
// textbox palette. Ported from Soulgold (../soulgold/include/battle_bg.h).
#define BATTLE_COMMAND_PAL_NUM          13
// Soulgold keeps DARK_BATTLE_UI_BG_COLOR private to src/battle_bg.c. HnS needs
// it in battle_controller_player.c too, to repaint the move-category icon's
// backdrop, so it lives here instead.
#define DARK_BATTLE_UI_BG_COLOR         RGB(5, 5, 5)
#define BATTLE_WINDOW_DARK_BG_PAL_INDEX 8
#define BATTLE_WINDOW_DARK_FG_PAL_INDEX 14
#define BATTLE_WINDOW_DARK_SHADOW_PAL_INDEX 13

// Dark/Light battle + Bag UI option. Independent of optionsNewBattleUI (which
// healthbox art style is shown) - this only changes its colors.
static inline bool8 IsDarkUiEnabled(void)
{
    return VarGet(VAR_DARK_UI) != 0;
}

void BattleInitBgsAndWindows(void);
void InitBattleBgsVideo(void);
void LoadBattleMenuWindowGfx(void);
void DrawMainBattleBackground(void);
void LoadBattleTextboxAndBackground(void);
void InitLinkBattleVsScreen(u8 taskId);
void DrawBattleEntryBackground(void);
bool8 LoadChosenBattleElement(u8 caseId);

#endif // GUARD_BATTLE_BG_H
