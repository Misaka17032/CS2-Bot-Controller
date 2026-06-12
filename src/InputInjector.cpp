// ProcessUsercmd hook

#include "InputInjector.h"
#include "ccsbot_slot.h"
#include "sig_scan.h"
#include "MotionRecorder.h"
#include "version_targets.h"

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace tg = cs2bl::targets;

using ProcessUsercmd_t = void(__fastcall *)(void *services, void *cmd);

namespace BotLocker
{
    namespace InputInjector
    {
        struct SlotState
        {
            std::atomic<bool> active{false};
            InjectedInput input{};
        };

        static std::array<SlotState, kMaxSlots> g_slots;
        static ProcessUsercmd_t g_origProcessUsercmd = nullptr;

        // Per-slot diagnostic snapshot, written at the end of each hook tick.
        static std::array<PawnSnapshot, kMaxSlots> g_snap{};
        static void *g_addrProcessUsercmd = nullptr;
        static bool g_installed = false;
        static std::string g_status = "not_attempted";

        // Diagnostics: total hook fires and the last slot we resolved.
        static std::atomic<uint64_t> g_hookCalls{0};
        static std::atomic<int> g_lastSlot{-1};

        // services -> player slot via pawn ptr at services+56, then m_hController.
        static int ServicesToSlot(void *services)
        {
            if (!services)
                return -1;
            void *pawn = *reinterpret_cast<void **>(
                reinterpret_cast<char *>(services) + tg::kServices_Pawn);
            if (!pawn)
                return -1;
            return ControllerSlotForPawn(pawn);
        }

        // services -> pawn -> WeaponServices*, for the recording weapon tap.
        static void *ServicesToWeaponServices(void *services)
        {
            if (!services)
                return nullptr;
            void *pawn = *reinterpret_cast<void **>(
                reinterpret_cast<char *>(services) + tg::kServices_Pawn);
            if (!pawn)
                return nullptr;
            return *reinterpret_cast<void **>(
                reinterpret_cast<char *>(pawn) + tg::kPawn_WeaponServices);
        }

