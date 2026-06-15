// CSS wrapper for BotController.dll. Always check IsCompatible() before use.
// Main-thread only.

using System.Runtime.InteropServices;

namespace BotControllerApi
{
    // Lock category. Per-slot lock states:
    //   All    - freezes both CCSBot::Update and CCSBot::Upkeep
    //   Aim    - freezes CCSBot::Upkeep only
    //   Weapon - locks the bot's weapon to a specific engine slot
    //   Jump   - blocks CCSBot::Jump only
    public enum LockKind
    {
        All    = 0,
        Aim    = 1,
        Weapon = 2,
        Jump   = 3,
    }

    // Engine weapon slots.
    public enum LockTarget
    {
        None  = 0,
        Slot1 = 1,
        Slot2 = 2,
        Slot3 = 3,
        Slot4 = 4,
        Slot5 = 5,
    }

    public static class BotController
    {
        private const int ExpectedAbiVersion = 5;

        [DllImport("BotController", CallingConvention = CallingConvention.Cdecl)]
        private static extern int BotController_Lock(int slot, int kind, int arg);

        [DllImport("BotController", CallingConvention = CallingConvention.Cdecl)]
        private static extern int BotController_Unlock(int slot, int kind);

        [DllImport("BotController", CallingConvention = CallingConvention.Cdecl)]
        private static extern int BotController_UnlockAll(int kind);

        [DllImport("BotController", CallingConvention = CallingConvention.Cdecl)]
        private static extern int BotController_IsLocked(int slot, int kind);

        [DllImport("BotController", CallingConvention = CallingConvention.Cdecl)]
        private static extern int BotController_GetVersion();

        public static bool IsCompatible() => BotController_GetVersion() == ExpectedAbiVersion;

        // ---- All / Aim / Jump ----
        public static bool Lock(int slot, LockKind kind)
            => BotController_Lock(slot, (int)kind, 0) == 0;

        // ---- Weapon: arg is the engine slot to lock onto ----
        public static bool Lock(int slot, LockTarget target)
            => BotController_Lock(slot, (int)LockKind.Weapon, (int)target) == 0;

        public static bool Unlock(int slot, LockKind kind)
            => BotController_Unlock(slot, (int)kind) == 0;

        public static bool UnlockAll(LockKind kind)
            => BotController_UnlockAll((int)kind) == 0;

        // For All/Aim/Jump returns true if locked; for Weapon use GetWeaponLock.
        public static bool IsLocked(int slot, LockKind kind)
            => BotController_IsLocked(slot, (int)kind) != 0;

        // Weapon-only query: returns the locked weapon slot, or None.
        public static LockTarget GetWeaponLock(int slot)
            => (LockTarget)BotController_IsLocked(slot, (int)LockKind.Weapon);
    }
}
