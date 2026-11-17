#include "MudMachine.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MachineMotion.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"
#include "RunningGear.h"

namespace
{
using namespace Earthwork;
struct FSpec
{
    const TCHAR* Name;
    const TCHAR* Paint;
    double Capacity, Speed, Mass, Turn, Clearance, Footprint;
};

constexpr FSpec Specs[] = {
    { TEXT("Volvo ECR255 | Blade"), TEXT("Amber"), 6, 4.2, 19, 68, 1.30, 1.45 },
    { TEXT("Volvo EC300E"), TEXT("Amber"), 2.4, 3.6, 24, 48, 1.30, 1.45 },
    { TEXT("Volvo A30G"), TEXT("Amber"), 16, 10.5, 17, 32, 1.06, 1.48 },
    { TEXT("Volvo FMX 6x6"), TEXT("Teal"), 24, 13.5, 15, 29, 1.05, 1.46 },
    { TEXT("Volvo L120H"), TEXT("Amber"), 3.4, 8.0, 20, 36, 0.80, 1.48 }
};

const FSpec& Spec(EMudMachine Kind) { return Specs[static_cast<uint8>(Kind)]; }

constexpr const TCHAR* CargoPaint[] = { TEXT("Loose"), TEXT("LooseGravel"), TEXT("LooseRock") };
const FVector BucketLip(-106, 0, -49);
const FVector StickRockerPin(195, 0, 14);
const FVector BucketLinkPin(26, 0, 17);
constexpr double RockerLength = 57;
constexpr double BucketLinkLength = 57;
const FVector BladeLip(12, 0, -26.5);
const FVector BladeMount(289, 0, 0);
const FVector LoaderMount(63, 0, 108);
const FVector LoaderCoupler(222, 0, 0);
constexpr double DumpCurl = -93;
constexpr RunningGear::TrackPath BeltPath{1.70, 0.47};
constexpr double BeltHeightM = 0.52;
constexpr double TyreRadiusM = 0.656;
constexpr double TruckWheelbaseM = 3.35;
constexpr double DumperWheelbaseM = 3.35;

UMaterialInterface* PaintMaterial(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Generated/%s.%s"), Name, Name));
}

FVector2D Horizontal(const FVector& Value) { return FVector2D(Value.X, Value.Y); }
}

const FVector AMudMachine::BoomMount(15, 38, 84);
const FVector AMudMachine::StickMount(344, 0, 0);
const FVector AMudMachine::BucketMount(225, 0, 0);
const FVector AMudMachine::StickRamPin(-38, 0, 59);
const FVector AMudMachine::BucketRamBase(49, 0, 58);

AMudMachine::AMudMachine()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MachineRoot"));
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
    const double HaulerAxles[] = {-230, -69, 186};
    const double LoaderAxles[] = {-165, 165};
    const bool bLoader = Kind == EMudMachine::Loader;
    const bool bTruck = Kind == EMudMachine::Truck;
    const double WidthScale = bTruck ? 0.82 : 1.0;
    const TConstArrayView<double> Axles = bLoader ? MakeArrayView(LoaderAxles) : MakeArrayView(HaulerAxles);
    for (const double AxleX : Axles)
    {
        for (const double Side : {-1.0, 1.0})
        {
            constexpr double OffsetM = 1.22;
            auto* Axle = RearFrame && AxleX < 0 ? RearFrame.Get() : Root;
            auto* Mount = Pivot(Axle, FVector(AxleX, Side * OffsetM * 100, TyreRadiusM * 100));
            auto* Knuckle = Kind == EMudMachine::Truck && AxleX > 0 ? Mount : nullptr;
            auto* Hub = Knuckle ? Pivot(Knuckle, FVector::ZeroVector) : Mount;
            Wheels.Add({Hub, Knuckle, Side * OffsetM, TyreRadiusM});
            BuildTyreShell(Hub, WidthScale);
            Part(TEXT("Cylinder"), TEXT("Chassis"), Hub, {}, {0.68, 0.68, 0.51 * WidthScale}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), Kind == EMudMachine::Dumper || bLoader ? TEXT("Amber") : TEXT("Steel"),
                Hub, {0, Side * 27 * WidthScale, 0}, {0.73, 0.73, 0.066 * WidthScale}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Chassis"), Hub, {0, Side * 31 * WidthScale, 0}, {0.55, 0.55, 0.028}, FRotator(0, 0, 90));
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
                    Lugs.Emplace(Around * Chevron, LugOffset, FVector(0.076, 0.255 * WidthScale, bTruck ? 0.095 : 0.115));
                }
            }
            RepeatedBoxes(TEXT("Tyre"), Hub, Lugs);
            TArray<FTransform> Bolts;
            for (int32 I = 0; I < 6; ++I)
            {
                const double Angle = I * RunningGear::Pi / 3;
                Bolts.Emplace(FRotator(60 * I, 0, 0), FVector(22 * FMath::Cos(Angle), Side * 32.6 * WidthScale,
                    22 * FMath::Sin(Angle)), FVector(0.058, 0.048, 0.058));
            }
            RepeatedBoxes(TEXT("Steel"), Hub, Bolts);
        }
    }
}

