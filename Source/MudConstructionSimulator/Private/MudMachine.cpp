#include "MudMachine.h"
#include "LoaderRig.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "MachineMotion.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"
#include "RunningGear.h"
#include "TerrainPassage.h"
#include "TrackGround.h"

namespace
{
using namespace Earthwork;
struct FSpec
{
    const TCHAR* Name;
    const TCHAR* Paint;
    double Capacity, Speed, Mass, Turn, Clearance, Footprint;
};

constexpr double DumperSteeringDegrees = 24.0;

constexpr FSpec Specs[] = {
    { TEXT("Cat D6"), TEXT("Amber"), 6, 4.2, 19, 68, 1.30, 1.45 },
    { TEXT("Volvo EC300E"), TEXT("Amber"), 3.2, 3.6, 24, 48, 1.30, 1.45 },
    { TEXT("Volvo A30G"), TEXT("Amber"), 16, 10.5, 17, DumperSteeringDegrees, 1.06, 1.48 },
    { TEXT("Volvo FMX 6x6"), TEXT("Teal"), 24, 13.5, 15, 29, 1.05, 1.46 },
    { TEXT("Volvo L120H"), TEXT("Amber"), 3.4, 8.0, 20, 36, 0.80, 1.48 }
};

const FSpec& Spec(EMudMachine Kind) { return Specs[static_cast<uint8>(Kind)]; }

constexpr const TCHAR* CargoPaint[] = { TEXT("Loose"), TEXT("LooseGravel"), TEXT("LooseRock"), TEXT("LooseSand"), TEXT("LooseClay") };
constexpr const TCHAR* SurfacePaint[] = { TEXT("PayloadSoil"), TEXT("PayloadGravel"), TEXT("PayloadRock"), TEXT("PayloadSand"), TEXT("PayloadClay") };

double CargoHeapHeight(double X, double Y)
{
    const double Profile = FMath::Max(0.0, 1.0 - FMath::Pow(FMath::Abs(X * 2), 2.8) - FMath::Pow(FMath::Abs(Y * 2), 2.2));
    // Match the surface exported by unreal_foliage.cargo_surface().
    const double Shape = 0.70 + 0.055 * FMath::Sin(X * 19 + Y * 12) + 0.035 * FMath::Sin(X * 31 - Y * 22);
    return FMath::Pow(Profile, 0.87) * 100.0 * Shape;
}

const FVector BucketLip(-123, 0, -64);
constexpr double BucketHalfWidthCm = 88;
constexpr double BucketOuterToothCm = BucketHalfWidthCm - 10;
constexpr double BucketFloorCm = -68;
constexpr double BucketRearCm = 24;
constexpr double BucketCrownCm = 16;
const FVector ExcavatorPayloadSize(0.99, 1.53, 0.89);
const FVector StickRockerPin(195, 0, 14);
const FVector BucketLinkPin(26, 0, 17);
constexpr double RockerLength = 57;
constexpr double BucketLinkLength = 57;
const FVector BladeLip(12, 0, -26.5);
const FVector BladeMount(289, 0, 0);
constexpr double DumpCurl = -93;
constexpr RunningGear::TrackPath BeltPath{1.70, 0.47};
constexpr double BeltHeightM = 0.52;
constexpr double DozerTrackHalfContact = 1.65;
constexpr double DozerTrackRadius = 0.405;
constexpr double DozerTrackAxleHeight = 0.455;
constexpr double DozerSprocketX = -0.83;
constexpr double DozerSprocketRise = 0.84;
const RunningGear::ElevatedTrackPath DozerBeltPath{DozerTrackHalfContact, DozerSprocketX, DozerSprocketRise, DozerTrackRadius};
constexpr double TyreRadiusM = 0.656;

UMaterialInterface* PaintMaterial(const TCHAR* Name)
{
    auto* Paint = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Generated/%s.%s"), Name, Name));
    if (!Paint && (FCString::Strstr(Name, TEXT("Sand")) || FCString::Strstr(Name, TEXT("Clay"))))
        Paint = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Generated/Loose.Loose"));
    return Paint;
}

FVector2D Horizontal(const FVector& Value) { return FVector2D(Value.X, Value.Y); }
FVector LoaderPin(LoaderRig::Point Point) { return FVector(Point.X, 0, Point.Z); }
}

const FVector AMudMachine::BoomMount(15, 38, 84);
const FVector AMudMachine::StickMount(344, 0, 0);
const FVector AMudMachine::BucketMount(225, 0, 0);
const FVector AMudMachine::StickRamPin(-38, 0, 59);
const FVector AMudMachine::BucketRamBase(49, 0, 58);
const FVector AMudMachine::DozerPushPivot(-115, 0, 51);
const FVector AMudMachine::DozerBladePin(DozerRig::Pin.X, DozerRig::Pin.Y, DozerRig::Pin.Z);
const FVector AMudMachine::TruckBedHinge(-389, 0, 140);

AMudMachine::AMudMachine()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MachineRoot"));
    EnvironmentCollider=CreateDefaultSubobject<UBoxComponent>(TEXT("EnvironmentContact"));
    EnvironmentCollider->SetupAttachment(RootComponent);
    EnvironmentCollider->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    EnvironmentCollider->SetCollisionObjectType(ECC_WorldDynamic);
    EnvironmentCollider->SetCollisionResponseToAllChannels(ECR_Ignore);
    EnvironmentCollider->SetCollisionResponseToChannel(ECC_PhysicsBody,ECR_Block);
}

USceneComponent* AMudMachine::Pivot(USceneComponent* Parent, FVector Offset)
{
    auto* Joint = NewObject<USceneComponent>(this);
    Joint->SetupAttachment(Parent);
    Joint->SetRelativeLocation(Offset);
    Joint->RegisterComponent();
    return Joint;
}

UStaticMeshComponent* AMudMachine::Part(const TCHAR* Shape, const TCHAR* Paint, USceneComponent* Parent,
    FVector PositionCm, FVector SizeM, FRotator Rotation)
{
    auto* Mesh = NewObject<UStaticMeshComponent>(this);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
    Mesh->SetMaterial(0, PaintMaterial(Paint));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetupAttachment(Parent);
    Mesh->SetRelativeLocation(PositionCm);
    Mesh->SetRelativeScale3D(SizeM);
    Mesh->SetRelativeRotation(Rotation);
    Mesh->RegisterComponent();
    return Mesh;
}

UInstancedStaticMeshComponent* AMudMachine::RepeatedBoxes(const TCHAR* Paint, USceneComponent* Parent,
    const TArray<FTransform>& Transforms)
{
    auto* Detail = NewObject<UInstancedStaticMeshComponent>(this);
    Detail->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Detail->SetMaterial(0, PaintMaterial(Paint));
    Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Detail->SetupAttachment(Parent);
    Detail->RegisterComponent();
    Detail->AddInstances(Transforms, false, false, false);
    return Detail;
}

void AMudMachine::BuildTyres()
{
    auto* Root = GetRootComponent();
    const double HaulerAxles[] = {DumperSecondRearAxleCm, DumperFirstRearAxleCm, DumperFrontAxleCm};
    const double LoaderAxles[] = {-LoaderAxleCm, LoaderAxleCm};
    const double TruckAxles[] = {TruckSecondRearAxleCm, TruckFirstRearAxleCm, TruckFrontAxleCm};
    const bool bLoader = Kind == EMudMachine::Loader;
    const bool bTruck = Kind == EMudMachine::Truck;
    const double RadiusM = bTruck ? TruckTyreRadiusM : (bLoader ? LoaderTyreRadiusM : TyreRadiusM);
    const double RadiusScale = RadiusM / TyreRadiusM;
    const TConstArrayView<double> Axles = bTruck ? MakeArrayView(TruckAxles) :
        (bLoader ? MakeArrayView(LoaderAxles) : MakeArrayView(HaulerAxles));
    for (const double AxleX : Axles)
    {
        const bool bDual = bTruck && AxleX < 0;
        const double WidthScale = bTruck ? (bDual ? 0.62 : 0.80) : (bLoader ? 1.06 / RadiusScale : 1.0);
        const double OffsetM = bTruck ? (bDual ? 1.125 : 1.045) : (bLoader ? 1.035 : 1.22);
        for (const double Side : {-1.0, 1.0})
        {
            auto* Axle = RearFrame && AxleX < 0 ? RearFrame.Get() : Root;
            auto* Mount = Pivot(Axle, FVector(AxleX, Side * OffsetM * 100, RadiusM * 100));
            auto* Knuckle = Kind == EMudMachine::Truck && AxleX > 0 ? Mount : nullptr;
            auto* Hub = Knuckle ? Pivot(Knuckle, FVector::ZeroVector) : Mount;
            Hub->SetRelativeScale3D(FVector(RadiusScale));
            Wheels.Add({Hub, Knuckle, Side * (OffsetM - (bDual ? 0.16 : 0.0)), RadiusM, 0,
                RearFrame && AxleX < 0, 0.26 * WidthScale * RadiusScale, bDual ? -Side * 0.32 : 0.0});
            BuildTyreShell(Hub, WidthScale);
            const double InnerTyreOffset = bDual ? -Side * 32.0 / RadiusScale : 0.0;
            if (bDual)
            {
                auto* InnerHub = Pivot(Hub, {0, InnerTyreOffset, 0});
                BuildTyreShell(InnerHub, WidthScale);
                Part(TEXT("Cylinder"), TEXT("Chassis"), InnerHub, {}, {0.68, 0.68, 0.46 * WidthScale}, FRotator(0, 0, 90));
            }
            Part(TEXT("Cylinder"), TEXT("Chassis"), Hub, {}, {0.68, 0.68, 0.51 * WidthScale}, FRotator(0, 0, 90));
            BuildWheelRim(Hub, Side, WidthScale, Kind == EMudMachine::Dumper || bLoader ? TEXT("Amber") : TEXT("Steel"));
            Part(TEXT("Cylinder"), TEXT("Steel"), Hub, {0, Side * 34 * WidthScale, 0}, {0.27, 0.27, 0.085}, FRotator(0, 0, 90));

            TArray<FTransform> Lugs;
            const int32 LugCount = bTruck ? 25 : 21;
            for (const double Row : {-1.0, 1.0})
            {
                for (int32 I = 0; I < LugCount; ++I)
                {
                    const double Degrees = I * 360.0 / LugCount + Row * (bTruck ? 3.8 : 1.3);
                    const double Angle = FMath::DegreesToRadians(Degrees);
                    const FVector LugOffset(FMath::Cos(Angle) * 61.5, Row * 12.0 * WidthScale, FMath::Sin(Angle) * 61.5);
                    const FQuat Around = FRotator(Degrees, 0, 0).Quaternion();
                    const FQuat Chevron(FVector::ForwardVector, FMath::DegreesToRadians(Row * (bTruck ? 15.0 : 30.0)));
                    const FVector LugSize(0.076, 0.255 * WidthScale, bTruck ? 0.095 : 0.115);
                    Lugs.Emplace(Around * Chevron, LugOffset, LugSize);
                    if (bDual)
                        Lugs.Emplace(Around * Chevron, LugOffset + FVector(0, InnerTyreOffset, 0), LugSize);
                }
            }
            RepeatedBoxes(TEXT("Tyre"), Hub, Lugs);
            TArray<FTransform> Bolts;
            constexpr int32 BoltCount = 10;
            for (int32 I = 0; I < BoltCount; ++I)
            {
                const double Angle = 2 * RunningGear::Pi * I / BoltCount;
                Bolts.Emplace(FRotator(FMath::RadiansToDegrees(Angle), 0, 0),
                    FVector(23 * FMath::Cos(Angle), Side * 32.6 * WidthScale, 23 * FMath::Sin(Angle)),
                    FVector(0.050, 0.046, 0.050));
            }
            RepeatedBoxes(TEXT("Steel"), Hub, Bolts);
        }
    }
}

void AMudMachine::BuildTracks()
{
    auto* Root = GetRootComponent();
    const bool bDozer = Kind == EMudMachine::Dozer;
    for (const double Side : {-1.0, 1.0})
    {
        auto& Track = Tracks.AddDefaulted_GetRef();
        Track.LateralM = 1.14 * Side;
        Track.Transforms.SetNum(bDozer ? 46 : 42);
        Track.GrouserTransforms.SetNum(Track.Transforms.Num());
        UpdateTrack(Track);
        Track.Shoes = RepeatedBoxes(TEXT("WornSteel"), Root, Track.Transforms);
        Track.Grousers = RepeatedBoxes(TEXT("CuttingEdge"), Root, Track.GrouserTransforms);
        Track.Grousers->SetCastShadow(false);
        Part(TEXT("Cube"), bDozer ? TEXT("Amber") : TEXT("Rubber"), Root,
            {0, Track.LateralM * 100, bDozer ? 49.0 : 66.0}, bDozer ? FVector(3.25, 0.46, 0.26) : FVector(3.55, 0.53, 0.33));

        const auto AddRoller = [&](double AxleX, double HeightCm, double Radius, bool bSprocket)
        {
            auto* Roller = Pivot(Root, FVector(AxleX, Track.LateralM * 100, HeightCm));
            Wheels.Add({Roller, nullptr, Track.LateralM, Radius});
            Part(TEXT("Cylinder"), TEXT("Rubber"), Roller, {}, {Radius * 2, Radius * 2, 0.50}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), bDozer ? TEXT("Amber") : TEXT("Steel"), Roller, {0, Side * 27, 0},
                {Radius * 1.59, Radius * 1.59, 0.12}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), bDozer ? TEXT("Chassis") : TEXT("Rubber"), Roller,
                {0, Side * 34, 0}, {0.22, 0.22, 0.06}, FRotator(0, 0, 90));
            TArray<FTransform> Spokes;
            const int32 Count = bSprocket ? (bDozer ? 13 : 8) : 3;
            for (int32 I = 0; I < Count; ++I)
            {
                const double Angle = 2 * RunningGear::Pi * I / Count;
                const double R = bDozer && bSprocket ? Radius * 94 : Radius * 51;
                Spokes.Emplace(FRotator(FMath::RadiansToDegrees(Angle), 0, 0),
                    FVector(FMath::Cos(Angle) * R, Side * (bDozer && bSprocket ? 22 : 34), FMath::Sin(Angle) * R),
                    bDozer && bSprocket ? FVector(0.077, 0.14, 0.063) : FVector(Radius * 0.49, 0.04, Radius * 0.17));
            }
            RepeatedBoxes(bDozer && bSprocket ? TEXT("WornSteel") : TEXT("Rubber"), Roller, Spokes);
        };
        if (bDozer)
        {
            for (const double AxleX : {-DozerTrackHalfContact, DozerTrackHalfContact})
                AddRoller(AxleX * 100, DozerTrackAxleHeight * 100, DozerTrackRadius - 0.044, false);
            for (const double AxleX : {-128.0, -77.0, -26.0, 26.0, 77.0, 128.0})
                AddRoller(AxleX, 27.0, 0.226, false);
            AddRoller(DozerSprocketX * 100, (DozerTrackAxleHeight + DozerSprocketRise) * 100, DozerTrackRadius - 0.022, true);
        }
        else
        {
            for (const double AxleX : {-170.0, -102.0, -34.0, 34.0, 102.0, 170.0})
            {
                const bool bSprocket = FMath::Abs(AxleX) > 150;
                AddRoller(AxleX, bSprocket ? BeltHeightM * 100 : 34, bSprocket ? BeltPath.Radius - 0.038 : 0.28, bSprocket);
            }
        }
    }
}

