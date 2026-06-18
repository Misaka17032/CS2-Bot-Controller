// Read a bot's BotProfile by slot. CCSBot* -> +kBot_Profile -> members.

#include "BotProfile.h"
#include "BotController.h"
#include "ccsbot_slot.h"
#include "version_targets.h"

#include <cstring>

namespace tg = BotController::targets;

namespace BotController
{
    namespace BotProfile
    {
        // Read profile members off a live bot for this slot
        bool ReadProfile(int slot, BotProfileData &out)
        {
            void *bot = BotControllerHooks::BotForSlot(slot);
            if (!bot)
                return false;
            // Guard against a stale pointer: it must still resolve to this slot
            if (CCSBotToSlot(bot) != slot)
                return false;

            void *prof = *reinterpret_cast<void **>(
                reinterpret_cast<char *>(bot) + tg::kBot_Profile);
            if (!prof)
                return false;
            char *p = reinterpret_cast<char *>(prof);

            std::memset(&out, 0, sizeof(out));
            out.aggression   = *reinterpret_cast<float *>(p + tg::kProf_Aggression);
            out.skill        = *reinterpret_cast<float *>(p + tg::kProf_Skill);
            out.teamwork     = *reinterpret_cast<float *>(p + tg::kProf_Teamwork);
            out.reactionTime = *reinterpret_cast<float *>(p + tg::kProf_ReactionTime);
            out.attackDelay  = *reinterpret_cast<float *>(p + tg::kProf_AttackDelay);
            out.lookAccelAtk = *reinterpret_cast<float *>(p + tg::kProf_LookAccelAtk);
            out.lookStiffAtk = *reinterpret_cast<float *>(p + tg::kProf_LookStiffAtk);
            out.lookDampAtk  = *reinterpret_cast<float *>(p + tg::kProf_LookDampAtk);
            out.cost         = *reinterpret_cast<int32_t *>(p + tg::kProf_Cost);
            out.difficulty   = *reinterpret_cast<uint8_t *>(p + tg::kProf_Difficulty);

            int count = *reinterpret_cast<int32_t *>(p + tg::kProf_WeaponPrefCount);
            if (count < 0)
                count = 0;
            if (count > 16)
                count = 16;
            out.weaponPrefCount = count;
            for (int i = 0; i < count; ++i)
                out.weaponPref[i] = *reinterpret_cast<uint16_t *>(
                    p + tg::kProf_WeaponPref + i * 2);
            return true;
        }
    }
}