void AMudMachine::BuildTracks()
{
    auto* Root = GetRootComponent();
    for (const double Side : {-1.0, 1.0})
    {
        auto& Track = Tracks.AddDefaulted_GetRef();
        Track.LateralM = 1.14 * Side;
        Track.Transforms.SetNum(42);
        Track.GrouserTransforms.SetNum(Track.Transforms.Num());
        UpdateTrack(Track);
        Track.Shoes = RepeatedBoxes(TEXT("WornSteel"), Root, Track.Transforms);
        Track.Grousers = RepeatedBoxes(TEXT("CuttingEdge"), Root, Track.GrouserTransforms);
        Track.Grousers->SetCastShadow(false);
        Part(TEXT("Cube"), TEXT("Rubber"), Root, {0, Track.LateralM * 100, 66}, {3.55, 0.53, 0.33});

        for (const double AxleX : {-170.0, -102.0, -34.0, 34.0, 102.0, 170.0})
        {
            const bool bSprocket = FMath::Abs(AxleX) > 150;
            const double Radius = bSprocket ? BeltPath.Radius - 0.038 : 0.28;
            const double HeightCm = bSprocket ? BeltHeightM * 100 : 34;
            auto* Roller = Pivot(Root, FVector(AxleX, Track.LateralM * 100, HeightCm));
            Wheels.Add({Roller, nullptr, Track.LateralM, Radius});
            Part(TEXT("Cylinder"), TEXT("Rubber"), Roller, {}, {Radius * 2, Radius * 2, 0.50}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Steel"), Roller, {0, Side * 27, 0},
                {Radius * 1.59, Radius * 1.59, 0.12}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Rubber"), Roller, {0, Side * 34, 0}, {0.22, 0.22, 0.06}, FRotator(0, 0, 90));
            TArray<FTransform> Spokes;
            const int32 Count = bSprocket ? 8 : 3;
            for (int32 I = 0; I < Count; ++I)
            {
                const double Angle = 2 * RunningGear::Pi * I / Count;
                Spokes.Emplace(FRotator(FMath::RadiansToDegrees(Angle), 0, 0),
                    FVector(FMath::Cos(Angle) * Radius * 51, Side * 34, FMath::Sin(Angle) * Radius * 51),
                    FVector(Radius * 0.49, 0.04, Radius * 0.17));
            }
            RepeatedBoxes(TEXT("Rubber"), Roller, Spokes);
        }
    }
}

void AMudMachine::UpdateTrack(FTrackRig& Track)
{
    const double Spacing = BeltPath.Length() / Track.Transforms.Num();
    for (int32 I = 0; I < Track.Transforms.Num(); ++I)
    {
        const auto Shoe = BeltPath.Sample(Track.Phase + I * Spacing);
        const FRotator Rotation(Shoe.Pitch, 0, 0);
        const FVector ShoePosition(Shoe.Ahead * 100, Track.LateralM * 100, (Shoe.Height + BeltHeightM) * 100);
        Track.Transforms[I] = FTransform(Rotation, ShoePosition, FVector(Spacing * 0.89, 0.65, 0.059));
        Track.GrouserTransforms[I] = FTransform(Rotation, ShoePosition + Rotation.RotateVector({0, 0, 4}),
            FVector(Spacing * 0.23, 0.60, 0.036));
    }
    if (Track.Shoes)
        Track.Shoes->BatchUpdateInstancesTransforms(0, Track.Transforms, false, true, true);
    if (Track.Grousers)
        Track.Grousers->BatchUpdateInstancesTransforms(0, Track.GrouserTransforms, false, true, true);
}

void AMudMachine::AnimateRunningGear(double Distance, double YawRadians, double FrontSteering)
{
    for (auto& Wheel : Wheels)
    {
        if (Wheel.Knuckle)
            Wheel.Knuckle->SetRelativeRotation(FRotator(0,
                RunningGear::SteeringDegrees(FrontSteering, TruckWheelbaseM, Wheel.LateralM), 0));
        const double Travel = RunningGear::SideDistance(Distance, YawRadians, Wheel.LateralM);
        if (FMath::IsNearlyZero(Travel) || (Tracks.IsEmpty() && FMath::IsNearlyZero(Distance)))
            continue;
        Wheel.Angle = RunningGear::Wrap(Wheel.Angle + RunningGear::RollDegrees(Travel, Wheel.RadiusM), 360);
        Wheel.Hub->SetRelativeRotation(FRotator(Wheel.Angle, 0, 0));
    }
    for (auto& Track : Tracks)
    {
        const double Travel = RunningGear::SideDistance(Distance, YawRadians, Track.LateralM);
        if (!FMath::IsNearlyZero(Travel))
        {
            Track.Phase = RunningGear::Wrap(Track.Phase + Travel, BeltPath.Length());
            UpdateTrack(Track);
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
    Heap->SetMaterial(0, PaintMaterial(TEXT("Loose")));
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<int32> Indices;
    constexpr int32 Across = 25;
    constexpr int32 Wide = 17;
    const auto Top = [](double X, double Y)
    {
        const double Profile = FMath::Max(0.0, 1.0 - FMath::Pow(FMath::Abs(X * 2), 2.8) - FMath::Pow(FMath::Abs(Y * 2), 2.2));
        const double Ridge = FMath::PerlinNoise2D(FVector2D(X * 6.4 + 1.7, Y * 4.2 - 0.8));
        const double Clumps = FMath::PerlinNoise2D(FVector2D(X * 18.0 - 1.2, Y * 13.0));
        return FMath::Pow(Profile, 0.87) * 100.0 * (0.69 + 0.37 * Ridge + 0.12 * Clumps);
    };
    for (int32 Y = 0; Y < Wide; ++Y)
    {
        for (int32 X = 0; X < Across; ++X)
        {
            const double U = static_cast<double>(X) / (Across - 1);
            const double V = static_cast<double>(Y) / (Wide - 1);
            const double CenteredX = U - 0.5, CenteredY = V - 0.5;
            Vertices.Emplace(CenteredX * 100, CenteredY * 100, Top(CenteredX, CenteredY));
            Normals.Add(FVector(Top(CenteredX - 0.02, CenteredY) - Top(CenteredX + 0.02, CenteredY),
                Top(CenteredX, CenteredY - 0.02) - Top(CenteredX, CenteredY + 0.02), 4.0).GetSafeNormal());
            UV.Emplace(U, V);
            if (X == 0 || Y == 0)
                continue;
            const int32 Id = Y * Across + X;
            Indices.Append({Id - Across - 1, Id - 1, Id - Across, Id - Across, Id - 1, Id});
        }
    }
    Heap->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, {}, false, false);
    Heap->RegisterComponent();
    return Heap;
}

void AMudMachine::BuildBucket(double HalfWidth)
{
    const FVector2D DiggerProfile[] = {{-24, 16}, {-22, -12}, {-16, -30}, {-1, -45}, {24, -53}, {58, -53}, {87, -48}};
    const FVector2D LoaderProfile[] = {{-29, 38}, {-30, 11}, {-18, -32}, {2, -50}, {29, -51}, {67, -50}, {103, -49}};
    const bool bLoader = Kind == EMudMachine::Loader;
    const TConstArrayView<FVector2D> Profile = bLoader ? MakeArrayView(LoaderProfile) : MakeArrayView(DiggerProfile);
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
    for (int32 I = 0; I < Profile.Num() - 1; ++I)
    {
        const FVector2D A = Profile[I], B = Profile[I + 1];
        Face({{A.X, -HalfWidth, A.Y}, {A.X, HalfWidth, A.Y}, {B.X, HalfWidth, B.Y}, {B.X, -HalfWidth, B.Y}});
        Face({{A.X - 4, HalfWidth, A.Y - 6}, {A.X - 4, -HalfWidth, A.Y - 6},
            {B.X - 4, -HalfWidth, B.Y - 6}, {B.X - 4, HalfWidth, B.Y - 6}});
    }
    for (const double Side : {-HalfWidth, HalfWidth})
    {
        const FVector Anchor(Profile[0].X, Side, Profile[0].Y);
        for (int32 I = 1; I < Profile.Num() - 1; ++I)
        {
            const FVector A(Profile[I].X, Side, Profile[I].Y);
            const FVector B(Profile[I + 1].X, Side, Profile[I + 1].Y);
            Face({Anchor, A, B});
            Face({Anchor, B, A});
        }
    }
    auto* Shell = NewObject<UProceduralMeshComponent>(this);
    Shell->SetupAttachment(Tool);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetMaterial(0, PaintMaterial(TEXT("WornSteel")));
    Shell->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, {}, false, false);
    Shell->RegisterComponent();
    BuildBucketEdge(HalfWidth, BucketLip, bLoader);
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
        Box(TEXT("Chassis"), RearFrame, {-157, 0, 99}, {3.14, 1.84, 0.39});
        Box(TEXT("Chassis"), Root, {157, 0, 99}, {3.14, 1.84, 0.39});
    }
    else
    {
        Box(TEXT("Chassis"), Root, {0, 0, 99}, {bTracked ? 4.1 : 6.5, 1.84, 0.39});
    }
    Turret = Pivot(Root, {0, 0, 119});
    if (Kind == EMudMachine::Excavator)
    {
        BoomPivot = Pivot(Turret, BoomMount);
        StickPivot = Pivot(BoomPivot, StickMount);
        Tool = Pivot(StickPivot, BucketMount);
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
        AddHydraulic(StickPivot, BucketRamBase, BucketLinkJoint, FVector::ZeroVector, 105, 0.15);
        BuildBucket();
        Payload = BuildHeap(Tool, {-40, 0, -43}, {0.73, 1.13, 0.72});
    }
    else if (Kind == EMudMachine::Dozer)
    {
        BoomPivot = Pivot(Turret, BoomMount);
        StickPivot = Pivot(BoomPivot, StickMount);
        BuildDozerBody(Paint);
        AddHydraulic(Turret, BoomMount + FVector(-12, 0, -54), BoomPivot, {163, 0, 22}, 135, 0.23);
        AddHydraulic(BoomPivot, {158, 0, 91}, StickPivot, StickRamPin, 125, 0.18);
        Tool = Pivot(Root, BladeMount);
        for (const double Side : {-1.0, 1.0})
            AddHydraulic(Root, {83, Side * 103, 93}, Tool, {-17, Side * 113, 23}, 124, 0.23);
        BuildBlade();
        Payload = BuildHeap(Tool, {60, 0, 1}, {1.1, 2.9, 0.57});
    }
    else if (Kind == EMudMachine::Loader)
    {
        Boom = 5;
        BoomPivot = Pivot(Root, LoaderMount);
        Tool = Pivot(BoomPivot, LoaderCoupler);
        BuildLoaderBody(Paint);
        for (const double Side : {-1.0, 1.0})
            AddHydraulic(Root, {68, Side * 48, 65}, BoomPivot, {137, Side * 48, 0}, 110, 0.20);
        AddHydraulic(BoomPivot, {35, 0, 25}, Tool, {9, 0, 37}, 135, 0.19);
        BuildBucket(124);
        Payload = BuildHeap(Tool, {-40, 0, -43}, {0.85, 2.15, 0.72});
    }
    else
    {
        auto* BedFrame = RearFrame ? RearFrame.Get() : Root;
        TipBed = Pivot(BedFrame, {-293, 0, 135});
        AddHydraulic(BedFrame, {-166, 0, 87}, TipBed, {210, 0, -17}, 72, 0.25);
        BuildHaulerBody(Paint);
        Tool = Pivot(TipBed, {-59, 0, 30});
        Payload = BuildHeap(TipBed, {185, 0, 12}, {3.6, 2.14, 0.8});
    }
    UpdatePose(Terrain);
}