void AMudMachine::UpdateTrack(FTrackRig& Track, const AMudTerrain* Terrain)
{
    const bool bDozer = Kind == EMudMachine::Dozer;
    const double Spacing = (bDozer ? DozerBeltPath.Length() : BeltPath.Length()) / Track.Transforms.Num();
    const double AxleHeight = bDozer ? DozerTrackAxleHeight : BeltHeightM;
    const double ShoeWidth = bDozer ? 0.61 : 0.65;
    for (int32 I = 0; I < Track.Transforms.Num(); ++I)
    {
        const double Distance = Track.Phase + I * Spacing;
        const auto Shoe = bDozer ? DozerBeltPath.Sample(Distance) : BeltPath.Sample(Distance);
        FRotator Rotation(Shoe.Pitch, 0, 0);
        FVector ShoePosition(Shoe.Ahead * 100, Track.LateralM * 100, (Shoe.Height + AxleHeight) * 100);
        if(Terrain && Shoe.Height < -(bDozer?DozerTrackRadius:BeltPath.Radius)+.06)
        {
            const FVector World=GetActorTransform().TransformPosition(ShoePosition)/100;
            // Limited bogie/shoe compliance follows depressions without stretching the
            // upper belt or allowing the rigid frame to sink through a crest.
            ShoePosition.Z+=FMath::Clamp((Terrain->Height(Horizontal(World))+.03-World.Z)*100/
                FMath::Max(.5,GetActorUpVector().Z),-10.,6.);
        }
        Track.Transforms[I] = FTransform(Rotation, ShoePosition, FVector(Spacing * 0.89, ShoeWidth, 0.059));
        Track.GrouserTransforms[I] = FTransform(Rotation, ShoePosition + Rotation.RotateVector({0, 0, 4}),
            FVector(Spacing * 0.23, ShoeWidth - 0.05, 0.036));
    }
    if (Track.Shoes)
        Track.Shoes->BatchUpdateInstancesTransforms(0, Track.Transforms, false, true, true);
    if (Track.Grousers)
        Track.Grousers->BatchUpdateInstancesTransforms(0, Track.GrouserTransforms, false, true, true);
}

void AMudMachine::AnimateRunningGear(double Distance, double YawRadians, double FrontSteering, double RearDistance, double RearYaw)
{
    for (auto& Wheel : Wheels)
    {
        const double WheelSteer = Wheel.Knuckle ?
            RunningGear::SteeringDegrees(FrontSteering, TruckWheelbaseM, Wheel.LateralM) : 0;
        if (Wheel.Knuckle)
            Wheel.Knuckle->SetRelativeRotation(FRotator(0, WheelSteer, 0));
        const double AxleTravel = RunningGear::SideDistance(Wheel.bOnRearFrame ? RearDistance : Distance,
            Wheel.bOnRearFrame ? RearYaw : YawRadians, Wheel.LateralM);
        const double Travel = AxleTravel / FMath::Cos(FMath::DegreesToRadians(WheelSteer));
        if (FMath::IsNearlyZero(Travel))
            continue;
        Wheel.Angle = RunningGear::Wrap(Wheel.Angle + RunningGear::RollDegrees(Travel, Wheel.RadiusM), 360);
        Wheel.Hub->SetRelativeRotation(FRotator(Wheel.Angle, 0, 0));
    }
    for (auto& Track : Tracks)
    {
        const double Travel = RunningGear::SideDistance(Distance, YawRadians, Track.LateralM);
        if (!FMath::IsNearlyZero(Travel))
        {
            const double Length = Kind == EMudMachine::Dozer ? DozerBeltPath.Length() : BeltPath.Length();
            Track.Phase = RunningGear::Wrap(Track.Phase + Travel, Length);
        // Ground-conforming shoes are updated after the final support pose below.
        }
    }
}

void AMudMachine::AddHydraulic(USceneComponent* Base, FVector BasePin, USceneComponent* End, FVector RodPin,
    double LengthCm, double DiameterM)
{
    auto* Housing = Part(TEXT("Cylinder"), TEXT("Chassis"), Base, {}, FVector(DiameterM));
    auto* Rod = Part(TEXT("Cylinder"), TEXT("CylinderRod"), Base, {}, FVector(DiameterM * 0.56));
    const FVector PinSize(DiameterM * 0.86, DiameterM * 0.86, DiameterM * 1.24);
    Part(TEXT("Cylinder"), TEXT("Steel"), Base, BasePin, PinSize, FRotator(0, 0, 90));
    Part(TEXT("Cylinder"), TEXT("Steel"), End, RodPin, PinSize, FRotator(0, 0, 90));
    Hydraulics.Add({Base, End, Housing, Rod, BasePin, RodPin, LengthCm, DiameterM});
}

UProceduralMeshComponent* AMudMachine::BuildHeap(USceneComponent* Parent, FVector PositionCm, FVector SizeM)
{
    auto* Heap = NewObject<UProceduralMeshComponent>(this);
    Heap->SetupAttachment(Parent);
    Heap->SetRelativeLocation(PositionCm);
    Heap->SetRelativeScale3D(SizeM);
    Heap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Indices;
    const int32 Across = Kind == EMudMachine::Dozer ? BladeHeapColumns : 25;
    const int32 Wide = Kind == EMudMachine::Dozer ? BladeHeapRows : 17;
    for (int32 Y = 0; Y < Wide; ++Y)
    {
        for (int32 X = 0; X < Across; ++X)
        {
            const double U = static_cast<double>(X) / (Across - 1);
            const double V = static_cast<double>(Y) / (Wide - 1);
            const double CenteredX = U - 0.5, CenteredY = V - 0.5;
            Vertices.Emplace(CenteredX * 100, CenteredY * 100, CargoHeapHeight(CenteredX, CenteredY));
            const FVector Normal = FVector(
                CargoHeapHeight(CenteredX - 0.02, CenteredY) - CargoHeapHeight(CenteredX + 0.02, CenteredY),
                CargoHeapHeight(CenteredX, CenteredY - 0.02) - CargoHeapHeight(CenteredX, CenteredY + 0.02), 4.0).GetSafeNormal();
            Normals.Add(Normal);
            Tangents.Emplace(FVector::VectorPlaneProject(FVector::ForwardVector, Normal).GetSafeNormal(), false);
            UV.Emplace(U * SizeM.X, V * SizeM.Y);
            if (X == 0 || Y == 0)
                continue;
            const int32 Id = Y * Across + X;
            Indices.Append({Id - Across - 1, Id - 1, Id - Across, Id - Across, Id - 1, Id});
        }
    }
    Heap->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, Tangents, false, false);
    Heap->SetMaterial(0, PaintMaterial(TEXT("Loose")));
    Heap->RegisterComponent();
    return Heap;
}

void AMudMachine::BuildCargoSurface(USceneComponent* Parent, FVector PositionCm, FVector SizeM)
{
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/CargoSurface.CargoSurface"));
    if (!Mesh)
    {
        Payload = BuildHeap(Parent, PositionCm, SizeM);
        return;
    }
    CargoSurface = NewObject<UStaticMeshComponent>(this);
    CargoSurface->SetupAttachment(Parent);
    CargoSurface->SetRelativeLocation(PositionCm);
    CargoSurface->SetRelativeScale3D(SizeM);
    CargoSurface->SetStaticMesh(Mesh);
    CargoSurface->SetMaterial(0, PaintMaterial(SurfacePaint[0]));
    CargoSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CargoSurface->RegisterComponent();
}

void AMudMachine::UpdateCargoGrains(const UMeshComponent& Surface, double Fraction)
{
    const double Volume = Cargo.Volume();
    if (Volume <= 0.02)
    {
        if (RenderedCargo.Volume() > 0.02)
            for (auto* Grains : CargoGrains)
                if (Grains)
                    Grains->SetVisibility(false);
        RenderedCargo = {};
        return;
    }

    double Change = 0;
    for (size_t KindIndex = 0; KindIndex < MaterialCount; ++KindIndex)
        Change += FMath::Abs(Cargo.Volumes[KindIndex] - RenderedCargo.Volumes[KindIndex]);
    if (RenderedCargo.Volume() > 0.02 && Change < Capacity() * 0.016)
        return;
    RenderedCargo = Cargo;

    const FTransform HeapPose = Surface.GetRelativeTransform();
    const int32 Count = FMath::Max(7, FMath::RoundToInt((TipBed ? 34 : 21) * FMath::Sqrt(Fraction)));
    FRandomStream Scatter(631 + 47 * static_cast<int32>(Kind));
    TArray<FTransform> Fragments[MaterialCount];
    for (int32 I = 0; I < Count; ++I)
    {
        const double Slot = (I + 0.5) / Count;
        size_t Choice = 0;
        double Cumulative = Cargo.Volumes[0] / Volume;
        while (Choice + 1 < MaterialCount && Slot >= Cumulative)
            Cumulative += Cargo.Volumes[++Choice] / Volume;
        const auto Type = static_cast<Material>(Choice);
        if (Type == Material::Sand || Type == Material::Clay) continue;
        const double Radius = FMath::Sqrt(Scatter.FRand());
        const double Angle = Scatter.FRandRange(0, UE_TWO_PI);
        const double X = Radius * FMath::Cos(Angle) * 0.43, Y = Radius * FMath::Sin(Angle) * 0.44;
        const double SizeRoll = Scatter.FRand();
        const double Size = (Type == Material::Gravel ? FMath::Lerp(0.025, 0.065, SizeRoll) :
            Type == Material::Rock ? FMath::Lerp(0.092, 0.235, SizeRoll) : FMath::Lerp(0.028, 0.065, SizeRoll))
            * FMath::Lerp(0.54, 1.0, FMath::Sqrt(Fraction));
        const FVector Location = HeapPose.TransformPosition(FVector(X * 100, Y * 100, CargoHeapHeight(X, Y))) -
            FVector(0, 0, Size * 13.0);
        const FRotator Rotation(Scatter.FRandRange(-18, 18), Scatter.FRandRange(0, 360), Scatter.FRandRange(-20, 20));
        Fragments[static_cast<size_t>(Type)].Emplace(Rotation, Location,
            FVector(Scatter.FRandRange(0.84, 1.22), 1.0, Type == Material::Soil ? 0.67 : 0.89) * Size);
    }

    for (size_t KindIndex = 0; KindIndex < MaterialCount; ++KindIndex)
    {
        auto*& Grains = CargoGrains[KindIndex];
        const auto& Pieces = Fragments[KindIndex];
        if (Pieces.IsEmpty())
        {
            if (Grains)
                Grains->SetVisibility(false);
            continue;
        }
        if (!Grains)
        {
            auto* Mesh = KindIndex == 0 ? nullptr :
                LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/QuarryFragment.QuarryFragment"));
            if (!Mesh)
                Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/SpoilClump.SpoilClump"));
            if (!Mesh)
                continue;
            Grains = NewObject<UInstancedStaticMeshComponent>(this);
            Grains->SetupAttachment(Surface.GetAttachParent());
            Grains->SetStaticMesh(Mesh);
            Grains->SetMaterial(0, PaintMaterial(CargoPaint[KindIndex]));
            Grains->SetCastShadow(false);
            Grains->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Grains->SetCullDistances(2400, 4000);
            Grains->RegisterComponent();
        }
        Grains->SetVisibility(true);
        if (Grains->GetInstanceCount() == Pieces.Num())
            Grains->BatchUpdateInstancesTransforms(0, Pieces, false, true, true);
        else
        {
            Grains->ClearInstances();
            Grains->AddInstances(Pieces, false, false, false);
        }
    }
}

void AMudMachine::BuildBucket()
{
    constexpr double HalfWidth = BucketHalfWidthCm;
    const FVector2D Profile[] = {{-BucketRearCm, BucketCrownCm}, {-23, -16}, {-17, -40}, {-1, -58},
        {27, BucketFloorCm}, {75, BucketFloorCm}, {-BucketLip.X - 19, BucketLip.Z + 1}};
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<int32> Indices;
    const auto Face = [&](std::initializer_list<FVector> Corners)
    {
        const int32 Base = Vertices.Num();
        const FVector* Points = Corners.begin();
        const auto Mirror = [](FVector Point) { return FVector(-Point.X, Point.Y, Point.Z); };
        const FVector Normal = FVector::CrossProduct(Mirror(Points[1] - Points[0]), Mirror(Points[2] - Points[0])).GetSafeNormal();
        for (const FVector& Source : Corners)
        {
            const FVector Point = Mirror(Source);
            Vertices.Add(Point);
            Normals.Add(Normal);
            UV.Emplace(Point.X * 0.01, (Point.Y + Point.Z) * 0.01);
        }
        Indices.Append({Base, Base + 2, Base + 1});
        if (Corners.size() == 4)
            Indices.Append({Base, Base + 3, Base + 2});
    };
    for (int32 I = 0; I < UE_ARRAY_COUNT(Profile) - 1; ++I)
    {
        const FVector2D A = Profile[I], B = Profile[I + 1];
        Face({{A.X, -HalfWidth, A.Y}, {A.X, HalfWidth, A.Y}, {B.X, HalfWidth, B.Y}, {B.X, -HalfWidth, B.Y}});
        Face({{A.X - 4, HalfWidth, A.Y - 6}, {A.X - 4, -HalfWidth, A.Y - 6},
            {B.X - 4, -HalfWidth, B.Y - 6}, {B.X - 4, HalfWidth, B.Y - 6}});
    }
    for (const double Side : {-HalfWidth, HalfWidth})
    {
        const FVector Anchor(Profile[0].X, Side, Profile[0].Y);
        for (int32 I = 1; I < UE_ARRAY_COUNT(Profile) - 1; ++I)
        {
            const FVector A(Profile[I].X, Side, Profile[I].Y);
            const FVector B(Profile[I + 1].X, Side, Profile[I + 1].Y);
            Face({Anchor, A, B});
            Face({Anchor, B, A});
        }
    }
    auto* Shell = NewObject<UProceduralMeshComponent>(this);
    Shell->SetupAttachment(BucketTiltPivot);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetMaterial(0, PaintMaterial(TEXT("WornSteel")));
    Shell->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, {}, false, false);
    Shell->RegisterComponent();
    BuildBucketEdge(BucketOuterToothCm, BucketLip);
}

