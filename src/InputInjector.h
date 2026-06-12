// MinHook for CCSPlayer_MovementServices::ProcessUsercmd.
// Lets the host plugin override per-slot UserCmd input (buttons/move/view),
// e.g. to drive a bot from external code such as demo replay.

#pragma once

#include <cstdint>
#include <string>

namespace BotLocker
{
    // Injected input applied for one slot until ClearInput.
    struct InjectedInput
    {
        uint64_t buttons;  // m_nButtonDownMaskPrev bitmask
        float forwardMove; // -1..+1, engine scales by maxspeed
        float sideMove;    // -1..+1
        float upMove;      // -1..+1
        float pitch;       // viewangle x
        float yaw;         // viewangle y
    };

    namespace InputInjector
    {
        // Max bots we track per-slot input for.
        static constexpr int kMaxSlots = 64;

        // Resolve sig and install the ProcessUsercmd hook.
        bool Install(const std::string &gamedataPath,
                     void *serverIface,
                     char *errorOut, size_t errorOutLen);

        // Disable + remove the hook; also wipes all per-slot inputs.
        void Remove();

        const char *Status();

        // Resolved address of the hooked function.
        void *ProcessUsercmdAddress();

        // Set the input applied each tick for this slot. Persists until ClearInput.
        // Returns false if slot is out of range.
        bool SetInput(int slot, const InjectedInput &input);

        // Stop injecting for this slot. Engine resumes its own input.
        bool ClearInput(int slot);

        // Stop injecting for every slot.
        void ClearAll();

        // bl_status queries.
        bool IsActive(int slot);
        int CountActive();

        // Diagnostics
        uint64_t HookCallCount();
        int LastResolvedSlot();

        // Per-slot pawn-state snapshot captured each ProcessUsercmd tick
        struct PawnSnapshot
        {
            bool valid;
            float velX, velY, velZ;           // m_vecAbsVelocity
            uint32_t flags;                   // m_fFlags (bit0 = FL_ONGROUND)
            float cmdForward, cmdSide, cmdUp; // cmd move fields this tick
            float cmdPitch, cmdYaw;           // cmd view fields this tick
        };
        bool GetPawnSnapshot(int slot, PawnSnapshot &out);
    }
}
