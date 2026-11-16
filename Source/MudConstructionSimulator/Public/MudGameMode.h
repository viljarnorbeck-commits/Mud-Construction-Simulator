#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "MudGameMode.generated.h"

class AMudTerrain;
class AMudMachine;
class UCameraComponent;

UCLASS()
class AMudGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMudGameMode();
};

UCLASS()
class AMudOperator : public APawn
{
    GENERATED_BODY()
public:
    AMudOperator();
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void SetupPlayerInputComponent(UInputComponent* Input) override;
    AMudTerrain* GetTerrain() const { return Terrain; }
    AMudMachine* GetMachine() const;
    const TArray<AMudMachine*>& GetFleet() const { return Fleet; }
    FVector2D GetPosition() const;
    FString MissionText() const;
    FString SurveyText() const;
    FString StatusText() const;
    float FrameSeconds = 0;
    bool bShowMap = false;
    bool bShowHelp = true;

private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<AMudTerrain> Terrain;
    UPROPERTY() TArray<AMudMachine*> Fleet;
    FVector2D OnFoot;
    int32 Active = 0;
    double LookYaw = -35;
    double LookPitch = -21;
    double Distance = 12;
    double StepBank = 0;
    double Throttle = 0;
    double Steering = 0;
    double HydraulicA = 0;
    double HydraulicB = 0;
    double MouseHydraulicA = 0;
    double MouseHydraulicB = 0;
    bool bMouseHydraulics = false;
    bool bPadHydraulics = false;
    bool bCab = false;
    bool bBrake = false;
    bool bDelivery = false;
    bool bFinished = false;
    bool bCruise = false;
    double Cruise = 0.5;
    bool bDepotLoad = false;
    double MessageTime = 0;
    FString Message;
    bool bInitializeFailed = false;
    double BenchmarkStart = 0;
    double BenchmarkLast = 0;
    TArray<double> BenchmarkFrames;

    void Step(double Dt);
    void UpdateCamera();
    void UpdateBenchmark();
    void SetThrottle(float Value) { Throttle = FMath::Clamp(Value, -1.f, 1.f); }
    void SetSteering(float Value) { Steering = FMath::Clamp(Value, -1.f, 1.f); }
    void SetHydraulicA(float Value) { HydraulicA = Value; }
    void SetHydraulicB(float Value) { HydraulicB = Value; }
    void MoveMouseX(float Value);
    void MoveMouseY(float Value);
    void MovePadX(float Value);
    void MovePadY(float Value);
    void Zoom(float Value);
    void EnterExit();
    void SwapVehicle();
    void SwitchCamera() { bCab = !bCab; }
    void SwitchMap() { bShowMap = !bShowMap; }
    void SwitchHelp() { bShowHelp = !bShowHelp; }
    void MouseHydraulicsOn() { bMouseHydraulics = true; }
    void MouseHydraulicsOff() { bMouseHydraulics = false; }
    void PadHydraulicsOn() { bPadHydraulics = true; }
    void PadHydraulicsOff() { bPadHydraulics = false; }
    void BrakeOn() { bBrake = true; bCruise = false; }
    void BrakeOff() { bBrake = false; }
    void CruiseToggle() { bCruise = !bCruise; }
    void CruiseUp() { Cruise = FMath::Min(1.0, Cruise + 0.1); }
    void CruiseDown() { Cruise = FMath::Max(0.1, Cruise - 0.1); }
    void WorkTool();
    void ChangeGroup();
    void Unload();
    void Recover();
    void Service();
    void Save();
    void Load();
    void PerformanceMode();
    void QualityMode();
    void ToggleFullscreen();
    void Quit();
    void Notify(const FString& Text);
};

UCLASS()
class AMudHUD : public AHUD
{
    GENERATED_BODY()
public:
    void DrawHUD() override;
};
