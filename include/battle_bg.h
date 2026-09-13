#ifndef GUARD_BATTLE_BG_H
#define GUARD_BATTLE_BG_H

#include "event_data.h"

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
