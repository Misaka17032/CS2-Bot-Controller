// C-ABI exports for CounterStrikeSharp P/Invoke. quiet=true on all entries.

#include "dispatch.h"
#include "InputInjector.h"
#include "MotionRecorder.h"

#include <cstdint>
#include <vector>

extern "C" __declspec(dllexport)
int BotLocker_Lock(int slot, int kind, int arg)
{
    return BotLocker::Dispatch::Lock(slot,
        static_cast<BotLocker::LockKind>(kind), arg, /*quiet=*/true);
}

extern "C" __declspec(dllexport)
int BotLocker_Unlock(int slot, int kind)
{
    return BotLocker::Dispatch::Unlock(slot,
        static_cast<BotLocker::LockKind>(kind), /*quiet=*/true);
}

extern "C" __declspec(dllexport)
int BotLocker_UnlockAll(int kind)
{
    return BotLocker::Dispatch::UnlockAll(
        static_cast<BotLocker::LockKind>(kind), /*quiet=*/true);
}

extern "C" __declspec(dllexport)
int BotLocker_IsLocked(int slot, int kind)
{
    return BotLocker::Dispatch::IsLocked(slot,
        static_cast<BotLocker::LockKind>(kind));
}

// Set per-slot injected input. Engine pmove runs with these values until cleared.
extern "C" __declspec(dllexport)
int BotLocker_InjectUserCmd(int      slot,
                            uint64_t buttons,
                            float    forwardMove,
                            float    sideMove,
                            float    upMove,
                            float    pitch,
                            float    yaw)
{
    BotLocker::InjectedInput in{buttons, forwardMove, sideMove, upMove, pitch, yaw};
    return BotLocker::InputInjector::SetInput(slot, in) ? 0 : -1;
}

// Stop injecting for one slot. Engine resumes its own UserCmd.
extern "C" __declspec(dllexport)
int BotLocker_ClearInjection(int slot)
{
    return BotLocker::InputInjector::ClearInput(slot) ? 0 : -1;
}

// Stop injecting for every slot at once.
extern "C" __declspec(dllexport)
int BotLocker_ClearAllInjections()
{
    BotLocker::InputInjector::ClearAll();
    return 0;
}

extern "C" __declspec(dllexport)
int BotLocker_GetVersion()
{
    return 8;
}

// ---- Motion recording & replay (ABI 8: ReplayFrame gained velX/Y/Z) ----

// Begin/stop recording a human slot's per-tick input. 0 ok / -1 fail.
extern "C" __declspec(dllexport)
int BotLocker_StartRecord(int slot)
{
    return BotLocker::MotionRecorder::StartRecord(slot) ? 0 : -1;
}

extern "C" __declspec(dllexport)
int BotLocker_StopRecord(int slot)
{
    return BotLocker::MotionRecorder::StopRecord(slot) ? 0 : -1;
}

// Number of frames currently recorded for a slot. <0 on bad slot.
extern "C" __declspec(dllexport)
int BotLocker_GetRecordedFrameCount(int slot)
{
    return BotLocker::MotionRecorder::RecordedFrameCount(slot);
}

// Copy recorded frames into caller buffer (C# passes a ReplayFrame[] pointer
// and its capacity). Returns frames written.
extern "C" __declspec(dllexport)
int BotLocker_CopyRecordedFrames(int slot, BotLocker::ReplayFrame *out, int maxFrames)
{
    return BotLocker::MotionRecorder::CopyFrames(slot, out, maxFrames);
}

// Load a frame array into a slot's replay buffer (internally copied). 0 ok.
extern "C" __declspec(dllexport)
int BotLocker_LoadReplay(int slot, const BotLocker::ReplayFrame *frames, int count)
{
    return BotLocker::MotionRecorder::LoadReplay(slot, frames, count) ? 0 : -1;
}

// Move a slot's just-recorded buffer into another slot's replay buffer
// without going through the managed side (console-only verification path).
extern "C" __declspec(dllexport)
int BotLocker_TransferRecordingToReplay(int srcSlot, int dstSlot)
{
    int n = BotLocker::MotionRecorder::RecordedFrameCount(srcSlot);
    if (n <= 0) return -1;
    std::vector<BotLocker::ReplayFrame> tmp(n);
    int got = BotLocker::MotionRecorder::CopyFrames(srcSlot, tmp.data(), n);
    if (got <= 0) return -1;
    return BotLocker::MotionRecorder::LoadReplay(dstSlot, tmp.data(), got) ? 0 : -1;
}

extern "C" __declspec(dllexport)
int BotLocker_StartReplay(int slot, int loop)
{
    return BotLocker::MotionRecorder::StartReplay(slot, loop != 0) ? 0 : -1;
}

extern "C" __declspec(dllexport)
int BotLocker_StopReplay(int slot)
{
    return BotLocker::MotionRecorder::StopReplay(slot) ? 0 : -1;
}

// Current replay frame index, or <0 if the slot is not replaying.
extern "C" __declspec(dllexport)
int BotLocker_GetReplayCursor(int slot)
{
    return BotLocker::MotionRecorder::ReplayCursor(slot);
}

// Total frames loaded in a slot's replay buffer.
extern "C" __declspec(dllexport)
int BotLocker_GetReplayTotal(int slot)
{
    return BotLocker::MotionRecorder::ReplayTotal(slot);
}

// Copy the frame currently being replayed (for C# to drive view/fire/weapon).
// Returns 0 on success, -1 if the slot isn't replaying.
extern "C" __declspec(dllexport)
int BotLocker_GetReplayFrame(int slot, BotLocker::ReplayFrame *out)
{
    if (!out) return -1;
    return BotLocker::MotionRecorder::CurrentReplayFrame(slot, *out) ? 0 : -1;
}

// Switch a bot to the weapon with this def index (kKnifeDef=9001 = its knife).
// Returns 0 ok / -1 not found or bot not ready.
extern "C" __declspec(dllexport)
int BotLocker_SwitchBotWeapon(int slot, int defIndex)
{
    return BotLocker::MotionRecorder::SwitchBotWeaponByDef(slot, defIndex) ? 0 : -1;
}