void AMudMachine::Initialize(EMudMachine Type, FVector2D Location, AMudTerrain& Terrain)
{
    Kind = Type;
    Position = Location;
    Heading = 0;
    const auto Box = [this](const TCHAR* Material, USceneComponent* Parent, FVector Offset, FVector Size)
    {
        return Part(TEXT("Cube"), Material, Parent, Offset, Size);
    };
    const TCHAR* Paint = Spec(Kind).Paint;
    EnvironmentCollider->SetBoxExtent(FVector(Kind==EMudMachine::Truck?400:Kind==EMudMachine::Dumper?330:220,120,90));
    EnvironmentCollider->SetRelativeLocation(FVector(0,0,130));
    auto* Root = GetRootComponent();
    const bool bTracked = Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer;
    if (Kind == EMudMachine::Dumper || Kind == EMudMachine::Loader)
        RearFrame = Pivot(Root, {0, 0, 0});
    if (bTracked)
        BuildTracks();
    else
        BuildTyres();
    if (RearFrame)
    {
        if (Kind == EMudMachine::Loader)
        {
            Box(TEXT("Chassis"), RearFrame, {-167, 0, 99}, {2.65, 1.64, 0.37});
            Box(TEXT("Chassis"), Root, {133, 0, 99}, {1.94, 1.57, 0.37});
        }
        else
        {
            Box(TEXT("Chassis"), RearFrame, {-181, 0, 99}, {4.38, 1.84, 0.39});
            Box(TEXT("Chassis"), Root, {198, 0, 99}, {2.32, 1.84, 0.39});
        }
    }
    else if (Kind == EMudMachine::Excavator)
    {
        Box(TEXT("Chassis"), Root, {0, 0, 99}, {4.1, 1.84, 0.39});
    }
    if (Kind == EMudMachine::Excavator)
    {
        Turret = Pivot(Root, {0, 0, 119});
        BoomPivot = Pivot(Turret, BoomMount);
        StickPivot = Pivot(BoomPivot, StickMount);
        Tool = Pivot(StickPivot, BucketMount);
        BucketTiltPivot = Pivot(Tool, FVector::ZeroVector);
        BuildExcavatorBody(Paint);
        for (const double Side : {-1.0, 1.0})
            AddHydraulic(Turret, BoomMount + FVector(-12, Side * 29, -54),
                BoomPivot, {163, Side * 29, 22}, 135, 0.22);
        AddHydraulic(BoomPivot, {158, 0, 91}, StickPivot, StickRamPin, 125, 0.18);
        BucketLinkJoint = Pivot(StickPivot, StickRockerPin);
        BucketRocker = Part(TEXT("Cube"), TEXT("Steel"), StickPivot, {}, {RockerLength / 100, 0.21, 0.09});
        BucketLink = Part(TEXT("Cube"), TEXT("Chassis"), StickPivot, {}, {BucketLinkLength / 100, 0.19, 0.09});
        Part(TEXT("Cube"), TEXT("Chassis"), StickPivot, StickRockerPin - FVector(0, 0, 9), {0.26, 0.25, 0.24});
        Part(TEXT("Cube"), TEXT("Chassis"), Tool, {17, 0, 11}, {0.43, 0.23, 0.24});
        Part(TEXT("Cylinder"), TEXT("Steel"), Tool, {}, {0.32, 0.32, 0.27}, FRotator(90, 0, 0));
        Part(TEXT("Cube"), TEXT("Chassis"), BucketTiltPivot, {-10, 0, -5}, {0.29, 0.46, 0.16});
        AddHydraulic(StickPivot, BucketRamBase, BucketLinkJoint, FVector::ZeroVector, 105, 0.15);
        BuildBucket();
        BuildCargoSurface(BucketTiltPivot, {-51, 0, BucketFloorCm + 10}, ExcavatorPayloadSize);
    }
    else if (Kind == EMudMachine::Dozer)
    {
        Tool = Pivot(Root, BladeMount);
        DozerPushArms = Pivot(Root, DozerPushPivot);
        DozerYawCarrier=Pivot(Root,BladeMount+DozerBladePin);
        BuildDozerBody(Paint);
        for (const double Side : {-1.0, 1.0})
        {
            AddHydraulic(Root, {150, Side * 110, 190}, DozerPushArms,
                {DozerPushLength-40, Side*78, 15}, 100, .18);
            AddHydraulic(DozerPushArms, {DozerPushLength-90, Side*DozerPushHalfWidth, 20}, Tool,
                {-50, Side*85, 38}, 85, .14);
        }
        AddHydraulic(DozerYawCarrier,{-50,-55,-10},Tool,{-55,-60,76},40,.13);
        BuildBlade(Paint);
        Payload = BuildHeap(Root, {}, FVector::OneVector);
        Payload->SetAbsolute(true, true, true);
        if (auto* ClumpMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/SpoilClump.SpoilClump")))
        {
            BladeClods = NewObject<UInstancedStaticMeshComponent>(this);
            BladeClods->SetupAttachment(Payload);
            BladeClods->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            BladeClods->SetCastShadow(false);
            BladeClods->SetCullDistances(1800, 2800);
            BladeClods->SetStaticMesh(ClumpMesh);
            BladeClods->SetMaterial(0, PaintMaterial(TEXT("Loose")));
            BladeClods->RegisterComponent();
            for (int32 I = 0; I < BladeClodCount; ++I)
                BladeClods->AddInstance(FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(0.001)));
        }
    }
    else if (Kind == EMudMachine::Loader)
    {
        Boom = 5;
        BoomPivot = Pivot(Root, LoaderPin(LoaderRig::Mount));
        Tool = Pivot(BoomPivot, LoaderPin(LoaderRig::Coupler));
        BuildLoaderBody(Paint);
        for (const double Side : {-1.0, 1.0})
        {
            const double Y = Side * LoaderRig::LiftArmAcross;
            AddHydraulic(Root, {LoaderRig::LiftBase.X, Y, LoaderRig::LiftBase.Z}, BoomPivot,
                {LoaderRig::LiftRod.X, Y, LoaderRig::LiftRod.Z}, 170, 0.20);
            AddHydraulic(Root, {35, Side * 47, 89}, RearFrame, {-57, Side * 66, 89}, 63, 0.11);
        }
        BucketLink = Part(TEXT("Cube"), TEXT("Chassis"), BoomPivot, {}, {LoaderRig::LinkLength / 100, 0.14, 0.12});
        AddHydraulic(BoomPivot, LoaderPin(LoaderRig::TiltBase), LoaderRocker, LoaderPin(LoaderRig::TiltRod), 148, 0.21);
        BuildLoaderBucket(145);
        BuildCargoSurface(Tool, {100, 0, -35}, {1.00, 2.65, 0.92});
    }
    else if (Kind == EMudMachine::Truck)
    {
        TipBed = Pivot(Root, TruckBedHinge);
        TruckTailgate = Pivot(TipBed, {4, 0, 103});
        AddHydraulic(Root, {52, 0, 107}, TipBed, {468, 0, 78}, 99, 0.26);
        BuildTruckBody(Paint);
        BuildTruckBed();
        Tool = Pivot(TipBed, {-17, 0, 10});
        BuildCargoSurface(TipBed, {246, 0, 9}, {4.68, 2.24, 0.92});
    }
    else
    {
        auto* BedFrame = RearFrame.Get();
        TipBed = Pivot(BedFrame, {DumperBedHingeCm, 0, 135});
        AddHydraulic(BedFrame, {-166, 0, 87}, TipBed, {210, 0, -17}, 72, 0.25);
        for (const double Side : {-1.0, 1.0})
            AddHydraulic(Root, {DumperHingeCm + 48, Side * 56, 78}, BedFrame,
                {DumperHingeCm - 55, Side * 69, 82}, 75, 0.15);
        BuildHaulerBody(Paint);
        Tool = Pivot(TipBed, {-59, 0, 30});
        BuildCargoSurface(TipBed, {185, 0, 12}, {3.6, 2.14, 0.8});
    }
    UpdatePose(Terrain);
    if (TipBed) Terrain.RegisterMaterialReceiver(*this);
}

const TCHAR* AMudMachine::Name() const { return Spec(Kind).Name; }
double AMudMachine::Capacity() const { return Spec(Kind).Capacity; }
double AMudMachine::OffRoadRadius() const { return Spec(Kind).Footprint; }

bool AMudMachine::BlocksPedestrian(FVector2D Point, double Height, double Radius) const
{
    const double RelativeHeight = Height - GetActorLocation().Z * 0.01;
    if (RelativeHeight > 3.55 || RelativeHeight < -1.9)
        return false;
    const auto InBody = [&](double Yaw, double Rear, double Front, double HalfWidth, FVector2D Center)
    {
        const FVector2D Offset = Point - Center;
        const FVector2D Forward = Horizontal(FRotator(0, Yaw, 0).Vector());
        const FVector2D Right(-Forward.Y, Forward.X);
        const double X = FVector2D::DotProduct(Offset, Forward);
        const double Y = FVector2D::DotProduct(Offset, Right);
        const FVector2D Excess(X - FMath::Clamp(X, Rear, Front), Y - FMath::Clamp(Y, -HalfWidth, HalfWidth));
        return Excess.SizeSquared() < FMath::Square(Radius);
    };
    const double RearYaw = RearFrame ? RearFrame->GetComponentRotation().Yaw : Heading;
    switch (Kind)
    {
    case EMudMachine::Dozer:
        if (InBody(Heading, -2.64, 2.16, 1.51, Position))
            return true;
        break;
    case EMudMachine::Excavator:
        if (InBody(Heading, -2.22, 2.22, 1.46, Position) || InBody(Heading + Swing, -2.36, 0.98, 1.15, Position))
            return true;
        if (BucketTiltPivot)
        {
            const FTransform& Pose = BucketTiltPivot->GetComponentTransform();
            const FVector Feet = Pose.InverseTransformPosition(FVector(Point.X, Point.Y, Height) * 100);
            const FVector Head = Pose.InverseTransformPosition(FVector(Point.X, Point.Y, Height + 1.83) * 100);
            const FBox BucketBounds(FVector(BucketLip.X - 2, -BucketHalfWidthCm, BucketFloorCm - 6),
                FVector(BucketRearCm + 4, BucketHalfWidthCm, BucketCrownCm));
            return FMath::LineBoxIntersection(BucketBounds.ExpandBy(Radius * 100), Feet, Head, Head - Feet);
        }
        return false;
    case EMudMachine::Loader:
        if (InBody(Heading, -0.2, 2.47, 1.36, Position) || InBody(RearYaw, -3.47, 0.18, 1.37, Position))
            return true;
        break;
    case EMudMachine::Dumper:
        return InBody(Heading, 0.63, 3.35, 1.53, Position) ||
            InBody(RearYaw, -4.12, 0.63, 1.50, RearFramePoint(Position, Heading, Articulation, 0));
    case EMudMachine::Truck:
        return InBody(Heading, -4.15, 3.98, 1.33, Position);
    }
    const FVector Edge = ToolPoint();
    if (Edge.Z < Height - 0.3 || Edge.Z > Height + 1.83)
        return false;
    const FVector2D Forward = Horizontal(FRotator(0, Heading, 0).Vector());
    const FVector2D Right(-Forward.Y, Forward.X);
    const FVector2D FromEdge = Point - Horizontal(Edge);
    const double ToolWidth = Kind == EMudMachine::Dozer ? 1.72 : 1.46;
    const double ToolDepth = Kind == EMudMachine::Dozer ? 0.23 : 0.95;
    const double Along = FVector2D::DotProduct(FromEdge, Forward);
    return Along >= -ToolDepth - Radius && Along <= Radius + 0.2 &&
        FMath::Abs(FVector2D::DotProduct(FromEdge, Right)) <= ToolWidth + Radius;
}

FVector AMudMachine::ToolPoint() const
{
    const FVector Lip = Kind == EMudMachine::Excavator ? BucketLip :
        (Kind == EMudMachine::Loader ? LoaderPin(LoaderRig::Lip) : (Kind == EMudMachine::Dozer ? BladeLip : FVector::ZeroVector));
    const auto* Edge = BucketTiltPivot ? BucketTiltPivot.Get() : Tool.Get();
    return Edge->GetComponentTransform().TransformPosition(Lip) / 100.0;
}

FVector AMudMachine::BucketPoint(double BoomAngle, double StickAngle, double CurlAngle) const
{
    const FVector TiltedLip = FRotator(0, 0, BucketTilt).RotateVector(BucketLip);
    const FVector BucketTip = BucketMount + FRotator(-CurlAngle, 0, 0).RotateVector(TiltedLip);
    const FVector StickTip = StickMount + FRotator(StickAngle, 0, 0).RotateVector(BucketTip);
    return Turret->GetComponentTransform().TransformPosition(
        BoomMount + FRotator(BoomAngle, 0, 0).RotateVector(StickTip)) / 100.0;
}

double AMudMachine::FindDigAngle(const AMudTerrain& Terrain, double TargetHeight) const
{
    double Low = -32.0, High = 48.0;
    for (int32 I = 0; I < 12; ++I)
    {
        const double Angle = (Low + High) * 0.5;
        const FVector Lip = BucketPoint(Angle, Stick, Curl);
        const double Target = DigPhase == EDigPhase::Approach ?
            Terrain.Height(Horizontal(Lip)) - 0.13 : TargetHeight;
        if (Lip.Z < Target)
            Low = Angle;
        else
            High = Angle;
    }
    return (Low + High) * 0.5;
}

FTransform AMudMachine::CabView() const
{
    return CabEye->GetComponentTransform();
}

double AMudMachine::HingeOffsetCm() const
{
    return Kind == EMudMachine::Dumper ? DumperHingeCm : 0;
}

double AMudMachine::ArticulationLimit() const
{
    return Kind == EMudMachine::Dumper ? 45 : (Kind == EMudMachine::Loader ? 28 : 0);
}

void AMudMachine::RestoreArticulation(double Angle)
{
    const double Limit = ArticulationLimit();
    Articulation = FMath::Clamp(Angle, -Limit, Limit);
}

FVector2D AMudMachine::RearFramePoint(FVector2D Center, double FrontHeading, double RearAngle, double RearOffsetM) const
{
    const FVector2D Front = Horizontal(FRotator(0, FrontHeading, 0).Vector());
    const FVector2D Rear = Horizontal(FRotator(0, FrontHeading + RearAngle, 0).Vector());
    const double JointOffset = HingeOffsetCm() / 100;
    return Center + Front * JointOffset + Rear * (RearOffsetM - JointOffset);
}