const TCHAR* AMudMachine::Name() const { return Spec(Kind).Name; }
double AMudMachine::Capacity() const { return Spec(Kind).Capacity; }
double AMudMachine::OffRoadRadius() const { return Spec(Kind).Footprint; }

FVector AMudMachine::ToolPoint() const
{
    const FVector Lip = Kind == EMudMachine::Excavator || Kind == EMudMachine::Loader ? BucketLip :
        (Kind == EMudMachine::Dozer ? BladeLip : FVector::ZeroVector);
    return Tool->GetComponentTransform().TransformPosition(Lip) / 100.0;
}

FVector AMudMachine::BucketPoint(double BoomAngle, double StickAngle, double CurlAngle) const
{
    const FVector BucketTip = BucketMount + FRotator(-CurlAngle, 0, 0).RotateVector(BucketLip);
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

void AMudMachine::Drive(double Dt, double Throttle, double Steering, bool bBrake, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet)
{
    if (RolledSide != 0)
        return;
    const FVector2D PreviousPosition = Position;
    const double PreviousHeading = Heading;
    const auto& Info = Spec(Kind);
    const double LoadFraction = Cargo.Volume() / Capacity();
    const double MassRatio = Info.Mass / (Info.Mass + Cargo.Mass());
    Grip = Terrain.Grip(Position);
    Water = Terrain.WaterDepth(Position);
    const bool bTracked = Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer;
    SteeringCommand = MachineMotion::Approach(SteeringCommand, Steering, 5, Dt, 0.12);
    DriveCommand = MachineMotion::Approach(DriveCommand, bBrake ? 0 : Throttle, bBrake ? 12 : 6, Dt, 0.16);
    const FVector2D CurrentForward = Horizontal(FRotator(0, Heading, 0).Vector());
    const double Grade = (Terrain.Height(Position + CurrentForward * 2) - Terrain.Height(Position - CurrentForward * 2)) * 0.25;
    const double EffectiveGrip = bTracked ? FMath::Min(1.0, Grip + 0.37) : Grip;
    const double WaterLimit = FordingLimit(Info.Clearance, LoadFraction);
    const double MaxSpeed = IsBladeWorking() ? 2.35 : Info.Speed;
    const double SpeedLimit = MaxSpeed * FMath::Sqrt(MassRatio) * FMath::Lerp(0.08, 1.0, EffectiveGrip);
    const bool bTooSteep = Grade * DriveCommand > (0.23 + 0.47 * EffectiveGrip) * FMath::Lerp(0.62, 1.0, MassRatio);
    const bool bBogged = !bTracked && LoadFraction > 0.5 && EffectiveGrip < 0.18;
    const double Target = bTooSteep || bBrake || bBogged ? 0 :
        DriveCommand * SpeedLimit * (DriveCommand < 0 ? 0.48 : 1.0);
    Speed = MachineMotion::Approach(Speed, Target, bBrake ? 9.0 : 3.4 * MassRatio + 1, Dt, bBrake ? 0.08 : 0.20);
    const double SteeringAtSpeed = SteeringCommand / (1 + FMath::Square(Speed / 8.0));
    const double FrontSteering = Kind == EMudMachine::Truck ? SteeringAtSpeed * 32 : 0;
    double TurnRate = SteeringCommand * Info.Turn;
    if (!bTracked)
    {
        if (RearFrame)
        {
            Articulation = MachineMotion::Approach(Articulation, -SteeringAtSpeed * 28, 54, Dt, 0.13);
            RearFrame->SetRelativeRotation(FRotator(0, Articulation, 0));
        }
        const double Curvature = RearFrame ?
            2 * FMath::Tan(FMath::DegreesToRadians(-Articulation * 0.5)) / DumperWheelbaseM :
            FMath::Tan(FMath::DegreesToRadians(FrontSteering)) / TruckWheelbaseM;
        TurnRate = FMath::Clamp(FMath::RadiansToDegrees(Speed * Curvature), -Info.Turn, Info.Turn);
    }
    Heading = FRotator::NormalizeAxis(Heading + TurnRate * Dt);
    const FVector2D Forward = Horizontal(FRotator(0, FRotator::NormalizeAxis(Heading - TurnRate * Dt * 0.5), 0).Vector());
    const FVector2D Next = Position + Forward * Speed * Dt;
    const double NextWater = Terrain.WaterDepth(Next);
    bool bBlocked = Next.X < 3 || Next.Y < 3 || Next.X > Terrain.GetWorldSize() - 3 || Next.Y > Terrain.GetWorldSize() - 3 ||
        (NextWater > WaterLimit && NextWater >= Water - 0.005);
    if (IsBladeWorking() && Speed > 0 && bBucketGround)
        Terrain.HitsTree(Position + Forward * 3.1, 1.95, true);
    bBlocked |= Terrain.HitsTree(Next + Forward * (Speed >= 0 ? 1.4 : -1.5), OffRoadRadius(), false);
    for (const auto* Other : Fleet)
    {
        if (Other == this)
            continue;
        if (FVector2D::DistSquared(Next, Other->Position) < FMath::Square(3.6) &&
            FVector2D::DistSquared(Next, Other->Position) < FVector2D::DistSquared(Position, Other->Position))
            bBlocked = true;
    }
    if (bBlocked)
    {
        Speed = 0;
        if (!bTracked)
            Heading = PreviousHeading;
    }
    else
        Position = Next;
    const FVector2D Side(-Forward.Y, Forward.X);
    const double CrossSlope = (Terrain.Height(Position - Side * 1.1) - Terrain.Height(Position + Side * 1.1)) / 2.2;
    const double RollLimit = (bTracked ? 0.96 : 0.80) - LoadFraction * 0.23 - FMath::Abs(SteeringCommand * Speed) * 0.023;
    if (FMath::Abs(Speed) > 0.1 && FMath::Abs(CrossSlope) > RollLimit)
    {
        RolledSide = CrossSlope > 0 ? 1 : -1;
        Speed = 0;
        bUnload = false;
    }
    const double Travel = FVector2D::DotProduct(Position - PreviousPosition, Forward);
    const double YawRadians = FMath::DegreesToRadians(FRotator::NormalizeAxis(Heading - PreviousHeading));
    AnimateRunningGear(Travel, YawRadians, FrontSteering);
    UpdatePose(Terrain);
    if (RolledSide != 0)
        return;
    TrackTravel += FMath::Abs(Travel);
    TrackScrub += bTracked ? FMath::Abs(YawRadians) * BeltPath.HalfStraight : 0.0;
    const double ImprintDistance = TrackTravel + TrackScrub;
    if (ImprintDistance < (bTracked ? 0.65 : 1.0))
        return;
    const double Scrub = TrackScrub / ImprintDistance;
    TrackTravel = TrackScrub = 0;
    if (bTracked)
    {
        for (const auto& Track : Tracks)
            Terrain.PressTrack(Position + Side * Track.LateralM, Forward, BeltPath.HalfStraight,
                0.79, FMath::Sign(Track.LateralM), Scrub, true);
    }
    else
    {
        for (const auto& Wheel : Wheels)
            Terrain.PressTrack(Horizontal(Wheel.Hub->GetComponentLocation()) / 100, Forward,
                0.74, 0.67, FMath::Sign(Wheel.LateralM), 0, false);
    }
}

void AMudMachine::ToggleTool()
{
    if (Kind == EMudMachine::Dozer)
    {
        bTool = !bTool;
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
    Speed = DriveCommand = SteeringCommand = ValveA = ValveB = ValveCurl = ValveSwing = 0;
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
        RearFrame->SetRelativeRotation(FRotator::ZeroRotator);
    AnimateRunningGear(0, 0, 0);
    Bed = {};
    BladeLift = 0.67;
    MaterialMoved = 0;
    TrackTravel = TrackScrub = 0;
}

double AMudMachine::BladeClearance(const AMudTerrain& Terrain, double Lift) const
{
    const FTransform& Chassis = GetActorTransform();
    double Clearance = TNumericLimits<double>::Max();
    for (const double Across : {-1.5, 0.0, 1.5})
    {
        const FVector Edge = Chassis.TransformPosition(BladeMount + BladeLip + FVector(0, Across * 100, Lift * 100)) / 100;
        Clearance = FMath::Min(Clearance, Edge.Z - Terrain.Height(Horizontal(Edge)));
    }
    return Clearance;
}

double AMudMachine::EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    if (Kind == EMudMachine::Excavator || Kind == EMudMachine::Loader)
    {
        for (auto* Other : Fleet)
        {
            if (!Other->TipBed || Other->IsTipping() || Other->RolledSide != 0)
                continue;
            const FVector Relative = Other->TipBed->GetComponentTransform().InverseTransformPosition(Point * 100);
            if (Relative.X < -12 || Relative.X > 377 || FMath::Abs(Relative.Y) > 120)
                continue;
            const double Free = Other->Capacity() - Other->Cargo.Volume();
            if (Relative.Z < 95 || Free <= Epsilon)
            {
                bBucketObstructed = true;
                return 0;
            }
            const Load Released = Cargo.Take(FMath::Min(Amount, Free));
            Other->Cargo.Receive(Released);
            const double Fill = Other->Cargo.Volume() / Other->Capacity();
            const double Floor = Other->TipBed->GetComponentTransform().TransformPosition(
                FVector(Relative.X, Relative.Y, 12 + 45 * Fill)).Z / 100.0;
            Terrain.ShowSpill(Point, Released.Volume(), Released.DominantMaterial(), FVector::ZeroVector, Floor);
            Other->UpdatePose(Terrain);
            return Released.Volume();
        }
    }
    if (XY.X > 1 && XY.Y > 1 && XY.X < Terrain.GetWorldSize() - 1 && XY.Y < Terrain.GetWorldSize() - 1)
    {
        const auto Released = Cargo.Take(Amount);
        Terrain.Deposit(XY, Kind == EMudMachine::Excavator ? 1.65 : 2.35, Released);
        Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Point.Z, Terrain.Height(XY)) + 0.24),
            Released.Volume(), Released.DominantMaterial(), TipBed ? -GetActorForwardVector() : GetActorForwardVector());
        return Released.Volume();
    }
    return 0;
}

