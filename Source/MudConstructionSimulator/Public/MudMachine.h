#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Earthwork.h"
#include "MachineMotion.h"
#include "MudMachine.generated.h"

class AMudTerrain;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;

enum class EMudMachine : uint8 { Dozer, Excavator, Dumper, Truck, Loader };

UCLASS()
class AMudMachine : public AActor
{
    GENERATED_BODY()

public:
    AMudMachine();
    void Initialize(EMudMachine Kind, FVector2D Position, AMudTerrain& Terrain);
    void Drive(double Dt, double Throttle, double Steering, bool bBrake, AMudTerrain& Terrain,
        const TArray<AMudMachine*>& Fleet);
    void Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet,
        const MachineMotion::HydraulicInput& Mouse = {});
    void ToggleTool();
    void ChangeGroup();
    void ToggleUnload();
    void Halt();
    void StopWork();
    void UpdatePose(const AMudTerrain& Terrain);
    FVector ToolPoint() const;
    FTransform CabView() const;
    void UpdateCabInstruments();
    FString ToolDescription() const;
    bool IsMovingEarth() const { return MaterialMoved > 0.02; }
    bool IsTipping() const { return bUnload || Bed.Angle > 0 || (Kind == EMudMachine::Loader && Curl <= -32 && Cargo.Volume() > Earthwork::Epsilon); }
    const TCHAR* Name() const;
    double Capacity() const;
    double Grip = 1;
    double Water = 0;
    double Speed = 0;
    double Heading = 0;
    double Boom = 15;
    double Stick = -28;
    double Curl = 0;
    double Swing = 0;
    double Blade = 0;
    FVector2D Position;
    Earthwork::Load Cargo;
    EMudMachine Kind = EMudMachine::Dozer;
    bool bTool = false;
    bool bGrade = false;
    bool bUnload = false;
    bool bOtherGroup = false;
    int32 RolledSide = 0;

private:
    struct FWheelRig
    {
        USceneComponent* Hub;
        USceneComponent* Knuckle;
        double LateralM;
        double RadiusM;
        double Angle = 0;
    };

    struct FTrackRig
    {
        UInstancedStaticMeshComponent* Shoes = nullptr;
        UInstancedStaticMeshComponent* Grousers = nullptr;
        TArray<FTransform> Transforms;
        TArray<FTransform> GrouserTransforms;
        double LateralM = 0;
        double Phase = 0;
    };

    struct FHydraulicRig
    {
        USceneComponent* Base;
        USceneComponent* End;
        UStaticMeshComponent* Housing;
        UStaticMeshComponent* Rod;
        FVector BasePin;
        FVector RodPin;
        double LengthCm;
        double DiameterM;
    };

    USceneComponent* Pivot(USceneComponent* Parent, FVector Offset);
    UStaticMeshComponent* Part(const TCHAR* Shape, const TCHAR* Paint, USceneComponent* Parent,
        FVector PositionCm, FVector SizeM, FRotator Rotation = FRotator::ZeroRotator);
    UInstancedStaticMeshComponent* RepeatedBoxes(const TCHAR* Paint, USceneComponent* Parent,
        const TArray<FTransform>& Transforms);
    UProceduralMeshComponent* BuildHeap(USceneComponent* Parent, FVector PositionCm, FVector SizeM);
    void BuildBucket(double HalfWidth = 68);
    void BuildLoaderBody(const TCHAR* Paint);
    void BuildExcavatorBody(const TCHAR* Paint);
    void BuildDozerBody(const TCHAR* Paint);
    void BuildHaulerBody(const TCHAR* Paint);
    void BuildCab(USceneComponent* Parent, const TCHAR* Paint, FVector Center, FVector SizeCm, double RakeCm);
    void BuildCabControls(USceneComponent* Cab, FVector SizeCm);
    void BuildBlade();
    void BevelBody(USceneComponent* Parent, const TCHAR* Paint, TConstArrayView<FVector2D> Profile,
        double CenterY, double HalfWidth, double Bevel, bool bBeam = false);
    void BuildTracks();
    void BuildTyres();
    void BuildTyreShell(USceneComponent* Hub, double WidthScale);
    void BuildBucketEdge(double HalfWidth, FVector Lip, bool bLoader);
    void UpdateTrack(FTrackRig& Track);
    void AnimateRunningGear(double Distance, double YawRadians, double FrontSteering);
    void AddHydraulic(USceneComponent* Base, FVector BasePin, USceneComponent* End, FVector RodPin,
        double LengthCm, double DiameterM);
    void UpdateBucketLinkage();
    FVector BucketPoint(double BoomAngle, double StickAngle, double CurlAngle) const;
    double FindDigAngle(const AMudTerrain& Terrain, double TargetHeight) const;
    static const FVector BoomMount, StickMount, BucketMount, StickRamPin, BucketRamBase;
    UPROPERTY() TObjectPtr<USceneComponent> Turret;
    UPROPERTY() TObjectPtr<USceneComponent> BoomPivot;
    UPROPERTY() TObjectPtr<USceneComponent> StickPivot;
    UPROPERTY() TObjectPtr<USceneComponent> Tool;
    UPROPERTY() TObjectPtr<USceneComponent> BucketLinkJoint;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BucketRocker;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BucketLink;
    UPROPERTY() TObjectPtr<USceneComponent> TipBed;
    UPROPERTY() TObjectPtr<USceneComponent> RearFrame;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Payload;
    UPROPERTY() TObjectPtr<USceneComponent> CabEye;
    UPROPERTY() TObjectPtr<USceneComponent> CabWheel;
    UPROPERTY() TObjectPtr<USceneComponent> CabLeverA;
    UPROPERTY() TObjectPtr<USceneComponent> CabLeverB;
    UPROPERTY() TObjectPtr<UTextRenderComponent> CabDisplay;
    FString CabReadout;
    TArray<FWheelRig> Wheels;
    TArray<FTrackRig> Tracks;
    TArray<FHydraulicRig> Hydraulics;
    Earthwork::Material CargoVisual = Earthwork::Material::Soil;
    double DriveCommand = 0;
    double SteeringCommand = 0;
    double Articulation = 0;
    MachineMotion::TipControl Bed;
    double BladeLift = 0.67;
    double ValveA = 0;
    double ValveB = 0;
    double ValveCurl = 0;
    double ValveSwing = 0;
    double MaterialMoved = 0;
    double TrackTravel = 0;
    double TrackScrub = 0;
    enum class EDigPhase : uint8 { Idle, Approach, Scoop, Lift };
    EDigPhase DigPhase = EDigPhase::Idle;
    double DigTime = 0;
    double DigSurface = 0;
    bool bDigMissed = false;
    bool bBucketObstructed = false;
    bool bBucketGround = false;
    bool IsBladeWorking() const { return Kind == EMudMachine::Dozer && (bTool || bBucketGround); }
    double BladeClearance(const AMudTerrain& Terrain, double Lift) const;
    double OffRoadRadius() const;
    double EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
};