void AMudMachine::Drive(double Dt, double Throttle, double Steering, bool bBrake, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet)
{
    if (RolledSide != 0)
        return;
    const FVector2D PreviousPosition = Position;
    const double PreviousHeading = Heading;
    for (auto& Wheel : Wheels)
        if (!Wheel.LastImprint.IsSet())
            Wheel.LastImprint = Horizontal(Wheel.Hub->GetComponentLocation()) / 100;
    const auto& Info = Spec(Kind);
    const double CarriedMass = Kind==EMudMachine::Dozer?0:Cargo.Mass();
    const double LoadFraction = Kind==EMudMachine::Dozer?0:Cargo.Volume() / Capacity();
    const double MassRatio = Info.Mass / (Info.Mass + CarriedMass);
    Grip = Terrain.Grip(Position);
    Water = Terrain.WaterDepth(Position);
    const bool bTracked = Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer;
    const FVector Up=GetActorUpVector();
    const FVector2D BodyAcross=Horizontal(GetActorRightVector()).GetSafeNormal(),BodyForward(BodyAcross.Y,-BodyAcross.X);
    const FVector2D BodyGradient=-Horizontal(Up)/FMath::Max(.5,Up.Z);
    const double BodyClearance=(bTracked?.46:Kind==EMudMachine::Truck?.36:Kind==EMudMachine::Dumper?.45:.40)-LoadFraction*.06;
    const double BodyHalfLength=bTracked?(Kind==EMudMachine::Dozer?DozerTrackHalfContact:BeltPath.HalfStraight):
        Kind==EMudMachine::Truck?TruckFrontAxleCm/100:Kind==EMudMachine::Dumper?DumperFrontAxleCm/100:LoaderAxleCm/100;
    const FVector2D BodyReach(bTracked?.72:.86,BodyHalfLength);
    const double BodyShare=Terrain.CompactBody(Position,BodyAcross,BodyReach,
        GetActorLocation().Z/100+BodyClearance/FMath::Max(.5,Up.Z),
        {FVector2D::DotProduct(BodyGradient,BodyAcross),FVector2D::DotProduct(BodyGradient,BodyForward)},
        (Info.Mass+CarriedMass)*9.81/(4*BodyReach.X*BodyReach.Y),Dt);
    SteeringCommand = MachineMotion::Approach(SteeringCommand, Steering, 5, Dt, 0.12);
    DriveCommand = MachineMotion::Approach(DriveCommand, bBrake ? 0 : Throttle, bBrake ? 12 : 6, Dt, 0.16);
    const FVector2D CurrentForward = Horizontal(FRotator(0, Heading, 0).Vector());
    const double Grade = (Terrain.Height(Position + CurrentForward * 2) - Terrain.Height(Position - CurrentForward * 2)) * 0.25;
    const double WaterLimit = FordingLimit(Info.Clearance, LoadFraction);
    const double MaxSpeed = IsBladeWorking() ? 2.35 : Info.Speed;
    const double SpeedLimit = MaxSpeed * FMath::Sqrt(MassRatio);
    double Sink = 0, PeakSink = 0, TotalGrip = 0, WeakestGrip = 1, Radius = 0;
    int32 ContactCount = 0;
    const double SlipSpeed = Drivetrain.WheelSpeed - Speed;
    const auto SampleContact = [&](RunningGear::SoilContact& Soil, FVector2D Point, double ContactRadius, double CargoLoad,
        double DistributedSink=-1,double DistributedGrip=-1)
    {
        const double Equilibrium = DistributedSink>=0?DistributedSink:Terrain.ContactSink(Point, bTracked, CargoLoad);
        Soil.Advance(Equilibrium, SlipSpeed, Speed, Dt);
        const double ContactGrip = (DistributedGrip>=0?DistributedGrip:Terrain.Grip(Point)) * (1.0 - Soil.Shear * 0.40 * FMath::Clamp(Equilibrium / 0.25, 0.0, 1.0));
        Sink += Soil.Sink;
        PeakSink = FMath::Max(PeakSink, Soil.Sink);
        TotalGrip += ContactGrip;
        WeakestGrip = FMath::Min(WeakestGrip, ContactGrip);
        Radius += ContactRadius;
        ++ContactCount;
    };
    if (bTracked)
    {
        const FVector2D Right(-CurrentForward.Y, CurrentForward.X);
        const double HalfLength=Kind==EMudMachine::Dozer?DozerTrackHalfContact:BeltPath.HalfStraight;
        const double Width=Kind==EMudMachine::Dozer?.61:.65;
        const double PressureKPa = (Info.Mass + CarriedMass) * 9.81 / FMath::Max(1.0, Tracks.Num() * 2 * HalfLength * Width);
        for (auto& Track : Tracks)
        {
            const double Pressure=FMath::Clamp(PressureKPa/80.,0.,1.);
            double GroundSink=0,GroundGrip=0;
            for(int I=0;I<5;++I)for(double Y:{-.2,0.,.2})
            {
                const double Weight=(I==2?3.:I==1||I==3?2.:1.)/27.;
                const FVector2D P=Position+CurrentForward*((I-2)*HalfLength*.5)+Right*(Track.LateralM+Y);
                GroundSink+=Terrain.ContactSink(P,true,Pressure)*Weight;
                GroundGrip+=Terrain.Grip(P)*Weight;
            }
            SampleContact(Track.Soil,Position+Right*Track.LateralM,Kind==EMudMachine::Dozer?DozerTrackRadius:BeltPath.Radius,
                Pressure,GroundSink,GroundGrip);
        }
    }
    else
    {
        for (auto& Wheel : Wheels)
        {
            const bool bRear = Wheel.bOnRearFrame || Wheel.Hub->GetRelativeLocation().X < 0;
            const double Share = Kind == EMudMachine::Loader ? (bRear ? 0.4 : 1.6) : (bRear ? 1.25 : 0.5);
            const double ContactMass = Info.Mass / Wheels.Num() + Cargo.Mass() * Share / Wheels.Num();
            const double Area = 4 * Wheel.ContactHalfWidthM * Wheel.RadiusM * 0.48 * (Wheel.InnerTyreOffsetM != 0 ? 2 : 1);
            const double CargoLoad = FMath::Clamp(ContactMass * 9.81 / FMath::Max(0.05, Area) / 300.0, 0.0, 1.0);
            SampleContact(Wheel.Soil, Horizontal(Wheel.Hub->GetComponentLocation()) / 100.0, Wheel.RadiusM, CargoLoad);
        }
    }
    if (ContactCount > 0)
    {
        Sink = FMath::Lerp(Sink / ContactCount, PeakSink, 0.35);
        Grip = FMath::Lerp(TotalGrip / ContactCount, WeakestGrip, 0.30);
        Radius /= ContactCount;
    }
    const double EffectiveGrip = (bTracked ? FMath::Min(1.0, Grip + 0.37) : Grip)*(1-BodyShare);
    DraftForceKN=0;
    if(Kind==EMudMachine::Dozer && IsBladeWorking() && Throttle>=0 && Speed>-.05)
    {
        constexpr int Teeth=13;
        for(int I=0;I<Teeth;++I)
        {
            const auto E=DozerRig::Edge(FMath::Lerp(-DozerRig::HalfBlade,DozerRig::HalfBlade,double(I)/(Teeth-1)));
            const FVector P=Tool->GetComponentTransform().TransformPosition({E.X,E.Y,E.Z})/100;
            const FVector2D XY=Horizontal(P);
            const double Depth=FMath::Max(0.,Terrain.Height(XY)-P.Z);
            DraftForceKN+=DozerRig::DraftKN(Terrain.Surface(XY),Terrain.Wetness(XY),3.44/Teeth,Depth,0,BladeYaw);
        }
        DraftForceKN+=DozerRig::DraftKN(Cargo.DominantMaterial(),Terrain.Wetness(Horizontal(ToolPoint())),0,0,Cargo.Mass(),BladeYaw);
    }
    else if(Kind==EMudMachine::Loader&&bBucketGround&&Curl>=-20&&Curl<=12&&Throttle>=0&&Speed>-.05)
    {
        for(int I=0;I<13;++I)
        {
            const FVector P=Tool->GetComponentTransform().TransformPosition(LoaderPin(LoaderRig::Lip)+
                FVector(0,FMath::Lerp(-141.,141.,I/12.),0))/100;
            const FVector2D XY=Horizontal(P);
            DraftForceKN+=SoilDraft::DraftKN(Terrain.Surface(XY),Terrain.Wetness(XY),2.82/13,
                FMath::Max(0.,Terrain.Height(XY)-P.Z),0,0);
        }
    }
    Speed = Drivetrain.Advance(Speed, DriveCommand, SpeedLimit, EffectiveGrip, Sink, Radius, MassRatio, Grade, bTracked, bBrake, Dt,
        DraftForceKN/(Info.Mass+CarriedMass)+BodyShare*9.81*Terrain.Grip(Position));
    bBogged = Sink > 0.16 && FMath::Abs(Speed) < 0.12 && FMath::Abs(Drivetrain.WheelSpeed - Speed) > 0.65;
    const bool bDumper = Kind == EMudMachine::Dumper;
    const bool bTruck = Kind == EMudMachine::Truck;
    const bool bArticulated = RearFrame != nullptr;
    const double SteeringAtSpeed = SteeringCommand / (1 + FMath::Square(Speed / (bDumper ? 6.0 : 8.0)));
    const double FrontSteering = bTruck ? SteeringAtSpeed * 32 : 0;
    double NextArticulation = Articulation;
    double NextHeading = Heading;
    FVector2D Forward, Next;
    double DriveTravel = Speed * Dt;
    RunningGear::ArticulatedStep SteeringStep{};
    const auto PredictArticulated = [&](double Travel)
    {
        const double AxleOffset = (bDumper ? DumperFrontAxleCm : LoaderAxleCm) / 100;
        const double RearOffset = (bDumper ? DumperBogieCm : -LoaderAxleCm) / 100;
        const double JointOffset = HingeOffsetCm() / 100;
        SteeringStep = RunningGear::MoveArticulated(Travel, AxleOffset - JointOffset, JointOffset - RearOffset,
            FMath::DegreesToRadians(-Articulation), FMath::DegreesToRadians(-NextArticulation));
        const double HeadingDelta = FMath::RadiansToDegrees(SteeringStep.FrontYaw);
        NextHeading = FRotator::NormalizeAxis(Heading + HeadingDelta);
        Forward = Horizontal(FRotator(0, Heading + HeadingDelta * 0.5, 0).Vector());
        // Follow the rolling front axle, then recover the actor origin used by terrain and saves.
        const FVector2D NextFront = Position + CurrentForward * AxleOffset + Forward * Travel;
        Next = NextFront - Horizontal(FRotator(0, NextHeading, 0).Vector()) * AxleOffset;
        DriveTravel = Travel;
    };
    if (bArticulated)
    {
        if (FMath::Abs(Steering) > 0.01 || FMath::Abs(Speed) > 0.08)
            NextArticulation = MachineMotion::Approach(Articulation, -SteeringAtSpeed * (bDumper ? DumperSteeringDegrees : ArticulationLimit()),
                bDumper ? 47 : 54, Dt, bDumper ? 0.16 : 0.13);
        PredictArticulated(DriveTravel);
    }
    else
    {
        double TurnRate = SteeringCommand * Info.Turn;
        if (bTruck)
            TurnRate = FMath::RadiansToDegrees(Speed * FMath::Tan(FMath::DegreesToRadians(FrontSteering)) / TruckWheelbaseM);
        NextHeading = FRotator::NormalizeAxis(Heading + TurnRate * Dt);
        Forward = Horizontal(FRotator(0, Heading + TurnRate * Dt * 0.5, 0).Vector());
        Next = Position + Forward * DriveTravel;
        if (bTruck)
        {
            // Advance the center of the rear tandem along its heading and then recover the chassis origin.
            const FVector2D NewForward = Horizontal(FRotator(0, NextHeading, 0).Vector());
            Next += (CurrentForward - NewForward) * (TruckRearSupportCm / 100);
        }
    }
    if(FMath::Abs(DriveCommand)>.05||FMath::Abs(Speed)>.1)
    {
        const double ContactHeight=IsBladeWorking()?0.95:1.1;
        const double WeightN=(Info.Mass+CarriedMass)*9810;
        const double ForceN=WeightN*EffectiveGrip*FMath::Abs(DriveCommand)+
            FMath::Min((Info.Mass+CarriedMass)*1000*Speed*Speed/.5,WeightN*6);
        const FVector2D PushDirection=Forward*((FMath::Abs(Speed)>.1?Speed:DriveCommand)>=0?1:-1);
        const double Nose=Kind==EMudMachine::Dozer?(IsBladeWorking()?3.1:1.4):Kind==EMudMachine::Truck?3.12:2.55;
        Terrain.PushTrees(Next+PushDirection*Nose,1.3,PushDirection,ForceN,ContactHeight,Dt);
    }
    const auto Blocked = [&]()
    {
        const double NextWater = Terrain.WaterDepth(Next);
        if (Next.X < 3 || Next.Y < 3 || Next.X > Terrain.GetWorldSize() - 3 || Next.Y > Terrain.GetWorldSize() - 3 ||
            (NextWater > WaterLimit && NextWater >= Water - 0.005))
            return true;
        // Use terrain geometry, not an arbitrary maximum slope or a scripted road flag.
        // Tracks have a shorter supported span and can bridge sharper changes in grade.
        const double Rear=bTruck?TruckRearSupportCm/100:bDumper?DumperBogieCm/100:bTracked?-1.6:-LoaderAxleCm/100;
        const double Front=bTruck?TruckRearSupportCm/100+TruckWheelbaseM:bDumper?DumperFrontAxleCm/100:bTracked?1.6:LoaderAxleCm/100;
        const double Clearance=(bTracked?.46:bDumper?.45:bTruck?.36:.40)-LoadFraction*.06-Sink*.45;
        const auto Penetration=[&](FVector2D P,double Yaw,double Bend)
        {
            const FVector2D F=Horizontal(FRotator(0,Yaw,0).Vector()),R(-F.Y,F.X);
            if(bTracked)
            {
                const auto Ground=[&](double X,double Y){return Terrain.Height(P+F*X+R*Y);};
                const double Half=Kind==EMudMachine::Dozer?DozerTrackHalfContact:BeltPath.HalfStraight;
                const auto Support=TrackGround::Fit(Ground,Half,Radius,Tracks.IsEmpty()?1.1:FMath::Abs(Tracks[0].LateralM),.305);
                double Depth=0;
                for(int X=0;X<=14;++X)for(int Y=0;Y<=4;++Y)
                {
                    const double A=FMath::Lerp(-Half,Half,X/14.),B=FMath::Lerp(-.72,.72,Y/4.);
                    Depth=FMath::Max(Depth,Ground(A,B)-Support.At(A,B)-Clearance);
                }
                return Depth;
            }
            return TerrainPassage::BellyPenetration([&](double X,double Y)
            {
                const FVector2D Center=bArticulated&&X<HingeOffsetCm()/100?
                    RearFramePoint(P,Yaw,Bend,X):P+F*X;
                const FVector2D Side=bArticulated&&X<HingeOffsetCm()/100?
                    Horizontal(FRotator(0,Yaw+Bend+90,0).Vector()):R;
                return Terrain.Height(Center+Side*Y);
            },Rear,Front,Wheels.IsEmpty()?1.1:FMath::Abs(Wheels[0].LateralM),Clearance,.86);
        };
        if(TerrainPassage::Grounds(Penetration(Position,Heading,Articulation),Penetration(Next,NextHeading,NextArticulation)))
            return true;
        if (bArticulated || bTruck)
        {
            const auto HitsObstacle = [&](FVector2D Before, FVector2D After)
            {
                const FVector2D Delta = After - Before;
                if (Delta.SizeSquared() < FMath::Square(0.0001))
                    return false;
                constexpr double Radius = 1.26;
                if (Terrain.HitsTree(After, Radius, false) &&
                    (!Terrain.HitsTree(Before, Radius, false) || Terrain.HitsTree(After + Delta.GetSafeNormal() * 0.16, Radius, false)))
                    return true;
                for (const auto* Other : Fleet)
                {
                    if (Other == this || FVector2D::DistSquared(After, Other->Position) > FMath::Square(7.0))
                        continue;
                    const double Height = GetActorLocation().Z / 100.0;
                    if (Other->BlocksPedestrian(After, Height, Radius) &&
                        (!Other->BlocksPedestrian(Before, Height, Radius) ||
                            FVector2D::DistSquared(After, Other->Position) < FVector2D::DistSquared(Before, Other->Position)))
                        return true;
                }
                return false;
            };
            const FVector2D NewFront = Horizontal(FRotator(0, NextHeading, 0).Vector());
            // Loader tyres end 2.43 m ahead of the articulation. The old 1.65 + 1.26 m
            // capsule left a phantom half metre between the tyres and a receiving bed.
            const double FrontProbe = bTruck ? 3.12 : (bDumper ? 2.55 : 1.18);
            if (HitsObstacle(Position + CurrentForward * FrontProbe, Next + NewFront * FrontProbe))
                return true;
            const double RearProbe = Kind == EMudMachine::Loader ? -2.68 : -3.33;
            for (const double Offset : {-0.35, RearProbe * 0.55, RearProbe})
                if (HitsObstacle(RearFramePoint(Position, Heading, Articulation, Offset),
                    RearFramePoint(Next, NextHeading, NextArticulation, Offset)))
                    return true;
            return false;
        }
        if (Terrain.HitsTree(Next + Forward * (Speed >= 0 ? 1.4 : -1.5), OffRoadRadius(), false))
            return true;
        for (const auto* Other : Fleet)
            if (Other != this && FVector2D::DistSquared(Next, Other->Position) < FMath::Square(3.6) &&
                FVector2D::DistSquared(Next, Other->Position) < FVector2D::DistSquared(Position, Other->Position))
                return true;
        return false;
    };
    bool bBlocked = Blocked();
    if (bBlocked && FMath::Abs(Speed) > 0.85)
    {
        ++SoundImpact;
        SoundImpactLevel = FMath::Clamp(FMath::Abs(Speed) * 0.19, 0.24, 1.0);
        bSoundTreeImpact = false;
    }
    if (bBlocked && bArticulated && !FMath::IsNearlyZero(DriveTravel))
    {
        Speed = 0;
        PredictArticulated(0);
        bBlocked = Blocked();
    }
    if (bBlocked)
    {
        Speed = 0;
        DriveTravel = 0;
        SteeringStep = {};
        if (bTracked)
            Heading = NextHeading;
    }
    else
    {
        Position = Next;
        Heading = NextHeading;
        Articulation = NextArticulation;
    }
    Forward = Horizontal(FRotator(0, Heading, 0).Vector());
    const FVector2D Side(-Forward.Y, Forward.X);
    const double CrossSlope = (Terrain.Height(Position - Side * 1.1) - Terrain.Height(Position + Side * 1.1)) / 2.2;
    const double RollLimit = (bTracked ? 0.96 : 0.80) - LoadFraction * 0.23 - FMath::Abs(SteeringCommand * Speed) * 0.023;
    if (FMath::Abs(Speed) > 0.1 && FMath::Abs(CrossSlope) > RollLimit)
    {
        RolledSide = CrossSlope > 0 ? 1 : -1;
        Halt();
        bUnload = false;
        bGravitySpilling = false;
        ++SoundImpact;
        SoundImpactLevel = 0.95f;
        bSoundTreeImpact = false;
    }
    const double Travel = bTracked ? FVector2D::DotProduct(Position - PreviousPosition, Forward) : DriveTravel;
    const double YawRadians = FMath::DegreesToRadians(FRotator::NormalizeAxis(Heading - PreviousHeading));
    const double Spin = RolledSide != 0 ? 0 : Drivetrain.WheelSpeed * Dt - Travel;
    AnimateRunningGear(Travel + Spin, YawRadians, FrontSteering, SteeringStep.RearTravel + Spin, SteeringStep.RearYaw);
    UpdatePose(Terrain);
    if (RolledSide != 0)
        return;
    SoundTravel = (FMath::Abs(Travel) + FMath::Abs(Spin) +
        (bTracked ? FMath::Abs(YawRadians) * BeltPath.HalfStraight : 0)) / Dt;
    SoundSteering = FMath::Clamp(FMath::Abs(SteeringStep.FrontYaw - SteeringStep.RearYaw) / Dt, 0.0, 1.0);
    TrackTravel += FMath::Max(FMath::Abs(Travel), FMath::Abs(SteeringStep.RearTravel));
    TrackScrub += FMath::Abs(Spin) + (bTracked ? FMath::Abs(YawRadians) * BeltPath.HalfStraight :
        FMath::Abs(SteeringStep.FrontYaw - SteeringStep.RearYaw) * 0.55);
    const double ImprintDistance = TrackTravel + TrackScrub;
    if (ImprintDistance < (bTracked ? 0.65 : 0.30))
        return;
    const double Scrub = TrackScrub / ImprintDistance;
    const double ContactHalf=Kind==EMudMachine::Dozer?DozerTrackHalfContact:BeltPath.HalfStraight;
    const double ContactWidth=Kind==EMudMachine::Dozer?.61:.65;
    const double LoadFactor = bTracked ? FMath::Clamp((Info.Mass + CarriedMass) * 9.81 /
        FMath::Max(1.0, Tracks.Num() * 2 * ContactHalf * ContactWidth) / 60.0, 0.2, 3.0) : 1.0;
    TrackTravel = TrackScrub = 0;
    if (bTracked)
    {
        for (const auto& Track : Tracks)
            Terrain.PressTrack(Position + Side * Track.LateralM, Forward, ContactHalf,
                ContactWidth*.5, FMath::Sign(Track.LateralM), Scrub, true, LoadFactor);
    }
    else
    {
        const FVector2D RearForward = Horizontal(FRotator(0, Heading + Articulation, 0).Vector()).GetSafeNormal();
        for (auto& Wheel : Wheels)
        {
            const FVector2D Contact = Horizontal(Wheel.Hub->GetComponentLocation()) / 100;
            const FVector2D PreviousContact = Wheel.LastImprint.GetValue();
            Wheel.LastImprint = Contact;
            const FVector2D ContactTravel = Contact - PreviousContact;
            const double Distance = ContactTravel.Size();
            // Recovery and restored positions start a new trail.
            if (Distance > 5)
                continue;
            const FVector2D TyreForward = Wheel.bOnRearFrame ? RearForward :
                (Wheel.Knuckle ? Horizontal(Wheel.Knuckle->GetForwardVector()).GetSafeNormal() :
                    Horizontal(FRotator(0, Heading, 0).Vector()));
            const FVector2D TyreRight(-TyreForward.Y, TyreForward.X);
            const double LateralTravel = FVector2D::DotProduct(ContactTravel, TyreRight);
            const double WheelScrub = FMath::Clamp(FMath::Max(Scrub, FMath::Abs(LateralTravel) / FMath::Max(0.05, Distance)), 0.0, 1.0);
            const double Outward = FMath::Abs(LateralTravel) > 0.025 ? FMath::Sign(LateralTravel) : FMath::Sign(Wheel.LateralM);
            const int32 Steps = FMath::Max(1, FMath::CeilToInt(Distance / 0.28));
            const bool bRear = Wheel.bOnRearFrame || Wheel.Hub->GetRelativeLocation().X < 0;
            const double Share = Kind == EMudMachine::Loader ? (bRear ? 0.4 : 1.6) : (bRear ? 1.25 : 0.5);
            const double ContactMass = Info.Mass / Wheels.Num() + Cargo.Mass() * Share / Wheels.Num();
            const double Area = 4 * Wheel.ContactHalfWidthM * Wheel.RadiusM * 0.48 * (Wheel.InnerTyreOffsetM != 0 ? 2 : 1);
            const double ContactPressure = FMath::Clamp(ContactMass * 9.81 / FMath::Max(0.05, Area) / 100.0, 0.2, 3.5);
            const double Pressure = ContactPressure * ImprintDistance / (0.55 * Steps);
            const double HalfLength = Wheel.RadiusM * (0.48 + WheelScrub * 0.16);
            for (int32 Step = 1; Step <= Steps; ++Step)
            {
                const FVector2D Point = FMath::Lerp(PreviousContact, Contact, double(Step) / Steps);
                Terrain.PressTrack(Point, TyreForward, HalfLength, Wheel.ContactHalfWidthM, Outward, WheelScrub, false, Pressure);
                if (Wheel.InnerTyreOffsetM != 0)
                    Terrain.PressTrack(Point + TyreRight * Wheel.InnerTyreOffsetM, TyreForward,
                        HalfLength, Wheel.ContactHalfWidthM, Outward, WheelScrub, false, Pressure);
            }
        }
    }
}

