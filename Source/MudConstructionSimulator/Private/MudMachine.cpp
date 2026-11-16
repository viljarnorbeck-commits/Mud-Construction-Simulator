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
    { TEXT("Bulldozer"), TEXT("Ochre"), 6, 4.2, 19, 68, 1.30, 1.45 },
    { TEXT("Excavator"), TEXT("Amber"), 2.4, 3.6, 24, 48, 1.30, 1.45 },
    { TEXT("Articulated dumper"), TEXT("Ochre"), 16, 10.5, 17, 32, 1.06, 1.48 },
    { TEXT("Off-road truck"), TEXT("Teal"), 24, 13.5, 15, 29, 1.05, 1.46 }
};

const FSpec& Spec(EMudMachine Kind) { return Specs[static_cast<uint8>(Kind)]; }

constexpr const TCHAR* CargoPaint[] = { TEXT("Loose"), TEXT("LooseGravel"), TEXT("LooseRock") };
const FVector BoomMount(15, 38, 84);
const FVector StickMount(344, 0, 0);
const FVector BucketMount(225, 0, 0);
const FVector BucketLip(91, 0, -53);
const FVector BladeLip(12, 0, -26.5);
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
    for (const double AxleX : {-230.0, -69.0, 186.0})
    {
        for (const double Side : {-1.0, 1.0})
        {
            constexpr double OffsetM = 1.22;
            auto* Axle = RearFrame && AxleX < 0 ? RearFrame.Get() : Root;
            auto* Mount = Pivot(Axle, FVector(AxleX, Side * OffsetM * 100, TyreRadiusM * 100));
            auto* Knuckle = Kind == EMudMachine::Truck && AxleX > 0 ? Mount : nullptr;
            auto* Hub = Knuckle ? Pivot(Knuckle, FVector::ZeroVector) : Mount;
            Wheels.Add({Hub, Knuckle, Side * OffsetM, TyreRadiusM});
            Part(TEXT("Cylinder"), TEXT("Tyre"), Hub, {}, {1.23, 1.23, 0.47}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Tyre"), Hub, {0, Side * 20, 0}, {1.11, 1.11, 0.135}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Steel"), Hub, {0, Side * 27, 0}, {0.73, 0.73, 0.066}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Chassis"), Hub, {0, Side * 31, 0}, {0.55, 0.55, 0.028}, FRotator(0, 0, 90));
            Part(TEXT("Cylinder"), TEXT("Steel"), Hub, {0, Side * 34, 0}, {0.27, 0.27, 0.085}, FRotator(0, 0, 90));

            TArray<FTransform> Lugs;
            for (const double Row : {-1.0, 1.0})
            {
                for (int32 I = 0; I < 20; ++I)
                {
                    const double Degrees = I * 18 + Row * 2.8;
                    const double Angle = FMath::DegreesToRadians(Degrees);
                    const FVector LugOffset(FMath::Cos(Angle) * 61.5, Row * 13.2, FMath::Sin(Angle) * 61.5);
                    Lugs.Emplace(FRotator(Degrees, Row * 12, 0), LugOffset, FVector(0.084, 0.262, 0.123));
                }
            }
            RepeatedBoxes(TEXT("Tyre"), Hub, Lugs);
            TArray<FTransform> Bolts;
            for (int32 I = 0; I < 6; ++I)
            {
                const double Angle = I * RunningGear::Pi / 3;
                Bolts.Emplace(FRotator(60 * I, 0, 0), FVector(22 * FMath::Cos(Angle), Side * 32.6,
                    22 * FMath::Sin(Angle)), FVector(0.058, 0.048, 0.058));
            }
            RepeatedBoxes(TEXT("Steel"), Hub, Bolts);
            Part(TEXT("Cube"), TEXT("Steel"), Hub, {0, Side * 27.3, 48}, {0.081, 0.012, 0.045});
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
        UpdateTrack(Track);
        Track.Shoes = RepeatedBoxes(TEXT("WornSteel"), Root, Track.Transforms);
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
        Track.Transforms[I] = FTransform(FRotator(Shoe.Pitch, 0, 0),
            FVector(Shoe.Ahead * 100, Track.LateralM * 100, (Shoe.Height + BeltHeightM) * 100),
            FVector(Spacing * 0.89, 0.65, 0.073));
    }
    if (Track.Shoes)
        Track.Shoes->BatchUpdateInstancesTransforms(0, Track.Transforms, false, true, true);
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

void AMudMachine::BuildBucket()
{
    constexpr double HalfWidth = 68;
    const FVector2D Profile[] = {{-24, 16}, {-22, -12}, {-16, -30}, {-1, -45}, {24, -53}, {58, -53}, {87, -48}};
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UV;
    TArray<int32> Indices;
    const auto Face = [&](std::initializer_list<FVector> Corners)
    {
        const int32 Base = Vertices.Num();
        const FVector* Points = Corners.begin();
        const FVector Normal = FVector::CrossProduct(Points[2] - Points[0], Points[1] - Points[0]).GetSafeNormal();
        for (const FVector& Point : Corners)
        {
            Vertices.Add(Point);
            Normals.Add(Normal);
            UV.Emplace(Point.X * 0.01, (Point.Y + Point.Z) * 0.01);
        }
        Indices.Append({Base, Base + 1, Base + 2});
        if (Corners.size() == 4)
            Indices.Append({Base, Base + 2, Base + 3});
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
    Shell->SetupAttachment(Tool);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetMaterial(0, PaintMaterial(TEXT("WornSteel")));
    Shell->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UV, {}, {}, false, false);
    Shell->RegisterComponent();
    for (int32 I = -2; I <= 2; ++I)
        Part(TEXT("Cube"), TEXT("CuttingEdge"), Tool, {93, I * 25.0, -50}, {0.28, 0.14, 0.12}, FRotator(-12, 0, 0));
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
    if (Kind == EMudMachine::Dumper)
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
        AddHydraulic(Turret, {-24, 38, 48}, BoomPivot, {187, 0, -9}, 116, 0.23);
        AddHydraulic(BoomPivot, {231, 0, 27}, StickPivot, {112, 0, 19}, 126, 0.17);
        AddHydraulic(StickPivot, {147, 0, 23}, Tool, {20, 0, 2}, 56, 0.14);
        BuildBucket();
        Payload = BuildHeap(Tool, {42, 0, -43}, {0.73, 1.13, 0.72});
    }
    else if (Kind == EMudMachine::Dozer)
    {
        BuildDozerBody(Paint);
        Tool = Pivot(Root, {289, 0, 42});
        for (const double Side : {-1.0, 1.0})
            AddHydraulic(Root, {83, Side * 103, 93}, Tool, {-17, Side * 113, 23}, 124, 0.23);
        BuildBlade();
        Payload = BuildHeap(Tool, {60, 0, 1}, {1.1, 2.9, 0.57});
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
    const FVector Lip = Kind == EMudMachine::Excavator ? BucketLip :
        (Kind == EMudMachine::Dozer ? BladeLip : FVector::ZeroVector);
    return Tool->GetComponentTransform().TransformPosition(Lip) / 100.0;
}

FVector AMudMachine::BucketPoint(double BoomAngle, double StickAngle, double CurlAngle) const
{
    const FVector BucketTip = BucketMount + FRotator(CurlAngle, 0, 0).RotateVector(BucketLip);
    const FVector StickTip = StickMount + FRotator(StickAngle, 0, 0).RotateVector(BucketTip);
    return Turret->GetComponentTransform().TransformPosition(
        BoomMount + FRotator(BoomAngle, 0, 0).RotateVector(StickTip)) / 100.0;
}

double AMudMachine::FindDigAngle(const AMudTerrain& Terrain) const
{
    double Low = -32.0, High = 48.0;
    for (int32 I = 0; I < 12; ++I)
    {
        const double Angle = (Low + High) * 0.5;
        const FVector Lip = BucketPoint(Angle, Stick, Curl);
        if (Lip.Z < Terrain.Height(Horizontal(Lip)) - 0.26)
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
    const double MaxSpeed = Kind == EMudMachine::Dozer && bTool ? 2.35 : Info.Speed;
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
    if (Kind == EMudMachine::Dozer && bTool && Speed > 0)
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
}

void AMudMachine::ToggleTool()
{
    if (Kind == EMudMachine::Dozer)
        bTool = !bTool;
    else if (Kind == EMudMachine::Excavator && !bUnload && (bTool || Cargo.Volume() < Capacity() - 0.01))
    {
        bTool = !bTool;
        bAssistDig = bTool;
        bLiftBucket = !bTool;
        DigStart = Cargo.Volume();
        DigIdle = 0;
    }
}

void AMudMachine::ChangeGroup()
{
    if (Kind == EMudMachine::Dozer)
        bGrade = !bGrade;
    else if (Kind == EMudMachine::Excavator)
    {
        bOtherGroup = !bOtherGroup;
        ValveA = ValveB = 0;
    }
}

void AMudMachine::ToggleUnload()
{
    bUnload = !bUnload && Cargo.Volume() > Epsilon;
    if (Kind == EMudMachine::Excavator && bUnload)
    {
        bLiftBucket |= bTool;
        bTool = bAssistDig = false;
    }
}

void AMudMachine::Halt()
{
    Speed = DriveCommand = SteeringCommand = ValveA = ValveB = 0;
}

void AMudMachine::StopWork()
{
    Halt();
    bTool = bUnload = bAssistDig = bLiftBucket = bBucketObstructed = false;
    Articulation = 0;
    if (RearFrame)
        RearFrame->SetRelativeRotation(FRotator::ZeroRotator);
    AnimateRunningGear(0, 0, 0);
    BedAngle = 0;
    BladeLift = 0.67;
    MaterialMoved = 0;
}

double AMudMachine::EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    if (Kind == EMudMachine::Excavator)
    {
        for (auto* Other : Fleet)
        {
            if (!Other->TipBed || Other->bUnload || Other->RolledSide != 0)
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
            Terrain.ShowSpill(Point, Released.Volume());
            Other->UpdatePose(Terrain);
            return Released.Volume();
        }
    }
    if (XY.X > 1 && XY.Y > 1 && XY.X < Terrain.GetWorldSize() - 1 && XY.Y < Terrain.GetWorldSize() - 1)
    {
        const auto Released = Cargo.Take(Amount);
        Terrain.Deposit(XY, Kind == EMudMachine::Excavator ? 1.65 : 2.35, Released);
        Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Point.Z, Terrain.Height(XY)) + 0.24),
            Released.Volume());
        return Released.Volume();
    }
    return 0;
}

