#include "global.h"
#include "party_menu.h"

// Owns the party menu's public API (as declared in party_menu.h) and forwards
// each call to whichever style variant is selected. For now there is only one
// variant (HnS's existing two-column menu, src/party_menu.c, compiled under
// the HnsPartyMenu_ prefix via include/party_menu_variant.h) - this commit
// only proves the dispatch layer forwards correctly with no behaviour change.
// The SwSh variant and the runtime style option are added in a later commit
// of this phase (see docs/PORT_PLAN_SOULGOLD_FEATURES.md §5.3).
//
// The party menu's shared EWRAM state (gPartyMenu and friends),
// gTutorMoves, and CanLearnTutorMove are NOT duplicated here - they stay
// defined exactly once, in party_menu.c, and both variants (plus other
// systems like scrcmd.c) reference that same instance via the existing
// extern/plain declarations in party_menu.h.

#define HNS_FUNC(name) HnsPartyMenu_ ## name

#define DISPATCH_RET(returnType, name, params, args) \
    extern returnType HNS_FUNC(name) params;         \
    returnType name params                           \
    {                                                 \
        return HNS_FUNC(name) args;                  \
    }

#define DISPATCH_VOID(name, params, args) \
    extern void HNS_FUNC(name) params;    \
    void name params                      \
    {                                     \
        HNS_FUNC(name) args;              \
    }

DISPATCH_VOID(AnimatePartySlot, (u8 slot, u8 animNum), (slot, animNum))
DISPATCH_RET(bool8, IsMultiBattle, (void), ())
DISPATCH_RET(u8, GetCursorSelectionMonId, (void), ())
DISPATCH_RET(u8, GetPartyMenuType, (void), ())
DISPATCH_VOID(Task_HandleChooseMonInput, (u8 taskId), (taskId))
DISPATCH_RET(u8 *, GetMonNickname, (struct Pokemon *mon, u8 *dest), (mon, dest))
DISPATCH_RET(u8, DisplayPartyMenuMessage, (const u8 *str, bool8 keepOpen), (str, keepOpen))
DISPATCH_RET(bool8, IsPartyMenuTextPrinterActive, (void), ())
DISPATCH_VOID(PartyMenuModifyHP, (u8 taskId, u8 slot, s8 hpIncrement, s16 hpDifference, TaskFunc task), (taskId, slot, hpIncrement, hpDifference, task))
DISPATCH_RET(u8, GetAilmentFromStatus, (u32 status), (status))
DISPATCH_RET(u8, GetMonAilment, (struct Pokemon *mon), (mon))
DISPATCH_VOID(DisplayPartyMenuStdMessage, (u32 stringId), (stringId))
DISPATCH_RET(bool8, FieldCallback_PrepareFadeInFromMenu, (void), ())
DISPATCH_RET(bool8, FieldCallback_PrepareFadeInForTeleport, (void), ())
DISPATCH_VOID(CB2_ReturnToPartyMenuFromFlyMap, (void), ())
DISPATCH_VOID(LoadHeldItemIcons, (void), ())
DISPATCH_VOID(DrawHeldItemIconsForTrade, (u8 *partyCounts, u8 *partySpriteIds, u8 whichParty), (partyCounts, partySpriteIds, whichParty))
DISPATCH_VOID(CB2_ShowPartyMenuForItemUse, (void), ())
DISPATCH_VOID(ItemUseCB_Medicine, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_ReduceEV, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_PPRecovery, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_PPUp, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_RET(u16, ItemIdToBattleMoveId, (u16 item), (item))
DISPATCH_RET(u16, BattleMoveIdToItemId, (u16 moveId), (moveId))
DISPATCH_RET(bool8, IsMoveHm, (u16 move), (move))
DISPATCH_RET(bool8, MonKnowsMove, (struct Pokemon *mon, u16 move), (mon, move))
DISPATCH_RET(int, MoveToHM, (u16 move), (move))
DISPATCH_VOID(ItemUseCB_TMHM, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_RareCandy, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_SacredAsh, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_EvolutionStone, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_RET(u8, GetItemEffectType, (u16 item), (item))
DISPATCH_VOID(CB2_PartyMenuFromStartMenu, (void), ())
DISPATCH_VOID(CB2_ChooseMonToGiveItem, (void), ())
DISPATCH_VOID(ChooseMonToGiveMailFromMailbox, (void), ())
DISPATCH_VOID(InitChooseHalfPartyForBattle, (u8 unused), (unused))
DISPATCH_VOID(ClearSelectedPartyOrder, (void), ())
DISPATCH_VOID(ChooseMonForTradingBoard, (u8 menuType, MainCallback callback), (menuType, callback))
DISPATCH_VOID(ChooseMonForMoveTutor, (void), ())
DISPATCH_VOID(ChooseMonForWirelessMinigame, (void), ())
DISPATCH_VOID(OpenPartyMenuInBattle, (u8 partyAction), (partyAction))
DISPATCH_VOID(ChooseMonForInBattleItem, (void), ())
DISPATCH_VOID(BufferBattlePartyCurrentOrder, (void), ())
DISPATCH_VOID(BufferBattlePartyCurrentOrderBySide, (u8 battlerId, u8 flankId), (battlerId, flankId))
DISPATCH_VOID(SwitchPartyOrderLinkMulti, (u8 battlerId, u8 slot, u8 arrayIndex), (battlerId, slot, arrayIndex))
DISPATCH_VOID(SwitchPartyMonSlots, (u8 slot, u8 slot2), (slot, slot2))
DISPATCH_RET(u8, GetPartyIdFromBattlePartyId, (u8 slot), (slot))
DISPATCH_VOID(ShowPartyMenuToShowcaseMultiBattleParty, (void), ())
DISPATCH_VOID(ChooseMonForDaycare, (void), ())
DISPATCH_RET(bool8, CB2_FadeFromPartyMenu, (void), ())
DISPATCH_VOID(ChooseContestMon, (void), ())
DISPATCH_VOID(ChoosePartyMon, (void), ())
DISPATCH_VOID(ChooseMonForMoveRelearner, (void), ())
DISPATCH_VOID(BattlePyramidChooseMonHeldItems, (void), ())
DISPATCH_VOID(DoBattlePyramidMonsHaveHeldItem, (void), ())
DISPATCH_VOID(IsSelectedMonEgg, (void), ())
DISPATCH_VOID(IsLastMonThatKnowsSurf, (void), ())
DISPATCH_VOID(MoveDeleterForgetMove, (void), ())
DISPATCH_VOID(BufferMoveDeleterNicknameAndMove, (void), ())
DISPATCH_VOID(GetNumMovesSelectedMonHas, (void), ())
DISPATCH_VOID(MoveDeleterChooseMoveToForget, (void), ())
DISPATCH_VOID(ItemUseCB_PokeBall, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_VOID(ItemUseCB_Mints, (u8 taskId, TaskFunc task), (taskId, task))
DISPATCH_RET(u16, GetTMHMMoves, (u16 position), (position))

#undef DISPATCH_VOID
#undef DISPATCH_RET
#undef HNS_FUNC