void AMudMachine::ToggleTool()
{
    if (Kind == EMudMachine::Dozer)
    {
        bTool = !bTool;
        BladeDatum.Reset();
        if (bTool)
        {
            BladeLift += Blade;
            Blade = 0;
        }
    }
    else if (Kind == EMudMachine::Excavator && !bUnload)
    {
        bDigMissed = false;
        if (DigPhase == EDigPhase::Approach || DigPhase == EDigPhase::Scoop)
            DigPhase = EDigPhase::Lift;
        else if (DigPhase == EDigPhase::Idle && Cargo.Volume() < Capacity() - 0.01 && FMath::Abs(Speed) < 0.35)
            DigPhase = EDigPhase::Approach;
        DigTime = 0;
    }
}

void AMudMachine::ChangeGroup()
{
    if (Kind == EMudMachine::Dozer)
        bGrade = !bGrade;
    else if (Kind == EMudMachine::Excavator)
        bOtherGroup = !bOtherGroup;
}

void AMudMachine::ToggleUnload()
{
    Bed.Manual = false;
    bUnload = !bUnload && Cargo.Volume() > Epsilon;
    if (Kind == EMudMachine::Excavator && bUnload)
    {
        if (DigPhase != EDigPhase::Idle)
            DigPhase = EDigPhase::Lift;
    }
}

void AMudMachine::Halt()
{
    bBogged = false;
    Drivetrain = {};
    Speed = DriveCommand = SteeringCommand = ValveA = ValveB = ValveCurl = ValveSwing = ValveTilt = 0;
    SoundTravel = SoundHydraulic = SoundSteering = SoundEarthFlow = 0;
}

FMudMachineSound AMudMachine::GetSoundState() const
{
    return {static_cast<float>(FMath::Abs(DriveCommand)), SoundTravel,
        FMath::Max(SoundSteering, SoundHydraulic), SoundEarthFlow, SoundImpactLevel, SoundMaterial, SoundImpact,
        bSoundTreeImpact};
}

void AMudMachine::RecordEarthSound(double Rate, Material Type)
{
    SoundEarthFlow = Rate;
    SoundMaterial = Type;
}

void AMudMachine::StopWork()
{
    Halt();
    bTool = bUnload = bBucketObstructed = bBucketGround = false;
    DigPhase = EDigPhase::Idle;
    DigTime = 0;
    bDigMissed = false;
    Articulation = 0;
    if (RearFrame)
        RearFrame->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    AnimateRunningGear(0, 0, 0);
    Bed = {};
    BladeLift = 0.67;
    BladeDatum.Reset();
    BladeHeapAnchor.Reset();
    BladeFlowTravel = RenderedBladeTravel = 0;
    bGravitySpilling = false;
    MaterialMoved = 0;
    LastWorkVolume=0;
    DraftForceKN=0;LastBladePose.Reset();
    TrackTravel = TrackScrub = 0;
    for (auto& Wheel : Wheels)
    {
        Wheel.LastImprint.Reset();
        Wheel.Soil = {};
    }
    for (auto& Track : Tracks)
        Track.Soil = {};
    LastToolContact.Reset();
}

FVector AMudMachine::BladePosition(double Lift) const
{
    const auto P=DozerRig::Neutral(Lift);return {P.X,P.Y,P.Z};
}

double AMudMachine::BladeWingOffset(double AcrossCm)
{
    return DozerRig::Wing(AcrossCm);
}

double AMudMachine::BladeClearance(const AMudTerrain& Terrain, double Lift) const
{
    const FTransform& Chassis = GetActorTransform();
    const FRotator Rotation(0,BladeYaw,BucketTilt);
    const FVector Mount = BladePosition(Lift)+DozerBladePin-Rotation.RotateVector(DozerBladePin);
    double Clearance = TNumericLimits<double>::Max();
    for (int I=0;I<=12;++I)
    {
        const auto P=DozerRig::Edge(FMath::Lerp(-DozerRig::HalfBlade,DozerRig::HalfBlade,I/12.));
        const FVector Edge = Chassis.TransformPosition(Mount+Rotation.RotateVector({P.X,P.Y,P.Z})) / 100;
        Clearance = FMath::Min(Clearance, Edge.Z - Terrain.Height(Horizontal(Edge)));
    }
    return Clearance;
}

double AMudMachine::SampleBladeDatum(const AMudTerrain& Terrain) const
{
    const FVector2D Edge = Horizontal(ToolPoint());
    const FVector2D Across = Horizontal(GetActorRightVector()).GetSafeNormal();
    const FVector2D Forward(Across.Y, -Across.X);
    const auto SampleRow = [&](double Distance)
    {
        const FVector2D Point = Edge + Forward * Distance;
        return (Terrain.Height(Point - Across * 1.08) + 2 * Terrain.Height(Point) +
            Terrain.Height(Point + Across * 1.08)) * 0.25;
    };
    // Reference the grade ahead of the working strip and project it back to the cutting edge.
    const double Near = SampleRow(-2.0);
    const double Rise = Near-SampleRow(-4.0);
    return Near+FMath::Clamp(Rise,-.6,.6);
}

FVector AMudMachine::BedLoadPoint() const
{
    return TipBed ? TipBed->GetComponentTransform().TransformPosition(
        FVector(Kind == EMudMachine::Truck ? 246 : 185, 0, 8)) / 100 : FVector::ZeroVector;
}

bool AMudMachine::InterceptMaterial(FVector From, FVector& To, FVector& Velocity, Load& Material, bool bReceive)
{
    if (!TipBed || RolledSide != 0 || (bReceive && Material.Volume() <= Epsilon)) return false;
    const FTransform Pose = TipBed->GetComponentTransform();
    const FVector A = Pose.InverseTransformPosition(From * 100);
    const FVector B = Pose.InverseTransformPosition(To * 100);
    const FVector Delta = B - A;
    const bool bTruck = Kind == EMudMachine::Truck;
    const double Front = bTruck ? TruckBedFrontCm - 8 : 365;
    const double HalfWidth = bTruck ? 119 : 113;
    const auto Rim = [bTruck](double X)
    {
        return bTruck ? 119.0 : FMath::Lerp(62.0, 103.0, FMath::Clamp(X / 365.0, 0.0, 1.0));
    };
    const double Floor = 8 + (bTruck ? 120 : 100) * FMath::Clamp(Cargo.Volume() / Capacity(), 0.0, 1.0);
    double HitTime = 2;
    FVector Normal = FVector::ZeroVector;
    bool bFloor = false;
    const auto Contact = [&](double T, FVector N, bool IsFloor)
    {
        if (T < 0 || T > 1 || T >= HitTime) return;
        const FVector P = A + Delta * T;
        if (P.X < 0 || P.X > Front || FMath::Abs(P.Y) > HalfWidth + 0.01 ||
            P.Z < 0 || (!IsFloor && P.Z > (FMath::Abs(N.X) > 0 ? 119 : Rim(P.X)))) return;
        HitTime = T; Normal = N; bFloor = IsFloor;
    };
    if (Delta.Z < -0.00001) Contact(A.Z <= Floor ? 0.0 : (Floor - A.Z) / Delta.Z, FVector::UpVector, true);
    for (const double Side : {-1.0, 1.0})
        if (FMath::Abs(Delta.Y) > 0.00001)
            Contact((Side * HalfWidth - A.Y) / Delta.Y,
                FVector(0, A.Y > HalfWidth ? 1 : (A.Y < -HalfWidth ? -1 : -Side), 0), false);
    if (FMath::Abs(Delta.X) > 0.00001)
    {
        Contact((Front - A.X) / Delta.X, FVector(A.X > Front ? 1 : -1, 0, 0), false);
        if (bTruck) Contact(-A.X / Delta.X, FVector(A.X < 0 ? -1 : 1, 0, 0), false);
    }
    if (HitTime > 1) return false;
    FVector Hit = A + Delta * HitTime;
    if (bReceive && bFloor && Bed.Angle < 8 && Cargo.Volume() < Capacity() - Epsilon)
    {
        const Load Received = Material.Take(FMath::Max(0.0, Capacity() - Cargo.Volume()));
        Cargo.Receive(Received);
        // The same cargo drives axle loads, rendering and save/load; no visual-only copies.
    }
    if (bFloor && Material.Volume() > Epsilon)
    {
        // Excess or a raised bed sheds over the nearest side, never disappears.
        const double Side = Hit.Y >= 0 ? 1 : -1;
        Hit = B;
        Hit.Z = FMath::Max(Hit.Z, Floor + 0.1);
        Velocity = Pose.TransformVectorNoScale(FVector(0, Side * 0.8, 0));
    }
    else
    {
        const FVector WorldNormal = Pose.TransformVectorNoScale(Normal).GetSafeNormal();
        Velocity -= WorldNormal * FMath::Min(0.0, FVector::DotProduct(Velocity, WorldNormal)) * 1.15;
        Hit += Normal * 0.5;
    }
    To = Pose.TransformPosition(Hit) / 100.0;
    return true;
}

double AMudMachine::EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    if (XY.X > 1 && XY.Y > 1 && XY.X < Terrain.GetWorldSize() - 1 && XY.Y < Terrain.GetWorldSize() - 1)
    {
        const auto Released = Cargo.Take(Amount);
        if (Kind == EMudMachine::Dozer)
        {
            const FVector2D Across = Horizontal(Tool->GetRightVector()).GetSafeNormal();
            const FVector2D Forward(Across.Y, -Across.X);
            const FVector2D Strip = BladeHeapAnchor.IsSet()?Horizontal(BladeHeapAnchor.GetValue()):XY+Forward*.6;
            Terrain.Spread(Strip, Across, {1.76, 0.97}, Released);
            Terrain.ShowSpill(FVector(XY.X, XY.Y, Terrain.Height(XY)), Released.Volume(), Released.DominantMaterial(),
                GetActorForwardVector(), -BIG_NUMBER, 0.45);
        }
        else
        {
            const FVector PourDirection = TipBed ? -TipBed->GetForwardVector() :
                (Turret ? Turret->GetForwardVector() : GetActorForwardVector());
            Terrain.Pour(Point, PourDirection, Kind == EMudMachine::Excavator ? 1.90 : 2.35, Released);
        }
        return Released.Volume();
    }
    return 0;
}