        // Overwrite buttons/move/view fields.
        static void __fastcall HookedProcessUsercmd(void *services, void *cmd)
        {
            g_hookCalls.fetch_add(1, std::memory_order_relaxed);
            int slot = ServicesToSlot(services);
            g_lastSlot.store(slot, std::memory_order_relaxed);

            // 1) Capture: if this slot is recording, read the human's real input
            //    BEFORE any inject/replay overwrites it. Refresh the slot's ws so
            //    the SelectItem tap can attribute weapon switches to this slot.
            if (slot >= 0 && slot < kMaxSlots && MotionRecorder::IsRecording(slot))
            {
                MotionRecorder::SetLiveWs(slot, ServicesToWeaponServices(services));
                MotionRecorder::OnTickCapture(slot, services, cmd);
            }

            // 2) Existing manual inject (bl_inject / BotLocker_InjectUserCmd).
            if (slot >= 0 && slot < kMaxSlots && g_slots[slot].active.load(std::memory_order_acquire))
            {
                const InjectedInput &p = g_slots[slot].input;
                auto *s = reinterpret_cast<char *>(services);

                // buttons live on MovementServices, not on cmd (CS2 specific).
                *reinterpret_cast<uint64_t *>(s + tg::kServices_Buttons) = p.buttons;

                if (cmd)
                {
                    auto *c = reinterpret_cast<char *>(cmd);
                    *reinterpret_cast<float *>(c + tg::kCmd_ForwardMove) = p.forwardMove;
                    *reinterpret_cast<float *>(c + tg::kCmd_SideMove) = p.sideMove;
                    *reinterpret_cast<float *>(c + tg::kCmd_UpMove) = p.upMove;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewPitch) = p.pitch;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewYaw) = p.yaw;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewRoll) = 0.0f;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewPitchInput) = p.pitch;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewYawInput) = p.yaw;
                    *reinterpret_cast<float *>(c + tg::kCmd_ViewRollInput) = 0.0f;
                }
            }

            // 3) Replay apply: overwrites everything above (highest priority).
            if (slot >= 0 && slot < kMaxSlots)
                MotionRecorder::OnTickApply(slot, services, cmd);

            // Diagnostic snapshot: capture the move/view actually fed to the
            // engine this tick (post-override), then run the original and read
            // back the resulting velocity/flags from the pawn.
            if (slot >= 0 && slot < kMaxSlots && cmd)
            {
                auto *c = reinterpret_cast<char *>(cmd);
                PawnSnapshot &sn = g_snap[slot];
                sn.cmdForward = *reinterpret_cast<float *>(c + tg::kCmd_ForwardMove);
                sn.cmdSide = *reinterpret_cast<float *>(c + tg::kCmd_SideMove);
                sn.cmdUp = *reinterpret_cast<float *>(c + tg::kCmd_UpMove);
                sn.cmdPitch = *reinterpret_cast<float *>(c + tg::kCmd_ViewPitch);
                sn.cmdYaw = *reinterpret_cast<float *>(c + tg::kCmd_ViewYaw);
            }

            g_origProcessUsercmd(services, cmd);

            // Post-tick: read the pawn's resulting velocity + ground flags.
            if (slot >= 0 && slot < kMaxSlots)
            {
                void *pawn = *reinterpret_cast<void **>(
                    reinterpret_cast<char *>(services) + tg::kServices_Pawn);
                if (pawn)
                {
                    auto *p = reinterpret_cast<char *>(pawn);
                    PawnSnapshot &sn = g_snap[slot];
                    sn.velX = *reinterpret_cast<float *>(p + tg::kEnt_AbsVelocity + 0);
                    sn.velY = *reinterpret_cast<float *>(p + tg::kEnt_AbsVelocity + 4);
                    sn.velZ = *reinterpret_cast<float *>(p + tg::kEnt_AbsVelocity + 8);
                    sn.flags = *reinterpret_cast<uint32_t *>(p + tg::kEnt_Flags);
                    sn.valid = true;
                }
            }
        }

        // Resolve a sig from gamedata against the loaded server.dll.
        static void *ResolveSig(const std::string &gd, HMODULE serverModule,
                                const char *name,
                                char *errorOut, size_t errorOutLen)
        {
            std::string sig = Sig::FindWindowsSig(gd, name);
            if (sig.empty())
            {
                std::snprintf(errorOut, errorOutLen,
                              "gamedata missing '%s.signatures.windows'", name);
                return nullptr;
            }
            std::vector<uint8_t> bytes;
            std::vector<bool> wild;
            if (!Sig::ParseSigString(sig, bytes, wild))
            {
                std::snprintf(errorOut, errorOutLen,
                              "failed to parse '%s' sig: '%s'", name, sig.c_str());
                return nullptr;
            }
            void *addr = Sig::FindPatternIn(serverModule, bytes, wild);
            if (!addr)
            {
                std::snprintf(errorOut, errorOutLen,
                              "sig '%s' not found in server.dll", name);
                return nullptr;
            }
            return addr;
        }

        bool Install(const std::string &gamedataPath,
                     void *serverIface,
                     char *errorOut, size_t errorOutLen)
        {
            HMODULE serverModule = Sig::ModuleFromInterfacePtr(serverIface);
            if (!serverModule)
            {
                std::snprintf(errorOut, errorOutLen,
                              "ModuleFromInterfacePtr returned null");
                g_status = "failed: no server module";
                return false;
            }

            std::string gd = Sig::ReadFile(gamedataPath);
            if (gd.empty())
            {
                std::snprintf(errorOut, errorOutLen,
                              "failed to read gamedata: %s", gamedataPath.c_str());
                g_status = "failed: gamedata missing";
                return false;
            }

            g_addrProcessUsercmd = ResolveSig(
                gd, serverModule,
                "CCSPlayer_MovementServices::ProcessUsercmd",
                errorOut, errorOutLen);
            if (!g_addrProcessUsercmd)
            {
                g_status = "failed: ProcessUsercmd sig";
                return false;
            }

            if (MH_CreateHook(g_addrProcessUsercmd,
                              reinterpret_cast<void *>(&HookedProcessUsercmd),
                              reinterpret_cast<void **>(&g_origProcessUsercmd)) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_CreateHook ProcessUsercmd failed");
                g_status = "failed: MH_CreateHook";
                return false;
            }
            if (MH_EnableHook(g_addrProcessUsercmd) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_EnableHook ProcessUsercmd failed");
                MH_RemoveHook(g_addrProcessUsercmd);
                g_origProcessUsercmd = nullptr;
                g_status = "failed: MH_EnableHook";
                return false;
            }

            g_installed = true;
            g_status = "ok";

            char dbg[160];
            std::snprintf(dbg, sizeof(dbg),
                          "[BotLocker] ProcessUsercmd hooked @ %p\n",
                          g_addrProcessUsercmd);
            OutputDebugStringA(dbg);
            return true;
        }

        void Remove()
        {
            if (!g_installed)
                return;
            MH_DisableHook(g_addrProcessUsercmd);
            MH_RemoveHook(g_addrProcessUsercmd);
            g_origProcessUsercmd = nullptr;
            g_addrProcessUsercmd = nullptr;
            g_installed = false;
            g_status = "not_attempted";
            ClearAll();
        }

        const char *Status() { return g_status.c_str(); }

        void *ProcessUsercmdAddress() { return g_addrProcessUsercmd; }

        bool SetInput(int slot, const InjectedInput &input)
        {
            if (slot < 0 || slot >= kMaxSlots)
                return false;
            g_slots[slot].input = input;
            g_slots[slot].active.store(true, std::memory_order_release);
            return true;
        }

        bool ClearInput(int slot)
        {
            if (slot < 0 || slot >= kMaxSlots)
                return false;
            g_slots[slot].active.store(false, std::memory_order_release);
            return true;
        }

        void ClearAll()
        {
            for (auto &s : g_slots)
                s.active.store(false, std::memory_order_release);
        }

        bool IsActive(int slot)
        {
            if (slot < 0 || slot >= kMaxSlots)
                return false;
            return g_slots[slot].active.load(std::memory_order_acquire);
        }

        int CountActive()
        {
            int n = 0;
            for (auto &s : g_slots)
                if (s.active.load(std::memory_order_acquire))
                    ++n;
            return n;
        }

        uint64_t HookCallCount()
        {
            return g_hookCalls.load(std::memory_order_relaxed);
        }

        int LastResolvedSlot()
        {
            return g_lastSlot.load(std::memory_order_relaxed);
        }

        // Copy out the last captured pawn snapshot for a slot.
        bool GetPawnSnapshot(int slot, PawnSnapshot &out)
        {
            if (slot < 0 || slot >= kMaxSlots)
                return false;
            out = g_snap[slot];
            return out.valid;
        }
    }
}
