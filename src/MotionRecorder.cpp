// Motion recording & replay implementation. See MotionRecorder.h.
//
// Recording: ProcessUsercmd hook calls OnTickCapture for the human's slot
//
// Replay: ProcessUsercmd hook calls OnTickApply for the bot's slot

#include "MotionRecorder.h"
#include "InputInjector.h"
#include "WeaponLocker.h"
#include "version_targets.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace tg = cs2bl::targets;

namespace BotLocker
{
    namespace MotionRecorder
    {
        struct RecordState
        {
            std::atomic<bool> recording{false};
            std::vector<ReplayFrame> frames;
            std::atomic<void *> liveWs{nullptr};
            std::atomic<int> currentDef{-1};
            std::mutex mu; // guards frames
        };

        struct ReplayState
        {
            std::atomic<bool> playing{false};
            std::atomic<bool> loop{false};
            std::vector<ReplayFrame> frames;
            std::atomic<int> cursor{0};
            std::atomic<int> lastAppliedDef{-1};
            std::mutex mu; // guards frames
        };

        static std::array<RecordState, kMaxSlots> g_rec;
        static std::array<ReplayState, kMaxSlots> g_rep;

        static bool ValidSlot(int s) { return s >= 0 && s < kMaxSlots; }

        // ---- recording ----

        bool StartRecord(int slot)
        {
            if (!ValidSlot(slot))
                return false;
            RecordState &r = g_rec[slot];
            {
                std::lock_guard<std::mutex> lk(r.mu);
                r.frames.clear();
                r.frames.reserve(4096); // ~64s @ 64 tick
            }
            r.currentDef.store(-1, std::memory_order_relaxed);
            r.liveWs.store(nullptr, std::memory_order_relaxed);
            r.recording.store(true, std::memory_order_release);
            return true;
        }

        bool StopRecord(int slot)
        {
            if (!ValidSlot(slot))
                return false;
            g_rec[slot].recording.store(false, std::memory_order_release);
            return true;
        }

        bool IsRecording(int slot)
        {
            return ValidSlot(slot) &&
                   g_rec[slot].recording.load(std::memory_order_acquire);
        }

        int RecordedFrameCount(int slot)
        {
            if (!ValidSlot(slot))
                return -1;
            RecordState &r = g_rec[slot];
            std::lock_guard<std::mutex> lk(r.mu);
            return static_cast<int>(r.frames.size());
        }

        void SetLiveWs(int slot, void *ws)
        {
            if (ValidSlot(slot))
                g_rec[slot].liveWs.store(ws, std::memory_order_relaxed);
        }

        void *LiveWs(int slot)
        {
            return ValidSlot(slot)
                       ? g_rec[slot].liveWs.load(std::memory_order_relaxed)
                       : nullptr;
        }

        void SetCurrentDef(int slot, int defIndex)
        {
            if (ValidSlot(slot))
                g_rec[slot].currentDef.store(defIndex, std::memory_order_relaxed);
        }