void AMudMachine::Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet, const MachineMotion::HydraulicInput& Direct)
{
    LastWorkVolume=0;
    if (RolledSide != 0)
        return;
    bBucketObstructed = false;
    const FVector PreviousTip = ToolPoint();
    const FTransform PreviousBucketPose = Tool->GetComponentTransform();
    const double PreviousAngles[] = {Boom, Stick, Curl, Swing, BucketTilt, Bed.Angle};
    const double PreviousBlade = BladeLift + Blade;
    const TOptional<FVector> PreviousBladeHeap = BladeHeapAnchor;
    double BladeInput = 0;
    const double DigResistance = Properties(Terrain.Surface(Horizontal(PreviousTip))).DigResistance;
    const double HydraulicScale = 1.0 / (1.0 + MaterialMoved * DigResistance * 0.12 + Cargo.Mass() * 0.015);
    const auto Move = [Dt, HydraulicScale](double Now, double Target, double Rate, double Response = 0.13)
    {
        return MachineMotion::Approach(Now, Target, Rate * HydraulicScale, Dt, Response);
    };
    const auto Valve = [Dt, HydraulicScale](double& Rate, double TargetRate)
    {
        return MachineMotion::HydraulicTravel(Rate, TargetRate * HydraulicScale, Dt, 0.10);
    };
    if (Kind == EMudMachine::Excavator)
    {
        const double BoomInput = FMath::Clamp(Direct.Boom + (bOtherGroup ? 0 : AxisA), -1.0, 1.0);
        const double StickInput = FMath::Clamp(Direct.Stick + (bOtherGroup ? 0 : AxisB), -1.0, 1.0);
        const double CurlInput = FMath::Clamp(Direct.Curl + (bOtherGroup ? AxisA : 0), -1.0, 1.0);
        const double SwingInput = FMath::Clamp(Direct.Swing + (bOtherGroup ? AxisB : 0), -1.0, 1.0);
        const double TiltInput = FMath::Clamp(Direct.Tilt, -1.0, 1.0);
        const bool bManual = FMath::Abs(BoomInput) + FMath::Abs(StickInput) +
            FMath::Abs(CurlInput) + FMath::Abs(SwingInput) + FMath::Abs(TiltInput) > 0.015;
        if (bManual)
        {
            DigPhase = EDigPhase::Idle;
            bDigMissed = false;
        }
        else if (FMath::Abs(Speed) > 0.55 && (DigPhase == EDigPhase::Approach || DigPhase == EDigPhase::Scoop))
            DigPhase = EDigPhase::Lift;
        if (FMath::Abs(CurlInput) + FMath::Abs(TiltInput) > 0.015)
            bUnload = false;
        if (DigPhase != EDigPhase::Idle || bUnload)
        {
            BucketTilt = Move(BucketTilt, 0, 46);
            ValveTilt = 0;
        }
        else
            BucketTilt = FMath::Clamp(BucketTilt + Valve(ValveTilt, TiltInput * 34),
                -MachineMotion::MaxBucketTilt, MachineMotion::MaxBucketTilt);

        if (DigPhase == EDigPhase::Approach)
        {
            ValveA = ValveB = ValveCurl = ValveSwing = 0;
            DigTime += Dt;
            Stick = Move(Stick, -14, 55);
            Curl = Move(Curl, -73, 76);
            Boom = Move(Boom, FindDigAngle(Terrain, 0), 38);
        }
        else if (DigPhase == EDigPhase::Scoop)
        {
            DigTime += Dt;
            const double Progress = FMath::Clamp(DigTime / 1.55, 0.0, 1.0);
            const double Travel = Progress * Progress * (3.0 - 2.0 * Progress);
            Stick = FMath::Lerp(-14.0, -57.0, Travel);
            Curl = FMath::Lerp(-73.0, 19.0, Travel);
            const double Floor = DigSurface - FMath::Lerp(0.13, 0.74, FMath::Sin(Travel * PI * 0.80));
            Boom = Move(Boom, FindDigAngle(Terrain, Floor), 57, 0.055);
        }
        else if (DigPhase == EDigPhase::Lift)
        {
            ValveA = ValveB = ValveCurl = ValveSwing = 0;
            Boom = Move(Boom, 29, 42);
            Stick = Move(Stick, -33, 42);
            Curl = Move(Curl, 30, 58);
            if (FMath::Abs(Boom - 29) < 0.3 && FMath::Abs(Stick + 33) < 0.3 && FMath::Abs(Curl - 30) < 0.3)
                DigPhase = EDigPhase::Idle;
        }
        else
        {
            Boom = FMath::Clamp(Boom + Valve(ValveA, BoomInput * 29), -32.0, 61.0);
            Stick = FMath::Clamp(Stick + Valve(ValveB, StickInput * 39), -107.0, 24.0);
            if (bUnload)
            {
                ValveCurl = 0;
                Curl = Move(Curl, DumpCurl, 71);
            }
            else
                Curl = FMath::Clamp(Curl + Valve(ValveCurl, CurlInput * 54), -95.0, 78.0);
            Swing = FRotator::NormalizeAxis(Swing + MachineMotion::HydraulicTravel(ValveSwing, SwingInput * 52, Dt, 0.18));
        }
        bTool = DigPhase == EDigPhase::Approach || DigPhase == EDigPhase::Scoop;
    }
    else if (Kind == EMudMachine::Loader)
    {
        const double LiftInput = FMath::Clamp(AxisA + Direct.Boom, -1.0, 1.0);
        const double CurlInput = FMath::Clamp(-AxisB + Direct.Curl, -1.0, 1.0);
        if (FMath::Abs(CurlInput) > 0.08)
            bUnload = false;
        const double PumpShare = 1.0 / FMath::Max(1.0, FMath::Abs(LiftInput) + FMath::Abs(CurlInput));
        Boom = FMath::Clamp(Boom + Valve(ValveA, LiftInput * (LiftInput >= 0 ? 18.0 : 28.0) * PumpShare), LoaderRig::MinBoom, LoaderRig::MaxBoom);
        if (bUnload)
        {
            Curl = FMath::Max(LoaderRig::MinCurl, Curl + Valve(ValveCurl, -43 * PumpShare));
        }
        else
            Curl = FMath::Clamp(Curl + Valve(ValveCurl, CurlInput * 43 * PumpShare), LoaderRig::MinCurl, LoaderRig::MaxCurl);
        Curl = Boom + LoaderRig::BucketPitch(Boom, Curl);
    }
    else if (Kind == EMudMachine::Dozer)
    {
        BucketTilt=FMath::Clamp(BucketTilt+Valve(ValveTilt,Direct.Tilt*12),-DozerRig::MaxTilt,DozerRig::MaxTilt);
        BladeYaw = FMath::Clamp(BladeYaw + FMath::Clamp(AxisB + Direct.Swing, -1.0, 1.0) * 22 * Dt, -25.0, 25.0);
        BladeInput = FMath::Clamp(AxisA + Direct.Boom, -1.0, 1.0);
        if (FMath::Abs(BladeInput) > 0.08 && (bTool || !FMath::IsNearlyEqual(BladeLift, 0.67)))
        {
            // Hand the blade back to the operator at its current height.
            Blade += BladeLift - 0.67;
            BladeLift = 0.67;
            bTool = false;
            BladeDatum.Reset();
        }
        Blade = FMath::Clamp(Blade + Valve(ValveA, BladeInput * 0.7), -1.0, 1.0);
        double Target = 0.67;
        if (bTool)
        {
            if (!BladeDatum.IsSet())
                BladeDatum = SampleBladeDatum(Terrain);
            // Hold the surveyed grade until the operator re-engages or takes manual
            // control. Following our own fresh cut creates a runaway deepening trench.
            const FRotator Orientation(0,BladeYaw,BucketTilt);
            const FVector LocalTip = BladePosition(0)+DozerBladePin+Orientation.RotateVector(BladeLip-DozerBladePin);
            const double NeutralZ = GetActorTransform().TransformPosition(LocalTip).Z / 100.0;
            const double VerticalTravel = FMath::Max(0.65, GetActorUpVector().Z);
            const double Depth = bUnload || bGrade ? 0.075 : 0.28;
            Target = FMath::Clamp((BladeDatum.GetValue() - NeutralZ - Depth) / VerticalTravel, -0.38, 1.05);
        }
        BladeLift = Move(BladeLift, Target, 1.85, 0.13);
        Blade=FMath::Clamp(BladeLift+Blade,DozerRig::MinLift,DozerRig::MaxLift)-BladeLift;
        // A blade does not carry its wedge when the operator lifts it. Use the same
        // finite, conserved spreading operation as explicit unloading, not a teleport.
        if(BladeInput>.08&&BladeLift+Blade>PreviousBlade+.00001&&Cargo.Volume()>Epsilon)bUnload=true;
    }
    if (TipBed)
    {
        if (FMath::Abs(AxisA) > 0.08)
            bUnload = false;
        Bed.Step(AxisA, bUnload, Dt);
    }
    if (Kind == EMudMachine::Excavator && DigPhase == EDigPhase::Idle && Boom < PreviousAngles[0] &&
        FMath::Abs(Stick - PreviousAngles[1]) < 0.0001 && FMath::Abs(Curl - PreviousAngles[2]) < 0.0001)
    {
        const FVector Candidate = BucketPoint(Boom, Stick, Curl);
        const double ContactHeight = Terrain.Height(Horizontal(Candidate)) - 0.12;
        if (Candidate.Z < ContactHeight)
        {
            Boom = FMath::Clamp(FindDigAngle(Terrain, ContactHeight), Boom, PreviousAngles[0]);
            ValveA = 0;
        }
    }
    if (Kind == EMudMachine::Loader && Boom < PreviousAngles[0])
    {
        const auto Penetration = [&](double Angle)
        {
            const FVector Edge = GetActorTransform().TransformPosition(LoaderPin(LoaderRig::CuttingEdge(Angle, Curl))) / 100;
            return Terrain.Height(Horizontal(Edge)) - Edge.Z;
        };
        if (Penetration(Boom) > 0.12 && Penetration(PreviousAngles[0]) <= 0.12)
        {
            double Low = Boom, High = PreviousAngles[0];
            for (int32 I = 0; I < 12; ++I)
            {
                const double Middle = (Low + High) * 0.5;
                if (Penetration(Middle) > 0.12) Low = Middle; else High = Middle;
            }
            Boom = High; ValveA = 0;
        }
    }
    const double JointMotion = FMath::Abs(Boom - PreviousAngles[0]) + FMath::Abs(Stick - PreviousAngles[1]) +
        FMath::Abs(Curl - PreviousAngles[2]) + FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousAngles[3], Swing)) +
        FMath::Abs(BucketTilt - PreviousAngles[4]) + FMath::Abs(Bed.Angle - PreviousAngles[5]);
    SoundHydraulic = FMath::Clamp((JointMotion / 52 + FMath::Abs(BladeLift + Blade - PreviousBlade) * 0.65) / Dt, 0.0, 1.0);
    UpdatePose(Terrain);

    if (Kind == EMudMachine::Loader && JointMotion > 0.00001)
    {
        // Sweep the shell corners too: lowering/curling must not pass through a bed wall.
        const FTransform Candidate = Tool->GetComponentTransform();
        bool Blocked = false;
        for (auto* Receiver : Fleet)
        {
            if (Receiver == this || !Receiver->TipBed) continue;
            for (const double Side : {-145.0, 145.0})
                for (std::size_t I = 0; I < LoaderRig::BucketProfile.size(); ++I)
                {
                    const auto P = LoaderRig::OuterBucketPoint(I);
                    const FVector Local(P.X, Side, P.Z);
                    const FVector From = PreviousBucketPose.TransformPosition(Local) / 100;
                    FVector To = Candidate.TransformPosition(Local) / 100, Velocity = To - From;
                    Load NoMass;
                    Blocked |= Receiver->InterceptMaterial(From, To, Velocity, NoMass, false);
                }
        }
        if (Blocked)
        {
            Boom = PreviousAngles[0]; Curl = PreviousAngles[2]; ValveA = ValveCurl = 0;
            bBucketObstructed = true; UpdatePose(Terrain);
        }
    }
    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    const FVector PreviousContact = LastToolContact.Get(PreviousTip);
    LastToolContact = Point;
    const double Ground = Terrain.Height(XY);
    double Clearance = Kind == EMudMachine::Dozer ? BladeClearance(Terrain, BladeLift + Blade) : Point.Z - Ground;
    const USceneComponent* Bucket = Kind == EMudMachine::Loader ? Tool.Get() : BucketTiltPivot.Get();
    const double BucketHalfEdge = Kind == EMudMachine::Loader ? 141 : BucketOuterToothCm;
    if (Bucket)
    {
        const FVector Lip = Kind == EMudMachine::Loader ? LoaderPin(LoaderRig::Lip) : BucketLip;
        const FTransform& BucketPose = Bucket->GetComponentTransform();
        for (const double Side : {-BucketHalfEdge, BucketHalfEdge})
        {
            const FVector Tooth = BucketPose.TransformPosition(Lip + FVector(0, Side, 0)) / 100.0;
            Clearance = FMath::Min(Clearance, Tooth.Z - Terrain.Height(Horizontal(Tooth)));
        }
    }
    bBucketGround = Clearance <= (Kind == EMudMachine::Dozer ? 0.08 : 0.16);
    double Moved = 0;
    double SpillVolume = 0;
    Material WorkedMaterial = Material::Soil;
    FVector SpillDirection = GetActorForwardVector();
    const bool bLoaderSpilling = Kind == EMudMachine::Loader && Curl <= -32 && Point.Z > Ground + 0.14;
    const bool bExcavatorOpen = BucketTiltPivot &&
        BucketTiltPivot->GetComponentTransform().TransformVectorNoScale(FVector(-0.68, 0, 0.88)).Z < -0.19;
    const bool bExcavatorSpilling = bExcavatorOpen && !bBucketGround && Curl < -52 && Point.Z > Ground + 0.18;
    bGravitySpilling = (bLoaderSpilling || bExcavatorSpilling) && DigPhase == EDigPhase::Idle && Cargo.Volume() > Epsilon;
    const bool bWasUnloading = bUnload || (TipBed && Bed.CanSpill(false)) || bGravitySpilling;
    if (bWasUnloading && DigPhase == EDigPhase::Idle)
    {
        bool bCanTip = TipBed ? Bed.CanSpill(bUnload) : Kind != EMudMachine::Loader || bLoaderSpilling;
        if (Kind == EMudMachine::Excavator)
        {
            bCanTip = bExcavatorOpen && Point.Z > Ground + 0.10;
            bBucketObstructed = !bCanTip && Curl < DumpCurl + 1;
        }
        if (bCanTip)
        {
            WorkedMaterial = Cargo.DominantMaterial();
            Moved = EmptyBucket(Dt * (Kind == EMudMachine::Excavator ? 4.8 : 8.0), Terrain, Fleet);
        }
        if (Cargo.Volume() < Epsilon)
        {
            bUnload = false;
            bGravitySpilling = false;
            if (Kind == EMudMachine::Dozer)
            {
                bTool = false;
                BladeDatum.Reset();
                Blade=BladeLift+Blade-.67;
                BladeLift=.67;
            }
        }
    }
    else if (Kind == EMudMachine::Excavator)
    {
        if (DigPhase == EDigPhase::Approach)
        {
            if (bBucketGround && FMath::Abs(Stick + 14) < 1 && FMath::Abs(Curl + 73) < 1.5)
            {
                DigSurface = Ground;
                DigTime = 0;
                DigPhase = EDigPhase::Scoop;
            }
            else if (DigTime > 3.4)
            {
                bDigMissed = true;
                DigPhase = EDigPhase::Lift;
            }
        }
        else if (DigPhase != EDigPhase::Lift && FMath::Abs(Speed) < 0.35 && !bUnload)
        {
            const bool bDrawingStick = Stick < PreviousAngles[1] - 0.0001;
            const bool bClosingBucket = Curl > PreviousAngles[2] + 0.0001;
            const bool bOpeningBucket = Curl < PreviousAngles[2] - 0.0001;
            const FTransform& BucketPose = BucketTiltPivot->GetComponentTransform();
            // The mouth normal points out between the cutting edge and the upper rim.
            const FVector MouthOut = BucketPose.TransformVectorNoScale(
                FVector(BucketLip.Z - BucketCrownCm, 0, BucketRearCm - BucketLip.X)).GetSafeNormal();
            // Compare at the same boom, turret and chassis pose to exclude their motion from intake.
            const FVector JointStart = BucketPoint(Boom, PreviousAngles[1], PreviousAngles[2]);
            const double IntakeTravel = FMath::Max(0.0, FMath::Min(
                FVector::DotProduct(Point - JointStart, MouthOut), FVector::DotProduct(Point - PreviousTip, MouthOut)));
            if ((bDrawingStick || bClosingBucket) && !bOpeningBucket && IntakeTravel > Epsilon && bBucketGround)
            {
                const double Floor = DigPhase == EDigPhase::Scoop ? FMath::Max(Point.Z, DigSurface - 0.77) : Point.Z;
                const FVector Lateral = BucketPose.GetUnitAxis(EAxis::Y);
                const FVector2D Across = Horizontal(Lateral).GetSafeNormal();
                const FVector2D Forward(Across.Y, -Across.X);
                const double AcrossSlope = Lateral.Z / FMath::Max(0.5, Horizontal(Lateral).Size());
                double BiteDepth = 0;
                constexpr int32 ToothSamples = 7;
                for (int32 Tooth = 0; Tooth < ToothSamples; ++Tooth)
                {
                    const double Side = FMath::Lerp(-BucketOuterToothCm, BucketOuterToothCm, double(Tooth) / (ToothSamples - 1));
                    const FVector Tip = BucketPose.TransformPosition(BucketLip + FVector(0, Side, 0)) / 100.0;
                    BiteDepth += FMath::Clamp(Terrain.Height(Horizontal(Tip)) - Tip.Z + 0.02, 0.0, 0.45) / ToothSamples;
                }
                const double SweptVolume = (2 * BucketHalfWidthCm / 100) * BiteDepth * IntakeTravel;
                const double Amount = FMath::Min3(Dt * 3.2, SweptVolume, Capacity() - Cargo.Volume());
                // A short footprint keeps the cut at the teeth while accommodating the metre terrain grid.
                const Load Cut = Terrain.CutPass(XY, Across, FVector2D(BucketHalfWidthCm / 100 + 0.25, 0.60),
                    Amount, Floor, {AcrossSlope, 0}, true);
                Moved = Cut.Volume();
                Cargo.Receive(Cut);
                SpillVolume = Moved;
                WorkedMaterial = Cut.DominantMaterial();
                SpillDirection = FVector(-Forward.X, -Forward.Y, 0);
            }
            if (DigPhase == EDigPhase::Scoop && (DigTime >= 1.55 || Cargo.Volume() >= Capacity() - 0.01))
                DigPhase = EDigPhase::Lift;
        }
    }
    else if (Kind == EMudMachine::Loader && bBucketGround && Speed > 0.08 && Curl >= -20 && Curl <= 12 &&
        FVector2D::Distance(XY, Horizontal(PreviousContact)) > 0.0001)
    {
        const FVector Lateral = Tool->GetRightVector();
        const FVector2D Across = Horizontal(Lateral).GetSafeNormal();
        const double AcrossSlope = Lateral.Z / FMath::Max(0.5, Horizontal(Lateral).Size());
        const double Free = FMath::Max(0.0, Capacity() - Cargo.Volume());
        const double Amount = Dt * FMath::Min(4.2, Speed * 3.0);
        Load Cut = Terrain.SweepCut(PreviousContact - FVector(0, 0, 0.035), Point - FVector(0, 0, 0.035),
            Across, FVector2D(1.41, 0.50), Amount, {AcrossSlope, 0});
        const double Displaced = Cut.Volume();
        const Material CutMaterial = Cut.DominantMaterial();
        Cargo.Receive(Cut.Take(Free));
        if (Cut.Volume() > Epsilon)
        {
            const Load Left = Cut.Take(Cut.Volume() * 0.5);
            Terrain.Deposit(XY - Across * 1.65, 0.55, Left);
            Terrain.Deposit(XY + Across * 1.65, 0.55, Cut);
        }
        SpillVolume = Moved = Displaced;
        WorkedMaterial = CutMaterial;
    }
    else if (Kind == EMudMachine::Dozer)
    {
        const FTransform PreviousPose=LastBladePose.Get(Tool->GetComponentTransform());
        LastBladePose=Tool->GetComponentTransform();
        const FVector2D Across = Horizontal(Tool->GetRightVector()).GetSafeNormal();
        const FVector2D Forward(Across.Y, -Across.X);
        if (Cargo.Volume() > Epsilon && (Speed < -0.12 || Clearance > 0.20))
        {
            const FVector2D Heap = PreviousBladeHeap.IsSet()
                ? Horizontal(PreviousBladeHeap.GetValue()) : Horizontal(PreviousTip) + Forward * 0.61;
            const auto Released = Cargo.Take(Cargo.Volume());
            WorkedMaterial = Released.DominantMaterial();
            const double Size = FMath::Sqrt(FMath::Clamp(Released.Volume() / Capacity(), 0.0, 1.0));
            Terrain.Spread(Heap, Across, {FMath::Lerp(0.74, 1.70, Size), FMath::Lerp(0.55, 1.32, Size)}, Released);
            Terrain.ShowSpill(FVector(Heap.X, Heap.Y, Terrain.Height(Heap)), Released.Volume(), Released.DominantMaterial(),
                GetActorForwardVector(), -BIG_NUMBER, 0.42);
            Moved = Released.Volume();
        }
        else if (bBucketGround && Speed > 0.08 && FVector2D::Distance(XY, Horizontal(PreviousContact)) > 0.0001)
        {
            BladeFlowTravel += Speed * Dt * (1.0 - Terrain.Wetness(XY) * 0.32);
            const FVector Up = Tool->GetUpVector();
            const FVector2D Gradient = -Horizontal(Up) / FMath::Max(0.65, Up.Z);
            const FVector2D Slopes(FVector2D::DotProduct(Gradient, Across), FVector2D::DotProduct(Gradient, Forward));
            Load Cut;
            constexpr int Strips=12;
            const double Amount=Dt*FMath::Min(5.,Speed*3.8);
            if(bGrade)
            {
                TArray<FVector> Edge;
                for(int I=0;I<=Strips;++I)
                {
                    const auto E=DozerRig::Edge(FMath::Lerp(-DozerRig::HalfBlade,DozerRig::HalfBlade,I/double(Strips)));
                    Edge.Add(Tool->GetComponentTransform().TransformPosition(FVector(E.X,E.Y,E.Z))/100);
                }
                Moved=Terrain.Grade(XY,Across,{1.72,.9},Speed*1.8*Dt,Point.Z-.035,Slopes,&Edge);
            }
            else for(int I=0;I<Strips;++I)
            {
                const double Side=FMath::Lerp(-DozerRig::HalfBlade,DozerRig::HalfBlade,(I+.5)/Strips);
                const auto E=DozerRig::Edge(Side);
                const FVector Local(E.X,E.Y,E.Z);
                const FVector To=Tool->GetComponentTransform().TransformPosition(Local)/100-FVector(0,0,.035);
                const FVector From=PreviousPose.TransformPosition(Local)/100-FVector(0,0,.035);
                const FVector2D Reach(3.44/Strips*.5+.025,.45);
                Cut.Receive(Terrain.SweepCut(From,To,Across,Reach,Amount/Strips,Slopes));
            }
            if(!bGrade)
            {
                Moved = Cut.Volume();
                WorkedMaterial = Cut.DominantMaterial();
                const Load Held = Cut.Take(Capacity() - Cargo.Volume());
                Cargo.Receive(Held);
                if (Cut.Volume() > Epsilon)
                {
                    // Material spills to both sides. Turning and crossfall move the larger share to one wing.
                    const double RightShare = FMath::Clamp(0.57 - Slopes.X * 0.6 - SteeringCommand * 0.28 + BladeYaw / 50.0, 0.10, 0.90);
                    const Load RightLoad = Cut.Take(Cut.Volume() * RightShare);
                    for (const double Side : {-1.0, 1.0})
                    {
                        const Load& Spill = Side > 0 ? RightLoad : Cut;
                        const FVector2D Wing = XY + Across * (Side * 1.68);
                        const FVector2D SidePile = Wing - Forward * 0.52 + Across * (Side * 0.77);
                        Terrain.Spread(SidePile, Across, {0.96, 1.32}, Spill);
                        Terrain.ShowSpill(FVector(Wing.X, Wing.Y, Terrain.MeshHeight(Wing) + 0.04), Spill.Volume(),
                            Spill.DominantMaterial(), GetActorRightVector() * Side - GetActorForwardVector() * 0.28,
                            -BIG_NUMBER, 0.38);
                    }
                }
            }
        }
    }
    if (Kind == EMudMachine::Dozer && bBucketGround && Speed > 0.08 && Cargo.Volume() > Epsilon)
    {
        const FVector2D Across = Horizontal(Tool->GetRightVector()).GetSafeNormal();
        const double Rate = Cargo.Volume() * (FMath::Abs(BladeYaw) / 25.0 * 0.38 +
            FMath::Abs(BucketTilt)/DozerRig::MaxTilt*.16+
            FMath::Max(0.0, Cargo.Volume() / Capacity() - 0.35) * 0.22);
        Load SideFlow = Cargo.Take(Rate * Dt);
        const double RightFraction=FMath::Clamp(.5+BladeYaw/25.0*.45+BucketTilt/DozerRig::MaxTilt*.12,.05,.95);
        const Load RightFlow=SideFlow.Take(SideFlow.Volume()*RightFraction);
        Terrain.Spread(XY+Across*2.25,Across,{.85,1.15},RightFlow);
        Terrain.Spread(XY-Across*2.25,Across,{.85,1.15},SideFlow);
    }
    if (SpillVolume > Epsilon)
        Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Ground, Point.Z)), SpillVolume,
            WorkedMaterial, SpillDirection);
    LastWorkVolume=Moved;
    MaterialMoved = FMath::Max(Moved / Dt, MaterialMoved - 5 * Dt);
    if (Moved > Epsilon)
        RecordEarthSound(Moved / Dt, WorkedMaterial);
    else
        SoundEarthFlow = FMath::Max(0.f, SoundEarthFlow - static_cast<float>(Dt * 16));
    if (Moved > 0)
        UpdatePose(Terrain);
}

