// CCSBot Update/Upkeep/Jump detours

#include "BotLocker.h"
#include "BotLockerState.h"
#include "ccsbot_slot.h"
#include "sig_scan.h"
#include "MotionRecorder.h"
#include "version_targets.h"

#include <Windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace tg = cs2bl::targets;

using Update_t = void(__fastcall *)(void *bot);
using Upkeep_t = void(__fastcall *)(void *bot);
using Jump_t = char(__fastcall *)(void *bot, char mustJump);
using UpdateLookAngles_t = void(__fastcall *)(void *bot);

namespace BotLocker
{
    namespace BotLockerHooks
    {
        static Update_t g_origUpdate = nullptr;
        static void *g_addrUpdate = nullptr;
        static Upkeep_t g_origUpkeep = nullptr;
        static void *g_addrUpkeep = nullptr;
        static Jump_t g_origJump = nullptr;
        static void *g_addrJump = nullptr;
        static UpdateLookAngles_t g_origUpdateLookAngles = nullptr;
        static void *g_addrUpdateLookAngles = nullptr;
        static bool g_installed = false;
        static std::string g_status = "not_attempted";

        // Skip the Bot tick under All lock OR while replaying
        static void __fastcall HookedUpdate(void *bot)
        {
            int slot = CCSBotToSlot(bot);
            if (slot >= 0 &&
                (BotLockerState::GetAll(slot) || MotionRecorder::IsReplaying(slot)))
            {
                *(reinterpret_cast<uint8_t *>(bot) + tg::kBot_AiTickedFlag) = 1;
                return;
            }
            g_origUpdate(bot);
        }

        // Skip the per-frame view tick under All or Aim lock.
        // EXCEPTION: while a slot is replaying, drive ONLY the view
        static void __fastcall HookedUpdateLookAngles(void *bot); // fwd decl

        static void __fastcall HookedUpkeep(void *bot)
        {
            int slot = CCSBotToSlot(bot);
            if (slot >= 0 && MotionRecorder::IsReplaying(slot))
            {
                if (g_origUpdateLookAngles)
                    HookedUpdateLookAngles(bot); // view only, no locomotion
                return;
            }
            if (slot >= 0 &&
                (BotLockerState::GetAll(slot) || BotLockerState::GetAim(slot)))
            {
                return;
            }
            g_origUpkeep(bot);
        }

        // Force the bot's view target to the current replay frame
        static void __fastcall HookedUpdateLookAngles(void *bot)
        {
            int slot = CCSBotToSlot(bot);
            ReplayFrame f{};
            if (slot >= 0 && MotionRecorder::CurrentReplayFrame(slot, f))
            {
                auto *b = reinterpret_cast<char *>(bot);
                *reinterpret_cast<float *>(b + tg::kBot_LookPitch) = f.pitch;
                *reinterpret_cast<float *>(b + tg::kBot_LookYaw) = f.yaw;
                *reinterpret_cast<float *>(b + tg::kBot_LookPitchVel) = 0.0f;
                *reinterpret_cast<float *>(b + tg::kBot_LookYawVel) = 0.0f;
            }
            g_origUpdateLookAngles(bot);
        }

