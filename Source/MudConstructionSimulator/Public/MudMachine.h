#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/Optional.h"
#include "Earthwork.h"
#include "MachineMotion.h"
#include "RunningGear.h"
#include "DozerRig.h"
#include "MudMachine.generated.h"

class AMudTerrain;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UMeshComponent;
class UBoxComponent;
enum class EMudMachine : uint8 { Dozer, Excavator, Dumper, Truck, Loader };

struct FMudMachineSound
{
    float Drive = 0;
    float Travel = 0;
    float Hydraulic = 0;
    float EarthFlow = 0;
    float ImpactLevel = 0;
    Earthwork::Material Material = Earthwork::Material::Soil;
    uint32 Impact = 0;
    bool bTreeImpact = false;
};

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
        const MachineMotion::HydraulicInput& Direct = {});
    void ToggleTool();
    void ChangeGroup();
    void ToggleUnload();
    void Halt();
    void StopWork();
    double GetArticulation() const { return Articulation; }
    double BladeYaw = 0;
    double ArticulationLimit() const;
    void RestoreArticulation(double Angle);
    void UpdatePose(const AMudTerrain& Terrain);
    FVector ToolPoint() const;
    // Swept collision of an airborne material packet against the moving bed.
    bool InterceptMaterial(FVector From, FVector& To, FVector& Velocity, Earthwork::Load& Material, bool bReceive = true);
    FVector BedLoadPoint() const;
    FTransform CabView() const;
    USceneComponent* GetDriverSeat() const { return DriverSeat; }
    bool BlocksPedestrian(FVector2D Point, double Height, double Radius) const;
    void UpdateCabInstruments();
    FString ToolDescription() const;
    bool IsMovingEarth() const { return MaterialMoved > 0.02; }
    FMudMachineSound GetSoundState() const;
    void RecordEarthSound(double Rate, Earthwork::Material Material);
    bool IsTipping() const { return bUnload || Bed.Angle > 0 || bGravitySpilling ||
        (Kind == EMudMachine::Loader && Curl <= -32 && Cargo.Volume() > Earthwork::Epsilon); }
    const TCHAR* Name() const;
    double Capacity() const;
    double Grip = 1;
    double Water = 0;
    double Speed = 0;
    double Heading = 0;
    double Boom = 15;
    double Stick = -28;
    double Curl = 0;
    double BucketTilt = 0;
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
    friend class AMudWorldValidation;
    friend class AMudMaterialReview;
    struct FWheelRig
    {
        USceneComponent* Hub;
        USceneComponent* Knuckle;
        double LateralM;
        double RadiusM;
        double Angle = 0;
        bool bOnRearFrame = false;
        double ContactHalfWidthM = 0.28;
        double InnerTyreOffsetM = 0;
        TOptional<FVector2D> LastImprint;
        RunningGear::SoilContact Soil;
    };

    struct FTrackRig
    {
        UInstancedStaticMeshComponent* Shoes = nullptr;
        UInstancedStaticMeshComponent* Grousers = nullptr;
        TArray<FTransform> Transforms;
        TArray<FTransform> GrouserTransforms;
        double LateralM = 0;
        double Phase = 0;
        RunningGear::SoilContact Soil;
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
    void BuildCargoSurface(USceneComponent* Parent, FVector PositionCm, FVector SizeM);
    void UpdateCargoGrains(const UMeshComponent& Surface, double Fraction);
    void UpdateBladeHeap(const AMudTerrain& Terrain, double Fraction);
    void BuildBucket();
    void BuildLoaderBucket(double HalfWidth);
    void BuildLoaderBody(const TCHAR* Paint);
    void BuildExcavatorBody(const TCHAR* Paint);
    void BuildDozerBody(const TCHAR* Paint);
    void BuildHaulerBody(const TCHAR* Paint);
    void BuildTruckBody(const TCHAR* Paint);
    void BuildTruckBed();
    void BuildCab(USceneComponent* Parent, const TCHAR* Paint, FVector Center, FVector SizeCm, double RakeCm);
    void BuildCabControls(USceneComponent* Cab, FVector SizeCm);
    void BuildBlade(const TCHAR* Paint);
    void BevelBody(USceneComponent* Parent, const TCHAR* Paint, TConstArrayView<FVector2D> Profile,
        double CenterY, double HalfWidth, double Bevel, bool bBeam = false);
    void BuildTracks();
    void BuildTyres();
    void BuildTyreShell(USceneComponent* Hub, double WidthScale);
    void BuildWheelRim(USceneComponent* Hub, double Side, double WidthScale, const TCHAR* Paint);
    void BuildBucketEdge(double HalfToothSpan, FVector Lip);
    void UpdateTrack(FTrackRig& Track, const AMudTerrain* Terrain = nullptr);
    void AnimateRunningGear(double Distance, double YawRadians, double FrontSteering, double RearDistance = 0, double RearYaw = 0);
    double HingeOffsetCm() const;
    FVector2D RearFramePoint(FVector2D Center, double FrontHeading, double RearAngle, double RearOffsetM) const;
    void AddHydraulic(USceneComponent* Base, FVector BasePin, USceneComponent* End, FVector RodPin,
        double LengthCm, double DiameterM);
    void UpdateBucketLinkage();
    void UpdateLoaderLinkage();
    FVector BladePosition(double Lift) const;
    static double BladeWingOffset(double AcrossCm);
    FVector BucketPoint(double BoomAngle, double StickAngle, double CurlAngle) const;
    double FindDigAngle(const AMudTerrain& Terrain, double TargetHeight) const;
    static const FVector BoomMount, StickMount, BucketMount, StickRamPin, BucketRamBase;
    static const FVector DozerPushPivot, DozerBladePin;
    static constexpr double DozerPushLength = DozerRig::PushLength;
    static constexpr double DozerPushHalfWidth = DozerRig::PushHalfWidth;
    static constexpr int32 BladeHeapColumns = 33;
    static constexpr int32 BladeHeapRows = 21;
    static constexpr int32 BladeClodCount = 10;
    static constexpr double TruckFrontAxleCm = 260;
    static constexpr double TruckFirstRearAxleCm = -155;
    static constexpr double TruckSecondRearAxleCm = -292;
    static constexpr double TruckRearSupportCm = (TruckFirstRearAxleCm + TruckSecondRearAxleCm) * 0.5;
    static constexpr double TruckWheelbaseM = (TruckFrontAxleCm - TruckRearSupportCm) / 100;
    static constexpr double TruckTyreRadiusM = 0.56;
    static constexpr double TruckBedFrontCm = 492;
    static constexpr double DumperFrontAxleCm = 186;
    static constexpr double DumperFirstRearAxleCm = -69;
    static constexpr double DumperSecondRearAxleCm = -230;
    static constexpr double DumperBogieCm = (DumperFirstRearAxleCm + DumperSecondRearAxleCm) * 0.5;
    static constexpr double DumperHingeCm = 65;
    static constexpr double DumperBedHingeCm = -397;
    static const FVector TruckBedHinge;
    static constexpr double LoaderAxleCm = 165;
    static constexpr double LoaderTyreRadiusM = 0.78;
    UPROPERTY() TObjectPtr<USceneComponent> Turret;
    UPROPERTY() TObjectPtr<USceneComponent> BoomPivot;
    UPROPERTY() TObjectPtr<USceneComponent> StickPivot;
    UPROPERTY() TObjectPtr<USceneComponent> Tool;
    UPROPERTY() TObjectPtr<USceneComponent> BucketLinkJoint;
    UPROPERTY() TObjectPtr<USceneComponent> BucketTiltPivot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BucketRocker;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BucketLink;
    UPROPERTY() TObjectPtr<USceneComponent> LoaderRocker;
    UPROPERTY() TObjectPtr<USceneComponent> DozerPushArms;
    UPROPERTY() TObjectPtr<USceneComponent> DozerYawCarrier;
    UPROPERTY() TObjectPtr<USceneComponent> TipBed;
    UPROPERTY() TObjectPtr<USceneComponent> TruckTailgate;
    UPROPERTY() TObjectPtr<USceneComponent> RearFrame;
    UPROPERTY() TObjectPtr<UBoxComponent> EnvironmentCollider;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Payload;
    UPROPERTY() TObjectPtr<USceneComponent> CabEye;
    USceneComponent* DriverSeat = nullptr;
    UPROPERTY() TObjectPtr<USceneComponent> CabWheel;
    UPROPERTY() TObjectPtr<USceneComponent> CabLeverA;
    UPROPERTY() TObjectPtr<USceneComponent> CabLeverB;
    UPROPERTY() TObjectPtr<UTextRenderComponent> CabDisplay;
    FString CabReadout;
    TArray<FWheelRig> Wheels;
    TArray<FTrackRig> Tracks;
    TArray<FHydraulicRig> Hydraulics;
    Earthwork::Material CargoVisual = Earthwork::Material::Soil;
    UStaticMeshComponent* CargoSurface = nullptr;
    UInstancedStaticMeshComponent* CargoGrains[Earthwork::MaterialCount]{};
    Earthwork::Load RenderedCargo;
    double DriveCommand = 0;
    double SteeringCommand = 0;
    double Articulation = 0;
    MachineMotion::TipControl Bed;
    double BladeLift = 0.67;
    TOptional<double> BladeDatum;
    UInstancedStaticMeshComponent* BladeClods = nullptr;
    TOptional<FVector> BladeHeapAnchor;
    double BladeHeapHeading = 0;
    double BladeHeapFraction = 0;
    double BladeFlowTravel = 0;
    double RenderedBladeTravel = 0;
    double ValveA = 0;
    double ValveB = 0;
    double ValveCurl = 0;
    double ValveTilt = 0;
    double ValveSwing = 0;
    double MaterialMoved = 0;
    double LastWorkVolume = 0;
    double DraftForceKN = 0;
    TOptional<FTransform> LastBladePose;
    TOptional<FVector> LastToolContact;
    float SoundTravel = 0;
    float SoundHydraulic = 0;
    float SoundSteering = 0;
    float SoundEarthFlow = 0;
    float SoundImpactLevel = 0;
    uint32 SoundImpact = 0;
    bool bSoundTreeImpact = false;
    bool bGravitySpilling = false;
    Earthwork::Material SoundMaterial = Earthwork::Material::Soil;
    double TrackTravel = 0;
    double TrackScrub = 0;
    bool bBogged = false;
    RunningGear::SoilDrivetrain Drivetrain;
    enum class EDigPhase : uint8 { Idle, Approach, Scoop, Lift };
    EDigPhase DigPhase = EDigPhase::Idle;
    double DigTime = 0;
    double DigSurface = 0;
    bool bDigMissed = false;
    bool bBucketObstructed = false;
    bool bBucketGround = false;
    bool IsBladeWorking() const { return Kind == EMudMachine::Dozer && (bTool || bBucketGround); }
    double BladeClearance(const AMudTerrain& Terrain, double Lift) const;
    double SampleBladeDatum(const AMudTerrain& Terrain) const;
    double OffRoadRadius() const;
    double EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
};