void AMudMachine::UpdateBucketLinkage()
{
    const FVector Ear = Tool->GetRelativeTransform().TransformPosition(BucketLinkPin);
    const FVector Span = Ear - StickRockerPin;
    const double Distance = Span.Size();
    const FVector Along = Span / Distance;
    const FVector Above(-Along.Z, 0, Along.X);
    // The knuckle lies where the two fixed-length links meet.
    const double Run = (FMath::Square(RockerLength) - FMath::Square(BucketLinkLength) + Span.SizeSquared()) /
        (2 * Distance);
    const double Rise = FMath::Sqrt(FMath::Max(0.0, FMath::Square(RockerLength) - FMath::Square(Run)));
    const FVector Joint = StickRockerPin + Along * Run + Above * Rise;
    BucketLinkJoint->SetRelativeLocation(Joint);
    BucketRocker->SetRelativeLocationAndRotation((StickRockerPin + Joint) * 0.5,
        FRotationMatrix::MakeFromX(Joint - StickRockerPin).Rotator());
    BucketLink->SetRelativeLocationAndRotation((Ear + Joint) * 0.5, FRotationMatrix::MakeFromX(Ear - Joint).Rotator());
}

void AMudMachine::UpdateLoaderLinkage()
{
    const FVector Ear = Tool->GetRelativeTransform().TransformPosition(LoaderPin(LoaderRig::BucketEar));
    const FVector Joint = LoaderPin(LoaderRig::LinkPin({Ear.X, Ear.Z}));
    const FVector Rocker = Joint - LoaderPin(LoaderRig::RockerPin);
    LoaderRocker->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(FMath::Atan2(Rocker.Z, Rocker.X)), 0, 0));
    BucketLink->SetRelativeLocationAndRotation((Ear + Joint) * 0.5, FRotationMatrix::MakeFromX(Ear - Joint).Rotator());
}

void AMudMachine::UpdateBladeHeap(const AMudTerrain& Terrain, double Fraction)
{
    if (Cargo.Volume() <= 0.02)
    {
        BladeHeapAnchor.Reset();
        return;
    }

    const double Spread = FMath::Sqrt(Fraction);
    const FVector2D Tip = Horizontal(ToolPoint());
    const double Wet = Terrain.Wetness(Tip);
    const double Length = FMath::Lerp(0.48, 2.31, Spread) * (1.0 - Wet * 0.15);
    const double Width = FMath::Lerp(0.83, 3.32, Spread);
    const double Relief = FMath::Lerp(0.10, 1.08, Spread) * (1.0 + Wet * 0.12);
    const FRotator Rotation(0, Heading+BladeYaw, 0);
    const FVector2D Forward = Horizontal(Rotation.Vector());
    const FVector2D Across(-Forward.Y, Forward.X);
    const FVector2D XY = Tip + Forward * (Length * 0.5 - 0.14);
    const FVector Anchor(XY.X, XY.Y, Terrain.MeshHeight(XY));
    // The bank follows the cutting edge every pose. Rebuild the ground contact only after visible movement.
    Payload->SetWorldLocationAndRotation(Anchor * 100, Rotation);
    if (BladeHeapAnchor.IsSet() && FVector::DistSquared(Anchor, BladeHeapAnchor.GetValue()) < FMath::Square(0.035) &&
        FMath::Abs(FMath::FindDeltaAngleDegrees(BladeHeapHeading, Heading+BladeYaw)) < 1.1 &&
        FMath::Abs(BladeFlowTravel - RenderedBladeTravel) < 0.035 &&
        FMath::Abs(Fraction - BladeHeapFraction) < 0.009)
        return;
    BladeHeapAnchor = Anchor;
    BladeHeapHeading = Heading+BladeYaw;
    BladeHeapFraction = Fraction;
    RenderedBladeTravel = BladeFlowTravel;

    const double Flow = BladeFlowTravel;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    constexpr int32 Count = BladeHeapRows * BladeHeapColumns;
    Vertices.Reserve(Count);
    Normals.Reserve(Count);
    UV.Reserve(Count);
    for (int32 Row = 0; Row < BladeHeapRows; ++Row)
    {
        const double V = static_cast<double>(Row) / (BladeHeapRows - 1);
        const double Side = V * 2.0 - 1.0;
        const double Y = Side * Width * 0.5;
        for (int32 Column = 0; Column < BladeHeapColumns; ++Column)
        {
            const double U = static_cast<double>(Column) / (BladeHeapColumns - 1);
            const double X = (U - 0.5) * Length + BladeWingOffset(Y * 100.0) * 0.01 * (1.0 - U);
            const double Ground = Terrain.MeshHeight(XY + Forward * X + Across * Y);
            const double Shoulder = 1.0 - FMath::SmoothStep(0.37, 1.0 - U * 0.16, FMath::Abs(Side));
            const double Crest = FMath::Lerp(0.75, 1.0, FMath::SmoothStep(0.0, 0.23, U))
                * (1.0 - FMath::SmoothStep(0.23, 1.0 - Side * Side * 0.20, U));
            const double MovingX = X - Flow * 0.56;
            const double Fold = FMath::PerlinNoise2D(FVector2D(MovingX * 1.8, Y * 1.6 + 1.2));
            const double Ripples = FMath::PerlinNoise2D(FVector2D(MovingX * 3.6, Y * 3.1 - 4.1));
            const double Height = Relief * Shoulder * Crest * (0.94 + Fold * 0.15 + Ripples * 0.045);
            Vertices.Emplace(X * 100.0, Y * 100.0, (Ground - Anchor.Z + Height) * 100.0 - 2.0);
            UV.Emplace(MovingX * 0.65, Y * 0.65);
        }
    }
    for (int32 Y = 0; Y < BladeHeapRows; ++Y)
    {
        for (int32 X = 0; X < BladeHeapColumns; ++X)
        {
            const int32 Row = Y * BladeHeapColumns;
            const FVector Along = Vertices[Row + FMath::Min(X + 1, BladeHeapColumns - 1)] -
                Vertices[Row + FMath::Max(0, X - 1)];
            const FVector Lateral = Vertices[FMath::Min(Y + 1, BladeHeapRows - 1) * BladeHeapColumns + X] -
                Vertices[FMath::Max(0, Y - 1) * BladeHeapColumns + X];
            Normals.Add(FVector::CrossProduct(Along, Lateral).GetSafeNormal());
        }
    }
    Payload->UpdateMeshSection_LinearColor(0, Vertices, Normals, UV, {}, {}, {}, {}, {}, false);
    if (!BladeClods)
        return;

    // Surface pieces climb at the blade, roll over the crest and fan out towards the ground.
    TArray<FTransform, TInlineAllocator<BladeClodCount>> Pieces;
    for (int32 I = 0; I < BladeClodCount; ++I)
    {
        const double Seed = (I * 7 % BladeClodCount + 0.5) / BladeClodCount;
        const double U = FMath::Fmod(Seed + Flow * (0.38 + 0.035 * (I % 4)), 1.0);
        const double Lane = (I * 11 % BladeClodCount + 0.5) / BladeClodCount - 0.5;
        const double V = 0.5 + Lane * (0.68 + U * 0.68);
        const double GridX = U * (BladeHeapColumns - 1), GridY = V * (BladeHeapRows - 1);
        const int32 X = FMath::Clamp(FMath::FloorToInt(GridX), 0, BladeHeapColumns - 2);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(GridY), 0, BladeHeapRows - 2);
        const int32 Index = Y * BladeHeapColumns + X;
        const FVector Surface = FMath::Lerp(
            FMath::Lerp(Vertices[Index], Vertices[Index + 1], GridX - X),
            FMath::Lerp(Vertices[Index + BladeHeapColumns], Vertices[Index + BladeHeapColumns + 1], GridX - X),
            FMath::Clamp(GridY - Y, 0.0, 1.0));
        const double Envelope = FMath::SmoothStep(0.02, 0.14, U) * (1.0 - FMath::SmoothStep(0.75, 0.99, U))
            * (1.0 - FMath::SmoothStep(0.27, 0.48, FMath::Abs(V - 0.5)));
        const double Size = FMath::Max(0.001, (0.035 + (I % 5) * 0.012) * FMath::Sqrt(Spread) * Envelope);
        const double Roll = FMath::Fmod(Flow * (186.0 + 12.0 * I) + I * 93.0, 360.0);
        Pieces.Emplace(FRotator(-Roll, I * 29.0, Lane * 36.0), Surface + FVector(0, 0, Size * 12.0), FVector(Size));
    }
    BladeClods->BatchUpdateInstancesTransforms(0, MakeArrayView(Pieces.GetData(), Pieces.Num()), false, true, true);
}

