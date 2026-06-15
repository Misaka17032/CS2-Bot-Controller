// MinHook install/remove for CCSBot Update/Upkeep/Jump.

#pragma once

#include <string>

namespace BotController
{
    namespace BotControllerHooks
    {
        // Resolve sigs and install detours.
        bool Install(const std::string &gamedataPath,
                     void *serverIface,
                     char *errorOut, size_t errorOutLen);

        // Disable + remove detours.
        void Remove();

        const char *Status();

        void *UpdateAddress();
        void *UpkeepAddress();
        void *JumpAddress();
        void *UpdateLookAnglesAddress();
    }
}
