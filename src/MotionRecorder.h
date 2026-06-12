// Motion recording & repla

#pragma once

#include <cstdint>

namespace BotLocker
{
    // One recorded server tick
    // weaponDefIndex = m_iItemDefinitionIndex of the active weapon, -1 = none.
#pragma pack(push, 4)
    struct ReplayFrame
    {
        uint64_t buttons;       // services+88 button mask
        float forwardMove;      // -1..+1 (engine scales by maxspeed)
        float sideMove;         // -1..+1
        float upMove;           // -1..+1
        float pitch;            // viewangle x
        float yaw;              // viewangle y
        int32_t weaponDefIndex; // active weapon item-definition index, -1 none
        float velX;             // m_vecAbsVelocity at capture, forced on replay
        float velY;
        float velZ;
    };
#pragma pack(pop)
    static_assert(sizeof(ReplayFrame) == 44, "ReplayFrame must stay 44 bytes");

    namespace MotionRecorder
    {
        constexpr int kMaxSlots = 64;

        // ---- recording ----
        bool StartRecord(int slot); // clears old buffer, begins capture
        bool StopRecord(int slot);  // stops; buffer kept for CopyFrames
        bool IsRecording(int slot);
        int RecordedFrameCount(int slot); // <0 on bad slot

        // ProcessUsercmd hook: read the human's cmd and append a frame.
        void OnTickCapture(int slot, void *services, void *cmd);
        // Track which WeaponServices* maps to this recording slot (set per tick).
        void SetLiveWs(int slot, void *ws);
        void *LiveWs(int slot);
        // SelectItem tap: update the slot's current weapon def index.
        void SetCurrentDef(int slot, int defIndex);

        // Copy recorded frames out to caller buffer; returns frames written.
        int CopyFrames(int slot, ReplayFrame *out, int maxFrames);

        // Diagnostic: copy the most recently captured frame. false if none yet.
        bool PeekLastFrame(int slot, ReplayFrame &out);

        // ---- replay ----
        bool LoadReplay(int slot, const ReplayFrame *frames, int count); // copies in
        bool StartReplay(int slot, bool loop);                           // play from frame 0
        bool StopReplay(int slot);                                       // stop + clear injection
        bool IsReplaying(int slot);
        int ReplayCursor(int slot); // current frame index, <0 if idle
        int ReplayTotal(int slot);  // loaded frame count

        // Current frame being applied this tick
        bool CurrentReplayFrame(int slot, ReplayFrame &out);

        // Switch a bot (by player slot) to the weapon with this def index
        bool SwitchBotWeaponByDef(int slot, int defIndex);

        // ProcessUsercmd hook
        bool OnTickApply(int slot, void *services, void *cmd);

        void ClearAll(); // wipe all record + replay buffers (on unload)
    }
}