void AMudMachine::Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet, const MachineMotion::HydraulicInput& Mouse)
{
    if (RolledSide != 0)
        return;
    bBucketObstructed = false;
    const FVector PreviousTip = ToolPoint();
    const auto Move = [Dt](double Now, double Target, double Rate, double Response = 0.13)
    {
        return MachineMotion::Approach(Now, Target, Rate, Dt, Response);
    };
    const auto Valve = [Dt](double& Rate, double TargetRate)
    {
        return MachineMotion::HydraulicTravel(Rate, TargetRate, Dt, 0.10);
    };
    if (Kind == EMudMachine::Excavator)
    {
        const double BoomInput = FMath::Clamp(Mouse.Boom + (bOtherGroup ? 0 : AxisA), -1.0, 1.0);
        const double StickInput = FMath::Clamp(Mouse.Stick + (bOtherGroup ? 0 : AxisB), -1.0, 1.0);
        const double CurlInput = FMath::Clamp(Mouse.Curl + (bOtherGroup ? AxisA : 0), -1.0, 1.0);
        const double SwingInput = FMath::Clamp(Mouse.Swing + (bOtherGroup ? AxisB : 0), -1.0, 1.0);
        const bool bManual = FMath::Abs(BoomInput) + FMath::Abs(StickInput) +
            FMath::Abs(CurlInput) + FMath::Abs(SwingInput) > 0.1;
        if (bManual)
        {
            DigPhase = EDigPhase::Idle;
            bDigMissed = false;
        }
        else if (FMath::Abs(Speed) > 0.55 && (DigPhase == EDigPhase::Approach || DigPhase == EDigPhase::Scoop))
            DigPhase = EDigPhase::Lift;
        if (FMath::Abs(CurlInput) > 0.08)
            bUnload = false;

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
            const double Floor = DigSurface - FMath::Lerp(0.13, 0.64, FMath::Sin(Travel * PI * 0.80));
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
        const double LiftInput = FMath::Clamp(AxisA + Mouse.Boom, -1.0, 1.0);
        const double CurlInput = FMath::Clamp(-AxisB + Mouse.Curl, -1.0, 1.0);
        if (FMath::Abs(CurlInput) > 0.08)
            bUnload = false;
        Boom = FMath::Clamp(Boom + Valve(ValveA, LiftInput * 33), -22.0, 64.0);
        if (bUnload)
        {
            ValveCurl = 0;
            Curl = Move(Curl, -55, 60);
        }
        else
            Curl = FMath::Clamp(Curl + Valve(ValveCurl, CurlInput * 48), -55.0, 38.0);
    }
    else if (Kind == EMudMachine::Dozer)
    {
        const double BladeInput = FMath::Clamp(AxisA + Mouse.Boom, -1.0, 1.0);
        if (bTool && FMath::Abs(BladeInput) > 0.08)
        {
            // Hand the blade back to the operator at its current height.
            Blade += BladeLift - 0.67;
            BladeLift = 0.67;
            bTool = false;
        }
        Blade = FMath::Clamp(Blade + Valve(ValveA, BladeInput * 0.7), -1.0, 1.0);
        double Target = 0.67;
        if (bTool)
        {
            const double VerticalTravel = FMath::Max(0.65, GetActorUpVector().Z);
            const double Depth = bGrade ? 0.07 : 0.28;
            Target = FMath::Clamp((-BladeClearance(Terrain, 0) - Depth) / VerticalTravel, -0.38, 1.05);
        }
        BladeLift = Move(BladeLift, Target, 1.85, 0.13);
    }
    if (TipBed)
    {
        if (FMath::Abs(AxisA) > 0.08)
            bUnload = false;
        Bed.Step(AxisA, bUnload, Dt);
    }
    UpdatePose(Terrain);

    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    const double Ground = Terrain.Height(XY);
    bBucketGround = Kind == EMudMachine::Dozer ? BladeClearance(Terrain, BladeLift + Blade) <= 0.08 :
        Point.Z <= Ground + 0.16;
    double Moved = 0;
    const bool bLoaderSpilling = Kind == EMudMachine::Loader && Curl <= -32 && Point.Z > Ground + 0.14;
    const bool bWasUnloading = bUnload || (TipBed && Bed.CanSpill(false)) || bLoaderSpilling;
    if (bWasUnloading && DigPhase == EDigPhase::Idle)
    {
        bool bCanTip = TipBed ? Bed.CanSpill(bUnload) : Kind != EMudMachine::Loader || bLoaderSpilling;
        if (Kind == EMudMachine::Excavator)
        {
            const FVector Mouth = Tool->GetComponentTransform().TransformVectorNoScale(FVector(-0.68, 0, 0.88));
            bCanTip = Mouth.Z < -0.19 && Point.Z > Ground + 0.10;
            bBucketObstructed = !bCanTip && Curl < DumpCurl + 1;
        }
        if (bCanTip)
            Moved = EmptyBucket(Dt * (Kind == EMudMachine::Excavator ? 4.8 : 8.0), Terrain, Fleet);
        if (Cargo.Volume() < Epsilon)
        {
            bUnload = false;
            if (Kind == EMudMachine::Dozer)
            {
                bTool = false;
                BladeLift += Blade;
                Blade = 0;
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
            const FVector2D Forward = Horizontal(Turret->GetForwardVector()).GetSafeNormal();
            const FVector2D Across(-Forward.Y, Forward.X);
            const double Pull = FVector2D::DotProduct(Horizontal(PreviousTip - Point), Forward);
            if (Pull > 0 && (bBucketGround || DigPhase == EDigPhase::Scoop))
            {
                const double Amount = FMath::Min(FMath::Min(Dt * 3.6, Pull * 1.9), Capacity() - Cargo.Volume());
                const double Floor = DigPhase == EDigPhase::Scoop ? FMath::Max(Point.Z, DigSurface - 0.67) : Point.Z;
                const Load Cut = Terrain.CutPass(XY - Forward * 0.22, Across, FVector2D(1.18, 1.18),
                    Amount, Floor - 0.07);
                Moved = Cut.Volume();
                Cargo.Receive(Cut);
            }
            if (DigPhase == EDigPhase::Scoop && (DigTime >= 1.55 || Cargo.Volume() >= Capacity() - 0.01))
                DigPhase = EDigPhase::Lift;
        }
    }
    else if (Kind == EMudMachine::Loader && bBucketGround && Speed > 0.08 && Curl >= -20 && Curl <= 12)
    {
        const FVector2D Across = Horizontal(GetActorRightVector()).GetSafeNormal();
        const double Amount = FMath::Min(Dt * FMath::Min(4.2, Speed * 3.0), Capacity() - Cargo.Volume());
        const Load Cut = Terrain.CutPass(XY, Across, FVector2D(1.26, 0.95), Amount, Point.Z - 0.035);
        Cargo.Receive(Cut);
        Moved = Cut.Volume();
    }
    else if (Kind == EMudMachine::Dozer && bBucketGround && Speed > 0.08)
    {
        const FVector Right = GetActorRightVector();
        const FVector2D Across = Horizontal(Right).GetSafeNormal();
        if (bGrade)
            Moved = Terrain.Grade(XY, Across, FVector2D(2.10, 1.25), Speed * 1.8 * Dt);
        else
        {
            const double Amount = Dt * FMath::Min(5.0, Speed * 3.8);
            const double CrossSlope = Right.Z / Horizontal(Right).Size();
            Load Cut = Terrain.CutPass(XY, Across, FVector2D(2.10, 1.1), Amount, Point.Z - 0.05, CrossSlope);
            Moved = Cut.Volume();
            Cargo.Receive(Cut.Take(Capacity() - Cargo.Volume()));
            if (Cut.Volume() > 0)
            {
                const FVector2D Forward(Across.Y, -Across.X);
                const FVector2D SidePile = XY - Forward * 0.65 + Across * 2.55;
                const double Limit = Terrain.GetWorldSize() - 1;
                Terrain.Deposit({FMath::Clamp(SidePile.X, 1.0, Limit), FMath::Clamp(SidePile.Y, 1.0, Limit)}, 1.25, Cut);
            }
        }
    }
    if (Moved > Epsilon && !bWasUnloading)
        Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Ground, Point.Z)), Moved,
            Cargo.Volume() > Epsilon ? Cargo.DominantMaterial() : Terrain.Surface(XY), GetActorForwardVector());
    MaterialMoved = FMath::Max(Moved / Dt, MaterialMoved - 5 * Dt);
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