        void OnTickCapture(int slot, void *services, void *cmd)
        {
            if (!ValidSlot(slot) || !services || !cmd)
                return;
            RecordState &r = g_rec[slot];
            if (!r.recording.load(std::memory_order_acquire))
                return;

            auto *s = reinterpret_cast<char *>(services);
            auto *c = reinterpret_cast<char *>(cmd);

            ReplayFrame f{};
            f.buttons = *reinterpret_cast<uint64_t *>(s + tg::kServices_Buttons);
            f.forwardMove = *reinterpret_cast<float *>(c + tg::kCmd_ForwardMove);
            f.sideMove = *reinterpret_cast<float *>(c + tg::kCmd_SideMove);
            f.upMove = *reinterpret_cast<float *>(c + tg::kCmd_UpMove);
            f.pitch = *reinterpret_cast<float *>(c + tg::kCmd_ViewPitch);
            f.yaw = *reinterpret_cast<float *>(c + tg::kCmd_ViewYaw);

            // Read the active weapon every tick
            void *ws = r.liveWs.load(std::memory_order_relaxed);
            int def = WeaponLockerHooks::ActiveWeaponDef(ws);
            if (def < 0)
                def = r.currentDef.load(std::memory_order_relaxed); // fallback
            f.weaponDefIndex = def;

            // Capture the player's actual velocity this tick
            void *pawn = *reinterpret_cast<void **>(s + tg::kServices_Pawn);
            if (pawn)
            {
                auto *pv = reinterpret_cast<char *>(pawn);
                f.velX = *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 0);
                f.velY = *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 4);
                f.velZ = *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 8);
            }

            std::lock_guard<std::mutex> lk(r.mu);
            r.frames.push_back(f);
        }

        int CopyFrames(int slot, ReplayFrame *out, int maxFrames)
        {
            if (!ValidSlot(slot) || !out || maxFrames <= 0)
                return 0;
            RecordState &r = g_rec[slot];
            std::lock_guard<std::mutex> lk(r.mu);
            int n = static_cast<int>(r.frames.size());
            if (n > maxFrames)
                n = maxFrames;
            for (int i = 0; i < n; ++i)
                out[i] = r.frames[i];
            return n;
        }

        bool PeekLastFrame(int slot, ReplayFrame &out)
        {
            if (!ValidSlot(slot))
                return false;
            RecordState &r = g_rec[slot];
            std::lock_guard<std::mutex> lk(r.mu);
            if (r.frames.empty())
                return false;
            out = r.frames.back();
            return true;
        }

        // ---- replay ----

        bool LoadReplay(int slot, const ReplayFrame *frames, int count)
        {
            if (!ValidSlot(slot) || !frames || count < 0)
                return false;
            ReplayState &p = g_rep[slot];
            // Refuse to swap frames out from under an active playback.
            if (p.playing.load(std::memory_order_acquire))
                return false;
            std::lock_guard<std::mutex> lk(p.mu);
            p.frames.assign(frames, frames + count);
            p.cursor.store(0, std::memory_order_relaxed);
            p.lastAppliedDef.store(-1, std::memory_order_relaxed);
            return true;
        }

        bool StartReplay(int slot, bool loop)
        {
            if (!ValidSlot(slot))
                return false;
            ReplayState &p = g_rep[slot];
            {
                std::lock_guard<std::mutex> lk(p.mu);
                if (p.frames.empty())
                    return false;
            }
            p.cursor.store(0, std::memory_order_relaxed);
            p.lastAppliedDef.store(-1, std::memory_order_relaxed);
            p.loop.store(loop, std::memory_order_relaxed);
            p.playing.store(true, std::memory_order_release);
            // View is driven via the UpdateLookAngles hook
            // Do NOT Aim-lock
            return true;
        }

        bool StopReplay(int slot)
        {
            if (!ValidSlot(slot))
                return false;
            g_rep[slot].playing.store(false, std::memory_order_release);
            // Release the engine-input override
            InputInjector::ClearInput(slot);
            return true;
        }

        bool IsReplaying(int slot)
        {
            return ValidSlot(slot) &&
                   g_rep[slot].playing.load(std::memory_order_acquire);
        }

        int ReplayCursor(int slot)
        {
            if (!ValidSlot(slot))
                return -1;
            ReplayState &p = g_rep[slot];
            if (!p.playing.load(std::memory_order_acquire))
                return -1;
            return p.cursor.load(std::memory_order_relaxed);
        }

        int ReplayTotal(int slot)
        {
            if (!ValidSlot(slot))
                return 0;
            ReplayState &p = g_rep[slot];
            std::lock_guard<std::mutex> lk(p.mu);
            return static_cast<int>(p.frames.size());
        }

        bool CurrentReplayFrame(int slot, ReplayFrame &out)
        {
            if (!ValidSlot(slot))
                return false;
            ReplayState &p = g_rep[slot];
            if (!p.playing.load(std::memory_order_acquire))
                return false;
            std::lock_guard<std::mutex> lk(p.mu);
            int total = static_cast<int>(p.frames.size());
            // cursor points at the NEXT frame; the one just applied is cursor-1.
            int idx = p.cursor.load(std::memory_order_relaxed) - 1;
            if (idx < 0)
                idx = 0;
            if (idx >= total)
                return false;
            out = p.frames[idx];
            return true;
        }

        bool SwitchBotWeaponByDef(int slot, int defIndex)
        {
            if (!ValidSlot(slot) || defIndex < 0)
                return false;
            if (!WeaponLockerHooks::WeaponHooksReady())
                return false;
            void *ws = WeaponLockerHooks::WsForSlot(slot);
            if (!ws)
                return false; // bot hasn't ticked yet
            void *weapon = WeaponLockerHooks::FindWeaponByDef(ws, defIndex);
            if (!weapon)
                return false; // bot doesn't carry it
            return WeaponLockerHooks::SelectWeaponRaw(ws, weapon);
        }

        bool OnTickApply(int slot, void *services, void *cmd)
        {
            if (!ValidSlot(slot) || !services || !cmd)
                return false;
            ReplayState &p = g_rep[slot];
            if (!p.playing.load(std::memory_order_acquire))
                return false;

            ReplayFrame f{};
            {
                std::lock_guard<std::mutex> lk(p.mu);
                int total = static_cast<int>(p.frames.size());
                int cur = p.cursor.load(std::memory_order_relaxed);
                if (cur >= total)
                {
                    if (p.loop.load(std::memory_order_relaxed) && total > 0)
                    {
                        cur = 0;
                        p.lastAppliedDef.store(-1, std::memory_order_relaxed);
                    }
                    else
                    {
                        // Done: stop and release the engine-input override.
                        p.playing.store(false, std::memory_order_release);
                        InputInjector::ClearInput(slot);
                        return false;
                    }
                }
                f = p.frames[cur];
                p.cursor.store(cur + 1, std::memory_order_relaxed);
            }

            auto *s = reinterpret_cast<char *>(services);
            auto *c = reinterpret_cast<char *>(cmd);

            // Force the recorded velocity onto the bot every tick
            {
                void *pawn = *reinterpret_cast<void **>(s + tg::kServices_Pawn);
                if (pawn)
                {
                    auto *pv = reinterpret_cast<char *>(pawn);
                    *reinterpret_cast<float *>(pv + tg::kEnt_Velocity + 0) = f.velX;
                    *reinterpret_cast<float *>(pv + tg::kEnt_Velocity + 4) = f.velY;
                    *reinterpret_cast<float *>(pv + tg::kEnt_Velocity + 8) = f.velZ;
                    *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 0) = f.velX;
                    *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 4) = f.velY;
                    *reinterpret_cast<float *>(pv + tg::kEnt_AbsVelocity + 8) = f.velZ;
                }
            }

            // Movement + its frame of reference
            *reinterpret_cast<uint64_t *>(s + tg::kServices_Buttons) = f.buttons;
            *reinterpret_cast<float *>(c + tg::kCmd_ForwardMove) = f.forwardMove;
            *reinterpret_cast<float *>(c + tg::kCmd_SideMove) = f.sideMove;
            *reinterpret_cast<float *>(c + tg::kCmd_UpMove) = f.upMove;
            // cmd view angles set the movement frame of reference
            *reinterpret_cast<float *>(c + tg::kCmd_ViewPitch) = f.pitch;
            *reinterpret_cast<float *>(c + tg::kCmd_ViewYaw) = f.yaw;
            *reinterpret_cast<float *>(c + tg::kCmd_ViewRoll) = 0.0f;
            *reinterpret_cast<float *>(c + tg::kCmd_ViewPitchInput) = f.pitch;
            *reinterpret_cast<float *>(c + tg::kCmd_ViewYawInput) = f.yaw;
            *reinterpret_cast<float *>(c + tg::kCmd_ViewRollInput) = 0.0f;
            return true;
        }

        void ClearAll()
        {
            for (int i = 0; i < kMaxSlots; ++i)
            {
                g_rec[i].recording.store(false, std::memory_order_release);
                g_rep[i].playing.store(false, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lk(g_rec[i].mu);
                    g_rec[i].frames.clear();
                }
                {
                    std::lock_guard<std::mutex> lk(g_rep[i].mu);
                    g_rep[i].frames.clear();
                }
                g_rec[i].currentDef.store(-1, std::memory_order_relaxed);
                g_rec[i].liveWs.store(nullptr, std::memory_order_relaxed);
                g_rep[i].cursor.store(0, std::memory_order_relaxed);
                g_rep[i].lastAppliedDef.store(-1, std::memory_order_relaxed);
            }
        }
    }
}
