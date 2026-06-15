// BotController native Metamod:Source plugin entry point.

#include <ISmmPlugin.h>

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>

#include <eiface.h>
#include <icvar.h>
#include <convar.h>
#include <interfaces/interfaces.h>

#include "WeaponLocker.h"
#include "BotController.h"
#include "InputInjector.h"
#include "MotionRecorder.h"
#include "dispatch.h"
#include "WeaponLockerState.h"
#include "BotControllerState.h"
#include "commands.h"

class BotControllerPlugin : public ISmmPlugin
{
public:
    bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late) override;
    bool Unload(char *error, size_t maxlen) override;

    bool Pause(char * /*error*/, size_t /*maxlen*/) override { return true; }
    bool Unpause(char * /*error*/, size_t /*maxlen*/) override { return true; }
    void AllPluginsLoaded() override {}

    const char *GetAuthor() override { return "XBribo(๑•.•๑)"; }
    const char *GetName() override { return "BotController"; }
    const char *GetDescription() override { return "Lock and Record CS2 bots."; }
    const char *GetURL() override { return ""; }
    const char *GetLicense() override { return "GPLv3"; }
    const char *GetVersion() override { return "0.4.4"; }
    const char *GetDate() override { return __DATE__; }
    const char *GetLogTag() override { return "BL"; }
};

BotControllerPlugin g_BotControllerPlugin;
PLUGIN_EXPOSE(BotControllerPlugin, g_BotControllerPlugin);

static HMODULE GetSelfModule()
{
    HMODULE mod = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetSelfModule),
        &mod);
    return mod;
}

// gamedata.json
static std::string ComputeGamedataPath()
{
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(GetSelfModule(), path, MAX_PATH) == 0)
        return "";

    for (int i = 0; i < 3; ++i)
    {
        char *slash = std::strrchr(path, '\\');
        if (!slash)
            return "";
        *slash = '\0';
    }
    std::string result(path);
    result += "\\gamedata.json";
    return result;
}

bool BotControllerPlugin::Load(PluginId id, ISmmAPI *ismm,
                               char *error, size_t maxlen, bool /*late*/)
{
    PLUGIN_SAVEVARS();

    g_pCVar = static_cast<ICvar *>(
        ismm->GetEngineFactory()(CVAR_INTERFACE_VERSION, nullptr));
    if (!g_pCVar)
    {
        std::snprintf(error, maxlen,
                      "Failed to get ICvar (%s) via engine factory",
                      CVAR_INTERFACE_VERSION);
        return false;
    }
    ConVar_Register(FCVAR_RELEASE | FCVAR_GAMEDLL);

    // IVEngineServer2::ClientCommand
    BotController::Dispatch::g_pEngine = static_cast<IVEngineServer2 *>(
        ismm->GetEngineFactory()(INTERFACEVERSION_VENGINESERVER, nullptr));
    if (!BotController::Dispatch::g_pEngine)
    {
        std::snprintf(error, maxlen,
                      "Failed to get IVEngineServer2 (%s)",
                      INTERFACEVERSION_VENGINESERVER);
        return false;
    }

    // Need ISource2GameClients only as the anchor for sig-scan
    void *serverIface =
        ismm->GetServerFactory()(INTERFACEVERSION_SERVERGAMECLIENTS, nullptr);
    if (!serverIface)
    {
        std::snprintf(error, maxlen,
                      "Failed to get ISource2GameClients (%s)",
                      INTERFACEVERSION_SERVERGAMECLIENTS);
        return false;
    }

    // Engine interface used by console command output (ClientPrintf).
    BotController::Commands::g_pEngine = BotController::Dispatch::g_pEngine;

    std::string gamedataPath = ComputeGamedataPath();
    if (gamedataPath.empty())
    {
        std::snprintf(error, maxlen, "Failed to compute gamedata.json path");
        return false;
    }

    if (!BotController::WeaponLockerHooks::Install(gamedataPath, serverIface,
                                                   error, maxlen))
        return false;

    if (!BotController::BotControllerHooks::Install(gamedataPath, serverIface,
                                                    error, maxlen))
    {
        BotController::WeaponLockerHooks::Remove();
        return false;
    }

    // ProcessUsercmd hook for demo-replay UserCmd injection
    char injErr[256] = {0};
    if (!BotController::InputInjector::Install(gamedataPath, serverIface,
                                               injErr, sizeof(injErr)))
    {
        char dbg[320];
        std::snprintf(dbg, sizeof(dbg),
                      "[BotController] WARN: InputInjector::Install failed (%s); "
                      "BotController_InjectUserCmd will be a no-op\n",
                      injErr);
        OutputDebugStringA(dbg);
    }

    OutputDebugStringA("[BotController] plugin loaded successfully\n");
    return true;
}

bool BotControllerPlugin::Unload(char * /*error*/, size_t /*maxlen*/)
{
    BotController::MotionRecorder::ClearAll();
    BotController::InputInjector::Remove();
    BotController::BotControllerHooks::Remove();
    BotController::WeaponLockerHooks::Remove();
    BotController::WeaponLockerState::ClearAll();
    BotController::BotControllerState::ClearAllAll();
    BotController::BotControllerState::ClearAllAim();
    BotController::BotControllerState::ClearAllJump();
    BotController::Dispatch::g_pEngine = nullptr;
    BotController::Commands::g_pEngine = nullptr;
    ConVar_Unregister();
    g_pCVar = nullptr;
    OutputDebugStringA("[BotController] plugin unloaded\n");
    return true;
}
