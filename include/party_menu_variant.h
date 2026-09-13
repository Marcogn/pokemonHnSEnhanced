#ifndef GUARD_PARTY_MENU_VARIANT_H
#define GUARD_PARTY_MENU_VARIANT_H

// The runtime party-menu style option builds both implementations as separate
// translation units and gives each a private API; party_menu_dispatch.c owns
// the public API (the functions declared in party_menu.h) and forwards to
// whichever variant is selected at runtime. This list is HnS's own - derived
// from HnS's include/party_menu.h, not copied from Soulgold's, since HnS's
// party_menu.c has diverged substantially (see
// docs/PORT_PLAN_SOULGOLD_FEATURES.md §5.1).
//
// Deliberately NOT renamed here (see §5.3 step 2's implementation notes):
// the shared EWRAM state (gPartyMenu, gPartyMenuUseExitCallback,
// gSelectedMonPartyId, gPostMenuFieldCallback, gSelectedOrderFromParty,
// gBattlePartyCurrentOrder, gItemUseCB) and gTutorMoves stay defined exactly
// once, in party_menu.c, and are shared via their existing party_menu.h
// extern declarations - both variants reference the same instance rather
// than each getting a private copy. MoveIdToTutorIndex is also excluded: it
// is a plain data lookup called directly from src/scrcmd.c via an ad hoc
// extern (not part of party_menu.h's public API), shared the same way.
#define PARTY_MENU_VARIANT_JOIN_INNER(prefix, name) prefix ## name
#define PARTY_MENU_VARIANT_JOIN(prefix, name) PARTY_MENU_VARIANT_JOIN_INNER(prefix, name)

#if defined(PARTY_MENU_VARIANT_SWSH)
#define PARTY_MENU_VARIANT_NAME(name) PARTY_MENU_VARIANT_JOIN(SwShPartyMenu_, name)
#elif defined(PARTY_MENU_VARIANT_HNS)
#define PARTY_MENU_VARIANT_NAME(name) PARTY_MENU_VARIANT_JOIN(HnsPartyMenu_, name)
#else
#error "A party menu variant must be selected"
#endif