        // Skip Jump under Jump lock; return 0 mimics its own gate-fail.
        static char __fastcall HookedJump(void *bot, char mustJump)
        {
            int slot = CCSBotToSlot(bot);
            if (slot >= 0 && BotLockerState::GetJump(slot))
                return 0;
            return g_origJump(bot, mustJump);
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

            g_addrUpdate = ResolveSig(gd, serverModule, "CCSBot::Update",
                                      errorOut, errorOutLen);
            if (!g_addrUpdate)
            {
                g_status = "failed: Update sig";
                return false;
            }

            g_addrUpkeep = ResolveSig(gd, serverModule, "CCSBot::Upkeep",
                                      errorOut, errorOutLen);
            if (!g_addrUpkeep)
            {
                g_status = "failed: Upkeep sig";
                return false;
            }

            // Jump is optional; failure leaves all/aim working, only jump dies.
            char jumpErr[256] = {0};
            g_addrJump = ResolveSig(gd, serverModule, "CCSBot::Jump",
                                    jumpErr, sizeof(jumpErr));
            if (!g_addrJump)
            {
                char dbg[320];
                std::snprintf(dbg, sizeof(dbg),
                              "[BotLocker] WARN: CCSBot::Jump sig not resolved (%s); jump-lock disabled\n",
                              jumpErr);
                OutputDebugStringA(dbg);
            }

            // UpdateLookAngles is optional
            char ulaErr[256] = {0};
            g_addrUpdateLookAngles = ResolveSig(gd, serverModule,
                                                "CCSBot::UpdateLookAngles",
                                                ulaErr, sizeof(ulaErr));
            if (!g_addrUpdateLookAngles)
            {
                char dbg[320];
                std::snprintf(dbg, sizeof(dbg),
                              "[BotLocker] WARN: CCSBot::UpdateLookAngles sig not resolved (%s); replay view-drive disabled\n",
                              ulaErr);
                OutputDebugStringA(dbg);
            }

            // MinHook already initialized by WeaponLockerHooks.
            if (MH_CreateHook(g_addrUpdate,
                              reinterpret_cast<void *>(&HookedUpdate),
                              reinterpret_cast<void **>(&g_origUpdate)) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_CreateHook CCSBot::Update failed");
                g_status = "failed: MH_CreateHook Update";
                return false;
            }
            if (MH_EnableHook(g_addrUpdate) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_EnableHook CCSBot::Update failed");
                MH_RemoveHook(g_addrUpdate);
                g_origUpdate = nullptr;
                g_status = "failed: MH_EnableHook Update";
                return false;
            }

            if (MH_CreateHook(g_addrUpkeep,
                              reinterpret_cast<void *>(&HookedUpkeep),
                              reinterpret_cast<void **>(&g_origUpkeep)) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_CreateHook CCSBot::Upkeep failed");
                MH_DisableHook(g_addrUpdate);
                MH_RemoveHook(g_addrUpdate);
                g_origUpdate = nullptr;
                g_status = "failed: MH_CreateHook Upkeep";
                return false;
            }
            if (MH_EnableHook(g_addrUpkeep) != MH_OK)
            {
                std::snprintf(errorOut, errorOutLen,
                              "MH_EnableHook CCSBot::Upkeep failed");
                MH_RemoveHook(g_addrUpkeep);
                g_origUpkeep = nullptr;
                MH_DisableHook(g_addrUpdate);
                MH_RemoveHook(g_addrUpdate);
                g_origUpdate = nullptr;
                g_status = "failed: MH_EnableHook Upkeep";
                return false;
            }

            if (g_addrJump)
            {
                if (MH_CreateHook(g_addrJump,
                                  reinterpret_cast<void *>(&HookedJump),
                                  reinterpret_cast<void **>(&g_origJump)) != MH_OK)
                {
                    char dbg[160];
                    std::snprintf(dbg, sizeof(dbg),
                                  "[BotLocker] WARN: MH_CreateHook CCSBot::Jump failed; jump-lock disabled\n");
                    OutputDebugStringA(dbg);
                    g_origJump = nullptr;
                    g_addrJump = nullptr;
                }
                else if (MH_EnableHook(g_addrJump) != MH_OK)
                {
                    char dbg[160];
                    std::snprintf(dbg, sizeof(dbg),
                                  "[BotLocker] WARN: MH_EnableHook CCSBot::Jump failed; jump-lock disabled\n");
                    OutputDebugStringA(dbg);
                    MH_RemoveHook(g_addrJump);
                    g_origJump = nullptr;
                    g_addrJump = nullptr;
                }
            }

            // UpdateLookAngles hook: optional, same tolerant pattern as Jump.
            if (g_addrUpdateLookAngles)
            {
                if (MH_CreateHook(g_addrUpdateLookAngles,
                                  reinterpret_cast<void *>(&HookedUpdateLookAngles),
                                  reinterpret_cast<void **>(&g_origUpdateLookAngles)) != MH_OK)
                {
                    OutputDebugStringA("[BotLocker] WARN: MH_CreateHook UpdateLookAngles failed; replay view-drive disabled\n");
                    g_origUpdateLookAngles = nullptr;
                    g_addrUpdateLookAngles = nullptr;
                }
                else if (MH_EnableHook(g_addrUpdateLookAngles) != MH_OK)
                {
                    OutputDebugStringA("[BotLocker] WARN: MH_EnableHook UpdateLookAngles failed; replay view-drive disabled\n");
                    MH_RemoveHook(g_addrUpdateLookAngles);
                    g_origUpdateLookAngles = nullptr;
                    g_addrUpdateLookAngles = nullptr;
                }
            }

            g_installed = true;
            g_status = "ok";

            char dbg[400];
            std::snprintf(dbg, sizeof(dbg),
                          "[BotLocker] CCSBot::Update hooked @ %p, "
                          "CCSBot::Upkeep hooked @ %p, "
                          "CCSBot::Jump hooked @ %p, "
                          "CCSBot::UpdateLookAngles hooked @ %p\n",
                          g_addrUpdate, g_addrUpkeep, g_addrJump,
                          g_addrUpdateLookAngles);
            OutputDebugStringA(dbg);
            return true;
        }

        void Remove()
        {
            if (!g_installed)
                return;
            if (g_addrUpdateLookAngles)
            {
                MH_DisableHook(g_addrUpdateLookAngles);
                MH_RemoveHook(g_addrUpdateLookAngles);
                g_origUpdateLookAngles = nullptr;
            }
            if (g_addrJump)
            {
                MH_DisableHook(g_addrJump);
                MH_RemoveHook(g_addrJump);
                g_origJump = nullptr;
            }
            MH_DisableHook(g_addrUpkeep);
            MH_RemoveHook(g_addrUpkeep);
            g_origUpkeep = nullptr;
            MH_DisableHook(g_addrUpdate);
            MH_RemoveHook(g_addrUpdate);
            g_origUpdate = nullptr;
            g_installed = false;
            g_status = "not_attempted";
        }

        const char *Status() { return g_status.c_str(); }
        void *UpdateAddress() { return g_addrUpdate; }
        void *UpkeepAddress() { return g_addrUpkeep; }
        void *JumpAddress() { return g_addrJump; }
        void *UpdateLookAnglesAddress() { return g_addrUpdateLookAngles; }
    }
}
