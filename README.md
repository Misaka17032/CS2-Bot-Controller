# CS2-Bot-Controller

**Make Bot Mimic Again**

## Your stars⭐ are my motivation to keep updating

CS2-Bot-Controller is a Metamod:Source plugin for Counter-Strike 2 that can lock Bot's Weapon/Aim/Jump/All
It can be installed on win64 clients.

- **Weapon** — pin a bot to one weapon slot; AI switches are blocked.
- **Aim** — freeze `CCSBot::Upkeep`; view holds still, AI keeps deciding/moving.
- **Jump** — block `CCSBot::Jump`; bot stops jumping, move/fire/aim unaffected.
- **All** — freeze both `CCSBot::Update` and `CCSBot::Upkeep`.

------------------------------------------------------------------------

## Slots

| Target  | Engine | Weapon                  |
| ------- | ------ | ----------------------- |
| `Slot1` | 0      | Primary                 |
| `Slot2` | 1      | Pistol                  |
| `Slot3` | 2      | Knife / Zeus            |
| `Slot4` | 3      | Grenades                |
| `Slot5` | 4      | C4                      |

------------------------------------------------------------------------

## Install

- `BotController.dll` → `csgo/addons/BotController/bin/win64/`
- `gamedata.json` → `csgo/addons/BotController/`
- `BotController.vdf`  → `csgo/addons/metamod/`

------------------------------------------------------------------------

## Build

Env: `HL2SDKCS2`, `MMSOURCE_DEV`, `CSGO_PROTO`, `protoc` on PATH.

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

------------------------------------------------------------------------

## Commands

```
bl_lock <all|aim|jump|weapon> <slot> [slot1..slot5]
bl_unlock <all|aim|jump|weapon> <slot>
bl_unlock_all <all|aim|jump|weapon>
bl_status
```

`weapon` mode requires the weapon slot as the third argument.

```
bl_lock aim 1                # freeze bot 1's view, AI still runs
bl_lock jump 1               # bot 1 can no longer jump
bl_lock all 1                # full freeze
bl_lock weapon 1 slot3       # force bot 1 to knife
bl_unlock_all weapon         # clear every weapon lock
```

------------------------------------------------------------------------

## CounterStrikeSharp API

Drop `scripts/BotController.NativeApi.cs` into your project.

```csharp
using BotControllerApi;

if (!BotController.IsCompatible()) return;   // requires ABI 5

BotController.Lock(slot, LockKind.Aim);
BotController.Lock(slot, LockKind.Jump);
BotController.Lock(slot, LockKind.All);
BotController.Lock(slot, LockTarget.Slot3);  // weapon lock
BotController.Unlock(slot, LockKind.Aim);
BotController.UnlockAll(LockKind.Weapon);
BotController.IsLocked(slot, LockKind.Aim);
BotController.GetWeaponLock(slot);
```

Main thread only.

------------------------------------------------------------------------

## License

GPL-v3.0

------------------------------------------------------------------------

## Author

**XBribo**