void AMudMachine::UpdatePose(const AMudTerrain& Terrain)
{
    const FVector2D Forward = Horizontal(FRotator(0, Heading, 0).Vector());
    const FVector2D Right(-Forward.Y, Forward.X);
    const double Wheelbase = Kind == EMudMachine::Truck || Kind == EMudMachine::Dumper ? 2.3 : 1.7;
    const double Front = Terrain.Height(Position + Forward * Wheelbase);
    const double Back = Terrain.Height(Position - Forward * Wheelbase);
    const double Left = Terrain.Height(Position - Right * 1.1);
    const double RightHeight = Terrain.Height(Position + Right * 1.1);
    const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Front - Back, Wheelbase * 2));
    const double Roll = RolledSide != 0 ? RolledSide * 78.0 :
        FMath::RadiansToDegrees(FMath::Atan2(Left - RightHeight, 2.2));
    const double Sink = Terrain.Wetness(Position) * (1.0 - Terrain.Grip(Position)) * (0.10 + 0.25 * Cargo.Volume() / Capacity());
    const double Z = (Terrain.Height(Position) * 0.5 + (Front + Back + Left + RightHeight) * 0.125) - Sink;
    SetActorLocationAndRotation(FVector(Position.X * 100, Position.Y * 100, Z * 100 + (RolledSide != 0 ? 115 : 0)),
        FRotator(Pitch, Heading, Roll));
    if (Kind == EMudMachine::Excavator)
    {
        Turret->SetRelativeRotation(FRotator(0, Swing, 0));
        BoomPivot->SetRelativeRotation(FRotator(Boom, 0, 0));
        StickPivot->SetRelativeRotation(FRotator(Stick, 0, 0));
        Tool->SetRelativeRotation(FRotator(-Curl, 0, 0));
        UpdateBucketLinkage();
    }
    if (Kind == EMudMachine::Dozer)
        Tool->SetRelativeLocation(BladeMount + FVector(0, 0, (BladeLift + Blade) * 100));
    if (Kind == EMudMachine::Loader)
    {
        BoomPivot->SetRelativeRotation(FRotator(Boom, 0, 0));
        // The existing bucket faces backwards. Yaw it forward and compensate for the lift arm.
        Tool->SetRelativeRotation(FRotator(Boom - Curl, 180, 0));
    }
    if (TipBed)
        TipBed->SetRelativeRotation(FRotator(Bed.Angle, 0, 0));
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
    Payload->SetVisibility(Cargo.Volume() > 0.02);
    const double Fraction = FMath::Clamp(Cargo.Volume() / Capacity(), 0.0, 1.0);
    if (Cargo.Volume() > Epsilon && CargoVisual != Cargo.DominantMaterial())
    {
        CargoVisual = Cargo.DominantMaterial();
        Payload->SetMaterial(0, PaintMaterial(CargoPaint[static_cast<uint8>(CargoVisual)]));
    }
    if (TipBed)
    {
        Payload->SetRelativeScale3D(FVector(3.60, 2.10, FMath::Lerp(0.05, 1.42, Fraction)));
    }
    if (Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer || Kind == EMudMachine::Loader)
    {
        const FVector Span = Kind == EMudMachine::Loader ? FVector(0.85, 2.15, 0.72) :
            (Kind == EMudMachine::Excavator ? FVector(0.73, 1.13, 0.72) : FVector(1.1, 2.9, 0.57));
        Payload->SetRelativeScale3D(Span * FVector(1, 1, FMath::Lerp(0.08, 1.0, Fraction)));
    }
    if (Kind == EMudMachine::Dozer && Fraction > 0)
    {
        FVector Heap = Tool->GetComponentTransform().TransformPosition(FVector(60, 0, 0));
        Heap.Z = Terrain.Height(Horizontal(Heap) / 100) * 100 - 5;
        Payload->SetWorldLocation(Heap);
    }
}

FString AMudMachine::ToolDescription() const
{
    if (RolledSide != 0)
        return TEXT("Rolled over | Backspace to recover");
    if (Grip < 0.18 && Cargo.Volume() > Capacity() * 0.5)
        return TEXT("Bogged down | Tip load or recover");
    switch (Kind)
    {
    case EMudMachine::Dozer:
        if (bUnload)
            return TEXT("Spreading the load");
        if (!IsBladeWorking())
            return TEXT("Blade raised");
        if (!bBucketGround)
            return TEXT("Down arrow | Lower blade");
        if (!bGrade && Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Full blade | spilling right | I to spread");
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
        return bOtherGroup ? TEXT("J to curl | N/M to swing") : TEXT("J/K boom | N/M stick");
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