#define AnimatePartySlot PARTY_MENU_VARIANT_NAME(AnimatePartySlot)
#define IsMultiBattle PARTY_MENU_VARIANT_NAME(IsMultiBattle)
#define GetCursorSelectionMonId PARTY_MENU_VARIANT_NAME(GetCursorSelectionMonId)
#define GetPartyMenuType PARTY_MENU_VARIANT_NAME(GetPartyMenuType)
#define Task_HandleChooseMonInput PARTY_MENU_VARIANT_NAME(Task_HandleChooseMonInput)
#define GetMonNickname PARTY_MENU_VARIANT_NAME(GetMonNickname)
#define DisplayPartyMenuMessage PARTY_MENU_VARIANT_NAME(DisplayPartyMenuMessage)
#define IsPartyMenuTextPrinterActive PARTY_MENU_VARIANT_NAME(IsPartyMenuTextPrinterActive)
#define PartyMenuModifyHP PARTY_MENU_VARIANT_NAME(PartyMenuModifyHP)
#define GetAilmentFromStatus PARTY_MENU_VARIANT_NAME(GetAilmentFromStatus)
#define GetMonAilment PARTY_MENU_VARIANT_NAME(GetMonAilment)
#define DisplayPartyMenuStdMessage PARTY_MENU_VARIANT_NAME(DisplayPartyMenuStdMessage)
#define FieldCallback_PrepareFadeInFromMenu PARTY_MENU_VARIANT_NAME(FieldCallback_PrepareFadeInFromMenu)
#define FieldCallback_PrepareFadeInForTeleport PARTY_MENU_VARIANT_NAME(FieldCallback_PrepareFadeInForTeleport)
#define CB2_ReturnToPartyMenuFromFlyMap PARTY_MENU_VARIANT_NAME(CB2_ReturnToPartyMenuFromFlyMap)
#define LoadHeldItemIcons PARTY_MENU_VARIANT_NAME(LoadHeldItemIcons)
#define DrawHeldItemIconsForTrade PARTY_MENU_VARIANT_NAME(DrawHeldItemIconsForTrade)
#define CB2_ShowPartyMenuForItemUse PARTY_MENU_VARIANT_NAME(CB2_ShowPartyMenuForItemUse)
#define ItemUseCB_Medicine PARTY_MENU_VARIANT_NAME(ItemUseCB_Medicine)
#define ItemUseCB_ReduceEV PARTY_MENU_VARIANT_NAME(ItemUseCB_ReduceEV)
#define ItemUseCB_PPRecovery PARTY_MENU_VARIANT_NAME(ItemUseCB_PPRecovery)
#define ItemUseCB_PPUp PARTY_MENU_VARIANT_NAME(ItemUseCB_PPUp)
#define ItemIdToBattleMoveId PARTY_MENU_VARIANT_NAME(ItemIdToBattleMoveId)
#define BattleMoveIdToItemId PARTY_MENU_VARIANT_NAME(BattleMoveIdToItemId)
#define IsMoveHm PARTY_MENU_VARIANT_NAME(IsMoveHm)
#define MonKnowsMove PARTY_MENU_VARIANT_NAME(MonKnowsMove)
#define MoveToHM PARTY_MENU_VARIANT_NAME(MoveToHM)
#define ItemUseCB_TMHM PARTY_MENU_VARIANT_NAME(ItemUseCB_TMHM)
#define ItemUseCB_RareCandy PARTY_MENU_VARIANT_NAME(ItemUseCB_RareCandy)
#define ItemUseCB_SacredAsh PARTY_MENU_VARIANT_NAME(ItemUseCB_SacredAsh)
#define ItemUseCB_EvolutionStone PARTY_MENU_VARIANT_NAME(ItemUseCB_EvolutionStone)
#define GetItemEffectType PARTY_MENU_VARIANT_NAME(GetItemEffectType)
#define CB2_PartyMenuFromStartMenu PARTY_MENU_VARIANT_NAME(CB2_PartyMenuFromStartMenu)
#define CB2_ChooseMonToGiveItem PARTY_MENU_VARIANT_NAME(CB2_ChooseMonToGiveItem)
#define ChooseMonToGiveMailFromMailbox PARTY_MENU_VARIANT_NAME(ChooseMonToGiveMailFromMailbox)
#define InitChooseHalfPartyForBattle PARTY_MENU_VARIANT_NAME(InitChooseHalfPartyForBattle)
#define ClearSelectedPartyOrder PARTY_MENU_VARIANT_NAME(ClearSelectedPartyOrder)
#define ChooseMonForTradingBoard PARTY_MENU_VARIANT_NAME(ChooseMonForTradingBoard)
#define ChooseMonForMoveTutor PARTY_MENU_VARIANT_NAME(ChooseMonForMoveTutor)
#define ChooseMonForWirelessMinigame PARTY_MENU_VARIANT_NAME(ChooseMonForWirelessMinigame)
#define OpenPartyMenuInBattle PARTY_MENU_VARIANT_NAME(OpenPartyMenuInBattle)
#define ChooseMonForInBattleItem PARTY_MENU_VARIANT_NAME(ChooseMonForInBattleItem)
#define BufferBattlePartyCurrentOrder PARTY_MENU_VARIANT_NAME(BufferBattlePartyCurrentOrder)
#define BufferBattlePartyCurrentOrderBySide PARTY_MENU_VARIANT_NAME(BufferBattlePartyCurrentOrderBySide)
#define SwitchPartyOrderLinkMulti PARTY_MENU_VARIANT_NAME(SwitchPartyOrderLinkMulti)
#define SwitchPartyMonSlots PARTY_MENU_VARIANT_NAME(SwitchPartyMonSlots)
#define GetPartyIdFromBattlePartyId PARTY_MENU_VARIANT_NAME(GetPartyIdFromBattlePartyId)
#define ShowPartyMenuToShowcaseMultiBattleParty PARTY_MENU_VARIANT_NAME(ShowPartyMenuToShowcaseMultiBattleParty)
#define ChooseMonForDaycare PARTY_MENU_VARIANT_NAME(ChooseMonForDaycare)
#define CB2_FadeFromPartyMenu PARTY_MENU_VARIANT_NAME(CB2_FadeFromPartyMenu)
#define ChooseContestMon PARTY_MENU_VARIANT_NAME(ChooseContestMon)
#define ChoosePartyMon PARTY_MENU_VARIANT_NAME(ChoosePartyMon)
#define ChooseMonForMoveRelearner PARTY_MENU_VARIANT_NAME(ChooseMonForMoveRelearner)
#define BattlePyramidChooseMonHeldItems PARTY_MENU_VARIANT_NAME(BattlePyramidChooseMonHeldItems)
#define DoBattlePyramidMonsHaveHeldItem PARTY_MENU_VARIANT_NAME(DoBattlePyramidMonsHaveHeldItem)
#define IsSelectedMonEgg PARTY_MENU_VARIANT_NAME(IsSelectedMonEgg)
#define IsLastMonThatKnowsSurf PARTY_MENU_VARIANT_NAME(IsLastMonThatKnowsSurf)
#define MoveDeleterForgetMove PARTY_MENU_VARIANT_NAME(MoveDeleterForgetMove)
#define BufferMoveDeleterNicknameAndMove PARTY_MENU_VARIANT_NAME(BufferMoveDeleterNicknameAndMove)
#define GetNumMovesSelectedMonHas PARTY_MENU_VARIANT_NAME(GetNumMovesSelectedMonHas)
#define MoveDeleterChooseMoveToForget PARTY_MENU_VARIANT_NAME(MoveDeleterChooseMoveToForget)
#define ItemUseCB_PokeBall PARTY_MENU_VARIANT_NAME(ItemUseCB_PokeBall)
#define ItemUseCB_Mints PARTY_MENU_VARIANT_NAME(ItemUseCB_Mints)
// CanLearnTutorMove is NOT renamed: it is a single shared implementation
// (backed by the private sTutorLearnsets table in
// data/pokemon/tutor_learnsets.h, included only by party_menu.c) called
// directly by other systems (scrcmd.c, pokedex_plus_hgss.c) as well as by
// both party menu variants - see docs/PORT_PLAN_SOULGOLD_FEATURES.md §5.6.
#define GetTMHMMoves PARTY_MENU_VARIANT_NAME(GetTMHMMoves)

#endif // GUARD_PARTY_MENU_VARIANT_H
