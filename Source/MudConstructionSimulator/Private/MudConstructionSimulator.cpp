#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS && !WITH_EDITOR
#include "Windows/WindowsHWrapper.h"
#endif

class FMudConstructionSimulatorModule : public FDefaultGameModuleImpl
{
public:
#if PLATFORM_WINDOWS && !WITH_EDITOR
    void StartupModule() override
    {
        GameMutex = ::CreateMutexW(nullptr, false, L"Local\\MudConstructionSimulator.Game");
    }

    void ShutdownModule() override
    {
        if (GameMutex)
            ::CloseHandle(GameMutex);
    }

private:
    HANDLE GameMutex = nullptr;
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMudConstructionSimulatorModule, MudConstructionSimulator, "MudConstructionSimulator");
