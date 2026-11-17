#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/HUD.h"
#include "Misc/Optional.h"
#include "MachineMotion.h"
#include "WorkerGait.h"
#include "MudGameMode.generated.h"

class AMudTerrain;
class AMudMachine;
class UMudAudio;
class UCameraComponent;
class USkeletalMeshComponent;

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
    bool IsCabView() const { return bCab; }
    float FrameSeconds = 0;
    bool bShowMap = false;
    bool bShowHelp = true;

private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> FootAvatar;
    UPROPERTY() TObjectPtr<UMudAudio> Audio;
    UPROPERTY() TObjectPtr<AMudTerrain> Terrain;
    UPROPERTY() TArray<AMudMachine*> Fleet;
    FVector2D OnFoot;
    FVector2D FootVelocity = FVector2D::ZeroVector;
    double FootHeight = 0;
    double FootFacing = 0;
    double FootVerticalSpeed = 0;
    double FootAirTime = 0;
    double FootLanding = 0;
    double FootJumpBuffer = 0;
    double FootDistance = 3.7;
    double FootCameraHeight = 0;
    double FootCameraBoom = 3.7;
    double FootPadForward = 0;
    double FootAvatarGroundOffset = 0;
    double FootAnimationSpeed = 0;
    WorkerGait::Cycle FootGait;
    bool bFootGrounded = true;
    bool bSprint = false;
    int32 Active = 1;
    double LookYaw = -55;
    double LookPitch = -27;
    double CabYaw = 0;
    double CabPitch = -15;
    FIntPoint WindowedSize = FIntPoint::ZeroValue;
    double Distance = 12;
    double Throttle = 0;
    double Steering = 0;
    double HydraulicA = 0;
    double HydraulicB = 0;
    double HydraulicKeyA = 0;
    double HydraulicKeyB = 0;
    MachineMotion::DirectionalTap VerticalTap;
    MachineMotion::DirectionalTap HorizontalTap;
    MachineMotion::HydraulicInput MouseHydraulics;
    double PadHydraulicA = 0;
    double PadHydraulicB = 0;
    bool bPadHydraulics = false;
    bool bPadSecondHydraulics = false;
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

    void Step(double Dt, const FVector2D& Hydraulics, const MachineMotion::HydraulicInput& Mouse);
    void UpdateCamera(float Dt = 0);
    void InitializeFootAvatar();
    void ResetFootMotion(double Facing);
    void StepOnFoot(double Dt);
    void UpdateFootView(double Dt);
    bool CanOccupyFoot(FVector2D Point, double Height, double Radius);
    TOptional<FVector2D> ExitPosition(const AMudMachine& Machine);
    void UpdateBenchmark();
    void SetThrottle(float Value) { Throttle = FMath::Clamp(Value, -1.f, 1.f); }
    void SetSteering(float Value) { Steering = FMath::Clamp(Value, -1.f, 1.f); }
    void SetHydraulicA(float Value) { HydraulicA = Value; }
    void SetHydraulicB(float Value) { HydraulicB = Value; }
    void SetHydraulicKeyA(float Value) { HydraulicKeyA = Value; }
    void SetHydraulicKeyB(float Value) { HydraulicKeyB = Value; }
    void ToolUpTap() { VerticalTap.Press(true); }
    void ToolDownTap() { VerticalTap.Press(false); }
    void ToolRightTap() { HorizontalTap.Press(true); }
    void ToolLeftTap() { HorizontalTap.Press(false); }
    void MoveMouseX(float Value);
    void MoveMouseY(float Value);
    bool ApplyMouseDrag(double X, double Y);
    void MovePadX(float Value);
    void MovePadY(float Value);
    void PanView(double Yaw, double Pitch);
    void Zoom(float Value);
    void EnterExit();
    void SwapVehicle();
    void SwitchCamera();
    void SwitchMap();
    void SwitchHelp();
    void PadHydraulicsOn() { bPadHydraulics = true; }
    void PadHydraulicsOff() { bPadHydraulics = false; }
    void PadSecondHydraulicsOn();
    void PadSecondHydraulicsOff() { bPadSecondHydraulics = false; }
    bool IsPadHydraulicsActive() const;
    void BrakeOn();
    void BrakeOff() { bBrake = false; }
    void SprintOn() { bSprint = true; }
    void SprintOff() { bSprint = false; }
    void SetFootPadForward(float Value) { FootPadForward = Value; }
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
    void ApplyQualityMode(bool bUseDesktop);
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