void AMudMachine::UpdatePose(const AMudTerrain& Terrain)
{
    const FVector2D Forward = Horizontal(FRotator(0, Heading, 0).Vector());
    const FVector2D Right(-Forward.Y, Forward.X);
    const double FrontSupport = Kind == EMudMachine::Truck ? TruckFrontAxleCm / 100 :
        (Kind == EMudMachine::Dumper ? DumperFrontAxleCm / 100 : (RearFrame ? LoaderAxleCm / 100 : 1.7));
    const double RearSupport = Kind == EMudMachine::Truck ? -TruckRearSupportCm / 100 : FrontSupport;
    const FVector2D FrontAxle = Position + Forward * FrontSupport;
    const double RearFrameAxle = (Kind == EMudMachine::Dumper ? DumperBogieCm : -LoaderAxleCm) / 100;
    const FVector2D RearAxle = RearFrame ? RearFramePoint(Position, Heading, Articulation, RearFrameAxle)
        : Position - Forward * RearSupport;
    const bool bTracked = Kind == EMudMachine::Dozer || Kind == EMudMachine::Excavator;
    const double LoadFraction = Kind==EMudMachine::Dozer?0:Cargo.Volume() / Capacity();
    const FVector2D RearForward = Horizontal(FRotator(0, Heading + Articulation, 0).Vector());
    const FVector2D RearRight(-RearForward.Y, RearForward.X);
    double SupportSink[4]{};
    int32 SupportCount[4]{};
    for (const auto& Wheel : Wheels)
    {
        if (!Wheel.Soil.Initialized)
            continue;
        const bool bRear = Wheel.bOnRearFrame || Wheel.Hub->GetRelativeLocation().X < 0;
        const int32 Group = (bRear ? 2 : 0) + (Wheel.LateralM > 0 ? 1 : 0);
        SupportSink[Group] += Wheel.Soil.Sink;
        ++SupportCount[Group];
    }
    for (const auto& Track : Tracks)
    {
        if (!Track.Soil.Initialized)
            continue;
        const int32 Side = Track.LateralM > 0 ? 1 : 0;
        SupportSink[Side] = SupportSink[Side + 2] = Track.Soil.Sink;
        SupportCount[Side] = SupportCount[Side + 2] = 1;
    }
    const auto SinkAt = [&](FVector2D Axle, FVector2D Lateral, int32 Group)
    {
        return SupportCount[Group] > 0 ? SupportSink[Group] / SupportCount[Group] :
            Terrain.ContactSink(Axle + Lateral * 1.1, bTracked, LoadFraction);
    };
    const double FrontLeft = SinkAt(FrontAxle, -Right, 0), FrontRight = SinkAt(FrontAxle, Right, 1);
    const double RearLeft = SinkAt(RearAxle, -RearRight, 2), RearRightSink = SinkAt(RearAxle, RearRight, 3);
    const double FrontSink = (FrontLeft + FrontRight) * 0.5, RearSink = (RearLeft + RearRightSink) * 0.5;
    const auto AxleHeight = [&](FVector2D Axle, FVector2D Lateral)
    {
        const double Center = Terrain.Height(Axle);
        if (bTracked)
            return Center;
        const double Gauge=Wheels.IsEmpty()?1.1:FMath::Abs(Wheels[0].LateralM);
        return (Terrain.Height(Axle-Lateral*Gauge)+Terrain.Height(Axle+Lateral*Gauge))*.5;
    };
    const double Front = AxleHeight(FrontAxle, Right) - FrontSink;
    const double Back = AxleHeight(RearAxle, RearRight) - RearSink;
    const double Left = Terrain.Height(Position - Right * 1.1) - (FrontLeft + RearLeft) * 0.5;
    const double RightHeight = Terrain.Height(Position + Right * 1.1) - (FrontRight + RearRightSink) * 0.5;
    double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Front - Back, FVector2D::Distance(FrontAxle, RearAxle)));
    double Roll = RolledSide != 0 ? RolledSide * 78.0 :
        FMath::RadiansToDegrees(FMath::Atan2(Left - RightHeight, 2.2));
    double Z = (Terrain.Height(Position) - (FrontSink + RearSink) * 0.5) * 0.5 +
        (Front + Back + Left + RightHeight) * 0.125;
    if(!bTracked)
    {
        const double RearRun=FMath::Max(.5,FVector2D::DotProduct(Position-RearAxle,Forward));
        Z=(Front*RearRun+Back*FrontSupport)/(RearRun+FrontSupport);
    }
    if(bTracked)
    {
        const double Gauge=Tracks.IsEmpty()?1.1:FMath::Abs(Tracks[0].LateralM);
        const auto Support=TrackGround::Fit([&](double X,double Y)
        {
            const double Sink=FMath::Lerp((FrontLeft+RearLeft)*.5,(FrontRight+RearRightSink)*.5,
                FMath::Clamp((Y/Gauge+1)*.5,0.,1.));
            return Terrain.Height(Position+Forward*X+Right*Y)-Sink;
        },Kind==EMudMachine::Dozer?DozerTrackHalfContact:BeltPath.HalfStraight,
            Kind==EMudMachine::Dozer?DozerTrackRadius:BeltPath.Radius,Gauge,.305);
        Z=Support.Height;Pitch=FMath::RadiansToDegrees(FMath::Atan(Support.Pitch));
        if(RolledSide==0)Roll=-FMath::RadiansToDegrees(FMath::Atan(Support.Crossfall));
    }
    SetActorLocationAndRotation(FVector(Position.X * 100, Position.Y * 100, Z * 100 + (RolledSide != 0 ? 115 : 0)),
        FRotator(Pitch, Heading, Roll));
    if (RearFrame)
    {
        const FRotator Bend(0, Articulation, 0);
        const FVector Joint(HingeOffsetCm(), 0, 0);
        // Rotate the load frame about the pin while keeping its existing local attachments.
        RearFrame->SetRelativeLocationAndRotation(Joint - Bend.RotateVector(Joint), Bend);
    }
    if (Kind == EMudMachine::Excavator)
    {
        Turret->SetRelativeRotation(FRotator(0, Swing, 0));
        BoomPivot->SetRelativeRotation(FRotator(Boom, 0, 0));
        StickPivot->SetRelativeRotation(FRotator(Stick, 0, 0));
        Tool->SetRelativeRotation(FRotator(-Curl, 0, 0));
        BucketTiltPivot->SetRelativeRotation(FRotator(0, 0, BucketTilt));
        UpdateBucketLinkage();
    }
    if (Kind == EMudMachine::Dozer)
    {
        Blade=FMath::Clamp(BladeLift+Blade,DozerRig::MinLift,DozerRig::MaxLift)-BladeLift;
        const FVector BladeLocation = BladePosition(BladeLift + Blade);
        BladeYaw=FMath::Clamp(BladeYaw,-DozerRig::MaxYaw,DozerRig::MaxYaw);
        BucketTilt=FMath::Clamp(BucketTilt,-DozerRig::MaxTilt,DozerRig::MaxTilt);
        const FRotator Orientation(0,BladeYaw,BucketTilt);
        Tool->SetRelativeLocationAndRotation(BladeLocation+DozerBladePin-Orientation.RotateVector(DozerBladePin),Orientation);
        const FVector Along = BladeLocation + DozerBladePin - DozerPushPivot;
        DozerPushArms->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(FMath::Atan2(Along.Z, Along.X)), 0, 0));
        DozerYawCarrier->SetRelativeLocationAndRotation(BladeLocation+DozerBladePin,FRotator(0,BladeYaw,0));
    }
    if (Kind == EMudMachine::Loader)
    {
        BoomPivot->SetRelativeRotation(FRotator(Boom, 0, 0));
        Tool->SetRelativeRotation(FRotator(LoaderRig::BucketPitch(Boom, Curl), 0, 0));
        UpdateLoaderLinkage();
    }
    if (TipBed)
        TipBed->SetRelativeRotation(FRotator(Bed.Angle, 0, 0));
    if (TruckTailgate)
    {
        const double Opening = FMath::SmoothStep(9.0, 25.0, Bed.Angle) * (Bed.Angle + 8);
        TruckTailgate->SetRelativeRotation(FRotator(-Opening, 0, 0));
    }
    for (const auto& Link : Hydraulics)
    {
        const FVector BasePin = Link.Base->GetComponentTransform().TransformPosition(Link.BasePin);
        const FVector RodPin = Link.End->GetComponentTransform().TransformPosition(Link.RodPin);
        const FVector Span = RodPin - BasePin;
        const double Length = Span.Size();
        if (Length < 1)
            continue;
        const FVector Direction = Span / Length;
        const FRotator Rotation = FRotationMatrix::MakeFromZ(Direction).Rotator();
        const double BarrelLength = FMath::Min(Link.LengthCm, Length * 0.93);
        const FVector Join = BasePin + Direction * BarrelLength;
        const double RodOffset = BarrelLength * 0.89;
        const FVector RodStart = BasePin + Direction * RodOffset;
        Link.Housing->SetWorldLocationAndRotation((BasePin + Join) * 0.5, Rotation);
        Link.Housing->SetRelativeScale3D({Link.DiameterM, Link.DiameterM, BarrelLength / 100.0});
        Link.Rod->SetWorldLocationAndRotation((RodStart + RodPin) * 0.5, Rotation);
        const double RodDiameter = Link.DiameterM * 0.56;
        Link.Rod->SetRelativeScale3D({RodDiameter, RodDiameter, (Length - RodOffset) / 100.0});
    }
    UMeshComponent* Surface = CargoSurface ? static_cast<UMeshComponent*>(CargoSurface) : Payload.Get();
    Surface->SetVisibility(Cargo.Volume() > 0.02, true);
    const double Fraction = FMath::Clamp(Cargo.Volume() / Capacity(), 0.0, 1.0);
    if (Cargo.Volume() > Epsilon && CargoVisual != Cargo.DominantMaterial())
    {
        CargoVisual = Cargo.DominantMaterial();
        const auto Index = static_cast<uint8>(CargoVisual);
        Surface->SetMaterial(0, PaintMaterial(CargoSurface ? SurfacePaint[Index] : CargoPaint[Index]));
        if (BladeClods)
            BladeClods->SetMaterial(0, Surface->GetMaterial(0));
    }
    if (TipBed)
    {
        const bool bTruck = Kind == EMudMachine::Truck;
        const double Span = FMath::Lerp(0.44, 1.0, FMath::Pow(Fraction, 0.45));
        Surface->SetRelativeScale3D(FVector((bTruck ? 4.68 : 3.60) * Span, (bTruck ? 2.24 : 2.10) * Span,
            FMath::Lerp(0.06, bTruck ? 1.66 : 1.42, FMath::Pow(Fraction, 0.8))));
    }
    if (Kind == EMudMachine::Excavator || Kind == EMudMachine::Loader)
    {
        const FVector Span = Kind == EMudMachine::Loader ? FVector(1.00, 2.65, 0.92) :
            ExcavatorPayloadSize;
        const double Footprint = FMath::Lerp(0.45, 1.0, FMath::Pow(Fraction, 0.45));
        Surface->SetRelativeScale3D(Span * FVector(Footprint, Footprint, FMath::Lerp(0.08, 1.0, FMath::Pow(Fraction, 0.8))));
    }
    if (Kind == EMudMachine::Dozer)
        UpdateBladeHeap(Terrain, Fraction);
    else
        UpdateCargoGrains(*Surface, Fraction);
    for(auto& Track:Tracks)UpdateTrack(Track,&Terrain);
}

FString AMudMachine::ToolDescription() const
{
    if (RolledSide != 0)
        return TEXT("Rolled over | Backspace to recover");
    if (bBogged)
        return TEXT("Bogged down | Ease off, then rock forwards / backwards");
    if (FMath::Abs(Drivetrain.WheelSpeed - Speed) > 1.2 && FMath::Abs(DriveCommand) > 0.35)
        return TEXT("Wheelspin | Reduce throttle to regain grip");
    switch (Kind)
    {
    case EMudMachine::Dozer:
        if(DraftForceKN>Spec(Kind).Mass*4. && FMath::Abs(Speed)<.08 && DriveCommand>.5)
            return FString::Printf(TEXT("Draft %.0f kN | Raise blade for a shallower cut"),DraftForceKN);
        if (bUnload)
            return TEXT("Spreading the load");
        if (!IsBladeWorking())
            return TEXT("Blade raised");
        if (!bBucketGround)
            return TEXT("Down arrow | Lower blade");
        if (!bGrade && Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Full blade | spilling at wings | I to spread");
        return IsMovingEarth() ? (bGrade ? TEXT("Grading the surface") : TEXT("Pushing soil")) :
            (bGrade ? TEXT("Blade down | W to grade") : TEXT("Blade down | W to cut"));
    case EMudMachine::Excavator:
        if (bBucketObstructed)
            return TEXT("Raise bucket or check bed capacity");
        if (DigPhase == EDigPhase::Lift)
            return TEXT("Lifting bucket");
        if (bUnload)
            return TEXT("Emptying bucket");
        if (Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Bucket full | I to tip");
        if (DigPhase == EDigPhase::Approach)
            return TEXT("Reaching for the ground");
        if (DigPhase == EDigPhase::Scoop || IsMovingEarth())
            return TEXT("Drawing the bucket through soil");
        if (bDigMissed)
            return TEXT("Ground out of reach | reposition and press V");
        if (FMath::Abs(Speed) >= 0.35)
            return TEXT("Stop the tracks to dig");
        if (bBucketGround)
            return TEXT("Draw the stick in and curl the bucket to collect soil");
        return TEXT("J/N boom | Shift+K/M bucket");
    case EMudMachine::Loader:
        if (bBucketObstructed)
            return TEXT("Raise bucket or check bed capacity");
        if (bUnload || Curl <= -32)
            return TEXT("Tipping bucket");
        if (Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Bucket full | Raise and tip");
        return IsMovingEarth() ? TEXT("Scooping material") : TEXT("Lower the edge and drive into a pile");
    default:
        return bUnload ? TEXT("Tipping") : (IsTipping() ? TEXT("Lowering bed") : TEXT("Hauling"));
    }
}