void AMudMachine::Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet, double DriveIntent)
{
    if (RolledSide != 0)
        return;
    bBucketObstructed = false;
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
        if (FMath::Abs(AxisA) + FMath::Abs(AxisB) > 0.1)
            bAssistDig = bLiftBucket = false;
        if (bAssistDig)
        {
            ValveA = ValveB = 0;
            Stick = Move(Stick, -36, 42);
            Curl = Move(Curl, FMath::Lerp(-23.0, 28.0, (Cargo.Volume() - DigStart) / Capacity()), 65);
            Boom = Move(Boom, FindDigAngle(Terrain), 34);
        }
        else if (bLiftBucket)
        {
            ValveA = ValveB = 0;
            Boom = Move(Boom, 29, 42);
            Stick = Move(Stick, -32, 42);
            Curl = Move(Curl, 31, 58);
            bLiftBucket = FMath::Abs(Boom - 29) > 0.15 || FMath::Abs(Stick + 32) > 0.15 || FMath::Abs(Curl - 31) > 0.15;
        }
        else if (bOtherGroup)
        {
            Curl = FMath::Clamp(Curl + Valve(ValveA, AxisA * 54), -95.0, 78.0);
            Swing = FRotator::NormalizeAxis(Swing + MachineMotion::HydraulicTravel(ValveB, AxisB * 52, Dt, 0.18));
        }
        else
        {
            Boom = FMath::Clamp(Boom + Valve(ValveA, AxisA * 29), -32.0, 61.0);
            Stick = FMath::Clamp(Stick + Valve(ValveB, AxisB * 39), -107.0, 24.0);
        }
        if (bUnload && !bLiftBucket && FMath::Abs(AxisA) + FMath::Abs(AxisB) <= 0.1)
            Curl = Move(Curl, -42, 54);
    }
    else if (Kind == EMudMachine::Dozer)
    {
        Blade = FMath::Clamp(Blade + Valve(ValveA, AxisA * 0.7), -1.0, 1.0);
        BladeLift = Move(BladeLift, bTool ? 0.06 : 0.67, 1.5, 0.13);
    }
    if (TipBed)
    {
        BedAngle = Move(BedAngle, bUnload ? 51 : 0, bUnload ? 42 : 58, 0.15);
        if (!bUnload && BedAngle < 0.12)
            BedAngle = 0;
    }
    UpdatePose(Terrain);

    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    const double Ground = Terrain.Height(XY);
    bBucketGround = Point.Z <= Ground + 0.16;
    double Moved = 0;
    if (bUnload && !bLiftBucket && (!TipBed || BedAngle >= 25) &&
        (Kind != EMudMachine::Excavator || Curl <= 8))
    {
        Moved = EmptyBucket(Dt * (Kind == EMudMachine::Excavator ? 4.8 : 8.0), Terrain, Fleet);
        if (Cargo.Volume() < Epsilon)
        {
            bUnload = false;
            if (Kind == EMudMachine::Dozer)
                bTool = false;
        }
    }
    else if (Kind == EMudMachine::Excavator && bTool)
    {
        if (bBucketGround && FMath::Abs(Speed) < 0.9)
        {
            const Load Cut = Terrain.Excavate(XY, 1.32, FMath::Min(Dt * 2.7, Capacity() - Cargo.Volume()), Point.Z - 0.15);
            Moved = Cut.Volume();
            Cargo.Receive(Cut);
            Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Ground, Point.Z)), Moved);
        }
        if (bAssistDig)
        {
            DigIdle = Moved > Epsilon ? 0 : DigIdle + Dt;
            if (Cargo.Volume() >= Capacity() - 0.01 || DigIdle > 2.4)
            {
                bTool = bAssistDig = false;
                bLiftBucket = true;
            }
        }
    }
    else if (Kind == EMudMachine::Dozer && bTool && (Speed > 0.10 || DriveIntent > 0.12) && bBucketGround)
    {
        if (bGrade)
            Moved = Terrain.Grade(XY, 2.05, 2.1 * Dt);
        else
        {
            const Load Cut = Terrain.Excavate(XY, 1.78, FMath::Min(Dt * 5, Capacity() - Cargo.Volume()), Point.Z);
            Moved = Cut.Volume();
            Cargo.Receive(Cut);
        }
        Terrain.ShowSpill(FVector(Point.X, Point.Y, FMath::Max(Ground, Point.Z)), Moved);
    }
    MaterialMoved = FMath::Max(Moved / Dt, MaterialMoved - 5 * Dt);
    if (Moved > 0)
        UpdatePose(Terrain);
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
    if (BoomPivot)
    {
        Turret->SetRelativeRotation(FRotator(0, Swing, 0));
        BoomPivot->SetRelativeRotation(FRotator(Boom, 0, 0));
        StickPivot->SetRelativeRotation(FRotator(Stick, 0, 0));
        Tool->SetRelativeRotation(FRotator(Curl, 0, 0));
    }
    if (Kind == EMudMachine::Dozer)
        Tool->SetRelativeLocation(FVector(289, 0, (BladeLift + Blade) * 100));
    if (TipBed)
        TipBed->SetRelativeRotation(FRotator(BedAngle, 0, 0));
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
    if (Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer)
    {
        const FVector Span = Kind == EMudMachine::Excavator ? FVector(0.73, 1.13, 0.72) : FVector(1.1, 2.9, 0.57);
        Payload->SetRelativeScale3D(Span * FVector(1, 1, FMath::Lerp(0.08, 1.0, Fraction)));
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
        if (!bTool)
            return TEXT("Blade raised");
        if (!bGrade && Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Blade full | I to spread");
        if (!bBucketGround)
            return TEXT("Lower the blade with K");
        return IsMovingEarth() ? (bGrade ? TEXT("Grading the surface") : TEXT("Pushing soil")) :
            (bGrade ? TEXT("Blade down | W to grade") : TEXT("Blade down | W to cut"));
    case EMudMachine::Excavator:
        if (bBucketObstructed)
            return TEXT("Raise bucket or check bed capacity");
        if (bLiftBucket)
            return TEXT("Lifting bucket");
        if (bUnload)
            return TEXT("Emptying bucket");
        if (Cargo.Volume() >= Capacity() - 0.01)
            return TEXT("Bucket full | I to tip");
        if (bTool)
            return IsMovingEarth() ? TEXT("Digging") : (bAssistDig ? TEXT("Lowering to the surface") : TEXT("Move teeth into the ground"));
        return bOtherGroup ? TEXT("Curl / swing") : TEXT("Boom / stick");
    default:
        return bUnload ? TEXT("Tipping") : (IsTipping() ? TEXT("Lowering bed") : TEXT("Hauling"));
    }
}
