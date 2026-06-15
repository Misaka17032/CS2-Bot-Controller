// version_targets.h

#pragma once

namespace BotController::targets
{
    // ---- CCSBot ----

    // AI-ran-this-tick byte flag; set to 1 to fake a completed tick
    inline constexpr int kBot_AiTickedFlag = 21196;
    // CCSBot -> pawn (CCSPlayerPawn*)
    inline constexpr int kBot_Pawn = 0x18;

    // ---- CBaseEntity / CEntityIdentity ----

    // entity -> CEntityIdentity*
    inline constexpr int kEnt_Identity = 0x10;
    // CEntityIdentity -> m_EHandle (low 15 bits = entity index)
    inline constexpr int kEntIdentity_EHandle = 0x10;
    // m_MoveType (MoveType_t, 1 byte) — restored each replay tick for §8
    inline constexpr int kEnt_MoveType = 0x2F3;
    // m_nActualMoveType (MoveType_t, 1 byte) — networked move type (ladder anim)
    inline constexpr int kEnt_ActualMoveType = 0x2F5;
    // m_fFlags (bit0 = FL_ONGROUND, bit1 = FL_DUCKING)
    inline constexpr int kEnt_Flags = 0x388;
    // m_fFlags bit masks restored on replay
    inline constexpr unsigned kFL_OnGround = 1u << 0;
    inline constexpr unsigned kFL_Ducking = 1u << 1;
    // m_vecAbsVelocity
    inline constexpr int kEnt_AbsVelocity = 0x38C;
    // entity -> m_pGameSceneNode -> m_vecAbsOrigin (world pos), written each
    // replay tick (direct write, not Teleport, to keep client interp smooth)
    inline constexpr int kEnt_GameSceneNode = 0x270;
    inline constexpr int kNode_AbsOrigin = 0xC8;

    // ---- CCSPlayerPawn ----

    // m_pWeaponServices
    inline constexpr int kPawn_WeaponServices = 0xA00;
    // m_hController (CHandle)
    inline constexpr int kPawn_Controller = 0xB80;
    // m_hOriginalController (CHandle)
    inline constexpr int kPawn_OriginalController = 0xB84;
    // CCSPlayerPawn -> v_angle (QAngle)
    inline constexpr int kPawn_ViewAngle = 0xAB8;
    // m_angEyeAngles (QAngle) — written each replay tick alongside v_angle
    inline constexpr int kPawn_EyeAngles = 0x1340;

    // ---- CCSPlayer_WeaponServices ----

    // m_hActiveWeapon (CHandle)
    inline constexpr int kWs_ActiveWeapon = 0x60;

    // ---- CBasePlayerWeapon ----

    // m_AttributeManager(0x958) -> m_Item(0x50) -> m_iItemDefinitionIndex(0x38),
    // all embedded; net direct add (no deref)
    inline constexpr int kWeapon_ItemDefIndex = 0x958 + 0x50 + 0x38; // 0x9E0

    // ---- CCSPlayer_MovementServices ----

    // m_pawn (CCSPlayerPawn*)
    inline constexpr int kServices_Pawn = 56;
    // m_nButtons.m_pButtonStates[0..2] — engine button state block (CInButtonState)
    inline constexpr int kServices_Buttons = 88;       // states[0] (pressed)
    inline constexpr int kServices_Buttons1 = 88 + 8;  // states[1]
    inline constexpr int kServices_Buttons2 = 88 + 16; // states[2]

    // duck/ladder state
    inline constexpr int kServices_LadderNormal = 0x3F8; // Vector m_vecLadderNormal
    inline constexpr int kServices_Ducked = 0x408;       // bool m_bDucked
    inline constexpr int kServices_DuckAmount = 0x40C;   // float m_flDuckAmount
    inline constexpr int kServices_DuckSpeed = 0x410;    // float m_flDuckSpeed
    inline constexpr int kServices_DesiresDuck = 0x415;  // bool m_bDesiresDuck
    inline constexpr int kServices_Ducking = 0x416;      // bool m_bDucking

    // ---- CMoveData  ----

    // m_vecVelocity — the velocity TryPlayerMove integrates into origin
    inline constexpr int kMove_Velocity = 56;
    // m_vecAbsOrigin — post-move origin written here before FinishMove commits
    inline constexpr int kMove_AbsOrigin = 200;

    // ---- CUserCmd ----

    inline constexpr int kCmd_ForwardMove = 44;
    inline constexpr int kCmd_SideMove = 48;
    inline constexpr int kCmd_UpMove = 52;

} // namespace cs2bl::targets
