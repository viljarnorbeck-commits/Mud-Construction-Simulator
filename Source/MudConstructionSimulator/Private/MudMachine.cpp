#include "MudMachine.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"

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

void AMudMachine::Initialize(EMudMachine Type, FVector2D Location, AMudTerrain& Terrain)
{
    Kind = Type;
    Position = Location;
    Heading = 0;
    const auto Pivot = [this](USceneComponent* Parent, FVector Offset)
    {
        auto* Joint = NewObject<USceneComponent>(this);
        Joint->SetupAttachment(Parent);
        Joint->SetRelativeLocation(Offset);
        Joint->RegisterComponent();
        return Joint;
    };
    const auto Box = [this](const TCHAR* Material, USceneComponent* Parent, FVector Offset, FVector Size)
    {
        return Part(TEXT("Cube"), Material, Parent, Offset, Size);
    };
    const auto RepeatedBoxes = [this](const TCHAR* Material, USceneComponent* Parent, const TArray<FTransform>& Transforms)
    {
        auto* Detail = NewObject<UInstancedStaticMeshComponent>(this);
        Detail->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Detail->SetMaterial(0, PaintMaterial(Material));
        Detail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Detail->SetupAttachment(Parent);
        Detail->RegisterComponent();
        Detail->AddInstances(Transforms, false, false, false);
    };
    const TCHAR* Paint = Spec(Kind).Paint;
    auto* Root = GetRootComponent();
    const bool bTracked = Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer;
    if (Kind == EMudMachine::Dumper)
        RearFrame = Pivot(Root, {0, 0, 0});
    if (bTracked)
    {
        Box(TEXT("Rubber"), Root, {0, -114, 54}, {4.55, 0.62, 0.93});
        Box(TEXT("Rubber"), Root, {0, 114, 54}, {4.55, 0.62, 0.93});
        TArray<FTransform> Shoes;
        for (double Side : {-1.0, 1.0})
        {
            for (int32 I = -4; I <= 4; ++I)
                Shoes.Emplace(FRotator::ZeroRotator, FVector(I * 49.0, Side * 114, 102), FVector(0.095, 0.66, 0.06));
            for (int32 I = -2; I <= 2; ++I)
                Part(TEXT("Cylinder"), TEXT("Steel"), Root, {I * 74.0, Side * 146.0, 55.0},
                    {0.62, 0.62, 0.11}, FRotator(0, 0, 90));
        }
        RepeatedBoxes(TEXT("Steel"), Root, Shoes);
    }
    else
    {
        for (double X : {-230.0, -69.0, 186.0})
            for (double Y : {-122.0, 122.0})
            {
                auto* Hub = Pivot(RearFrame && X < 0 ? RearFrame.Get() : Root, {X, Y, 66});
                Wheels.Add(Hub);
                Part(TEXT("Cylinder"), TEXT("Rubber"), Hub, {0, 0, 0}, {1.23, 1.23, 0.54}, FRotator(0, 0, 90));
                Part(TEXT("Cylinder"), TEXT("Steel"), Hub, {0, Y > 0 ? 28.0 : -28.0, 0},
                    {0.63, 0.63, 0.07}, FRotator(0, 0, 90));
                TArray<FTransform> Lugs;
                for (int32 Row = 0; Row < 2; ++Row)
                {
                    const double Sign = Row == 0 ? -1.0 : 1.0;
                    for (int32 I = 0; I < 20; ++I)
                    {
                        const double Degrees = I * 18 + Sign * 2.8;
                        const double Angle = FMath::DegreesToRadians(Degrees);
                        const FVector TreadOffset(FMath::Cos(Angle) * 61.5, Sign * 13.2, FMath::Sin(Angle) * 61.5);
                        Lugs.Emplace(FRotator(Degrees, 0, 0), TreadOffset, FVector(0.084, 0.262, 0.123));
                    }
                }
                RepeatedBoxes(TEXT("Rubber"), Hub, Lugs);
            }
    }
    if (RearFrame)
    {
        Box(TEXT("Steel"), RearFrame, {-157, 0, 99}, {3.14, 1.84, 0.39});
        Box(TEXT("Steel"), Root, {157, 0, 99}, {3.14, 1.84, 0.39});
    }
    else
    {
        Box(TEXT("Steel"), Root, {0, 0, 99}, {bTracked ? 4.1 : 6.5, 1.84, 0.39});
    }
    Turret = Pivot(Root, {0, 0, 119});
    if (Kind == EMudMachine::Excavator)
    {
        Box(Paint, Turret, {-67, 0, 58}, {3.09, 2.26, 1.14});
        Box(TEXT("Glass"), Turret, {20, -62, 159}, {1.57, 0.96, 1.27});
        Box(Paint, Turret, {20, -62, 232}, {1.73, 1.09, 0.15});
        Box(Paint, Turret, {-49, -112, 153}, {0.13, 0.09, 1.38});
        Box(TEXT("Headlight"), Turret, {102, -88, 226}, {0.10, 0.32, 0.16});
        Box(TEXT("Steel"), Turret, {-167, 71, 129}, {0.24, 0.26, 0.64});
        BoomPivot = Pivot(Turret, {15, 38, 84});
        Box(Paint, BoomPivot, {172, 0, 0}, {3.48, 0.36, 0.40});
        Box(TEXT("Steel"), BoomPivot, {144, 0, -30}, {2.32, 0.18, 0.15});
        StickPivot = Pivot(BoomPivot, {344, 0, 0});
        Box(Paint, StickPivot, {112, 0, 0}, {2.24, 0.26, 0.30});
        Tool = Pivot(StickPivot, {225, 0, 0});
        Box(TEXT("Steel"), Tool, {39, 0, -40}, {0.90, 1.36, 0.27});
        Box(TEXT("Steel"), Tool, {-2, 0, -9}, {0.25, 1.36, 0.73});
        for (int32 I = -2; I <= 2; ++I)
            Box(TEXT("Steel"), Tool, {90, I * 25.0, -46}, {0.24, 0.12, 0.12});
        Payload = BuildHeap(Tool, {42, 0, -31}, {0.73, 1.13, 0.54});
    }
    else if (Kind == EMudMachine::Dozer)
    {
        Box(Paint, Turret, {42, 0, 54}, {2.77, 1.57, 0.95});
        Box(TEXT("Glass"), Turret, {-106, 0, 125}, {1.17, 1.47, 1.49});
        Box(Paint, Turret, {-106, 0, 207}, {1.34, 1.63, 0.17});
        for (double Side : {-1.0, 1.0})
        {
            Box(Paint, Turret, {-57, Side * 73.0, 126}, {0.10, 0.11, 1.47});
            Box(TEXT("Headlight"), Turret, {-41, Side * 57.0, 196}, {0.12, 0.27, 0.14});
            Part(TEXT("Cube"), TEXT("Steel"), Root, {165, Side * 105.0, 75}, {2.05, 0.24, 0.22}, FRotator(-11, 0, 0));
        }
        Tool = Pivot(Root, {289, 0, 42});
        Box(TEXT("Steel"), Tool, {0, 0, 19}, {0.34, 3.45, 0.91});
        Box(Paint, Tool, {-31, 0, 34}, {0.40, 3.34, 0.44});
        Payload = BuildHeap(Tool, {60, 0, 1}, {1.1, 2.9, 0.57});
    }
    else
    {
        Box(Paint, Root, {213, 0, 170}, {1.93, 2.33, 1.41});
        Box(TEXT("Glass"), Root, {220, 0, 256}, {1.54, 2.12, 0.84});
        Box(Paint, Root, {220, 0, 306}, {1.74, 2.29, 0.15});
        Box(TEXT("Steel"), Root, {319, 0, 101}, {0.18, 2.63, 0.36});
        Box(TEXT("Steel"), Root, {310, 0, 175}, {0.11, 1.48, 0.56});
        TArray<FTransform> Grille;
        for (int32 I = 0; I < 6; ++I)
            Grille.Emplace(FRotator::ZeroRotator, FVector(317, 0, 154 + I * 8.0), FVector(0.035, 1.30, 0.022));
        RepeatedBoxes(TEXT("Rubber"), Root, Grille);
        for (double Side : {-1.0, 1.0})
        {
            Box(Paint, Root, {232, Side * 106.0, 256}, {0.17, 0.12, 0.88});
            Box(TEXT("Headlight"), Root, {313, Side * 98.0, 158}, {0.13, 0.42, 0.20});
            Box(TEXT("Steel"), Root, {213, Side * 130.0, 109}, {0.84, 0.30, 0.12});
            Box(TEXT("Steel"), Root, {233, Side * 138.0, 247}, {0.15, 0.24, 0.35});
        }
        TipBed = Pivot(RearFrame ? RearFrame.Get() : Root, {-293, 0, 135});
        Box(Paint, TipBed, {189, 0, 0}, {3.82, 2.46, 0.21});
        for (double Side : {-1.0, 1.0})
            Box(Paint, TipBed, {189, Side * 119, 45}, {3.82, 0.18, 0.93});
        Box(Paint, TipBed, {373, 0, 45}, {0.18, 2.46, 0.93});
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
    const double X = Kind == EMudMachine::Excavator ? 74 : 0;
    const double Z = Kind == EMudMachine::Excavator ? -55 : 0;
    return Tool->GetComponentTransform().TransformPosition(FVector(X, 0, Z)) / 100.0;
}

FVector AMudMachine::CameraSeat() const
{
    switch (Kind)
    {
    case EMudMachine::Excavator: return Turret->GetComponentTransform().TransformPosition(FVector(68, -62, 190));
    case EMudMachine::Dozer: return GetActorTransform().TransformPosition(FVector(-80, 0, 270));
    default: return GetActorTransform().TransformPosition(FVector(259, 0, 282));
    }
}

void AMudMachine::Drive(double Dt, double Throttle, double Steering, bool bBrake, AMudTerrain& Terrain,
    const TArray<AMudMachine*>& Fleet)
{
    if (RolledSide != 0)
        return;
    const auto& Info = Spec(Kind);
    const double LoadFraction = Cargo.Volume() / Capacity();
    const double MassRatio = Info.Mass / (Info.Mass + Cargo.Mass());
    Grip = Terrain.Grip(Position);
    Water = Terrain.WaterDepth(Position);
    const bool bTracked = Kind == EMudMachine::Excavator || Kind == EMudMachine::Dozer;
    const double SteerSense = Speed < -0.15 && !bTracked ? -1 : 1;
    const double SteerPower = bTracked ? 1.0 : FMath::Clamp(FMath::Abs(Speed) * 1.2, 0.0, 1.0);
    Heading = FRotator::NormalizeAxis(Heading + Steering * Info.Turn * SteerSense * SteerPower * Dt);
    const FVector2D Forward = Horizontal(FRotator(0, Heading, 0).Vector());
    const double Grade = (Terrain.Height(Position + Forward * 2) - Terrain.Height(Position - Forward * 2)) * 0.25;
    const double EffectiveGrip = bTracked ? FMath::Min(1.0, Grip + 0.37) : Grip;
    const double WaterLimit = FordingLimit(Info.Clearance, LoadFraction);
    const double SpeedLimit = Info.Speed * FMath::Sqrt(MassRatio) * FMath::Lerp(0.08, 1.0, EffectiveGrip);
    const bool bTooSteep = Grade * Throttle > (0.23 + 0.47 * EffectiveGrip) * FMath::Lerp(0.62, 1.0, MassRatio);
    const bool bBogged = !bTracked && LoadFraction > 0.5 && EffectiveGrip < 0.18;
    const double Target = bTooSteep || bBrake || bBogged ? 0 : Throttle * SpeedLimit * (Throttle < 0 ? 0.48 : 1.0);
    Speed = FMath::FInterpConstantTo(Speed, Target, Dt, bBrake ? 9.0 : 3.4 * MassRatio + 1);
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
        Speed = 0;
    else
        Position = Next;
    const FVector2D Side(-Forward.Y, Forward.X);
    const double CrossSlope = (Terrain.Height(Position - Side * 1.1) - Terrain.Height(Position + Side * 1.1)) / 2.2;
    const double RollLimit = (bTracked ? 0.96 : 0.80) - LoadFraction * 0.23 - FMath::Abs(Steering * Speed) * 0.023;
    if (FMath::Abs(Speed) > 0.1 && FMath::Abs(CrossSlope) > RollLimit)
    {
        RolledSide = CrossSlope > 0 ? 1 : -1;
        Speed = 0;
        bUnload = false;
    }
    WheelAngle = FMath::Fmod(WheelAngle + Speed * Dt * 88.2, 360.0);
    for (const auto& Wheel : Wheels)
        Wheel->SetRelativeRotation(FRotator(WheelAngle, 0, 0));
    if (RearFrame)
    {
        Articulation = FMath::FInterpConstantTo(Articulation, -Steering * 28 * SteerPower, Dt, 54);
        RearFrame->SetRelativeRotation(FRotator(0, Articulation, 0));
    }
    UpdatePose(Terrain);
}

void AMudMachine::ToggleTool()
{
    if (Kind == EMudMachine::Dozer || Kind == EMudMachine::Excavator)
        bTool = !bTool;
}

void AMudMachine::ChangeGroup()
{
    if (Kind == EMudMachine::Dozer)
        bGrade = !bGrade;
    else if (Kind == EMudMachine::Excavator)
        bOtherGroup = !bOtherGroup;
}

void AMudMachine::ToggleUnload() { bUnload = !bUnload; }

void AMudMachine::EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    if (Kind == EMudMachine::Excavator)
    {
        for (auto* Other : Fleet)
        {
            if (Other->Kind != EMudMachine::Dumper && Other->Kind != EMudMachine::Truck)
                continue;
            const FVector Center = Other->GetActorTransform().TransformPosition(FVector(-100, 0, 205)) / 100.0;
            if (FVector2D::DistSquared(XY, Horizontal(Center)) > FMath::Square(2.25))
                continue;
            if (Point.Z < Center.Z - 0.30)
                return;
            Other->Cargo.Receive(Cargo.Take(FMath::Min(Amount, Other->Capacity() - Other->Cargo.Volume())));
            Other->UpdatePose(Terrain);
            return;
        }
    }
    if (XY.X > 1 && XY.Y > 1 && XY.X < Terrain.GetWorldSize() - 1 && XY.Y < Terrain.GetWorldSize() - 1)
    {
        const auto Released = Cargo.Take(Amount);
        Terrain.Deposit(XY, Kind == EMudMachine::Excavator ? 1.65 : 2.35, Released);
        Terrain.ShowSpill(Point + FVector(0, 0, 0.24), Released.Volume());
    }
}

void AMudMachine::Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    if (RolledSide != 0)
        return;
    if (Kind == EMudMachine::Excavator)
    {
        if (bOtherGroup)
        {
            Curl = FMath::Clamp(Curl + AxisA * Dt * 54, -95.0, 78.0);
            Swing = FRotator::NormalizeAxis(Swing + AxisB * Dt * 52);
        }
        else
        {
            Boom = FMath::Clamp(Boom + AxisA * Dt * 29, -32.0, 61.0);
            Stick = FMath::Clamp(Stick + AxisB * Dt * 39, -107.0, 24.0);
        }
    }
    if (Kind == EMudMachine::Dozer)
        Blade = FMath::Clamp(Blade + AxisA * Dt * 0.7, -1.0, 1.0);
    UpdatePose(Terrain);

    const FVector Point = ToolPoint();
    const FVector2D XY = Horizontal(Point);
    if (bUnload)
    {
        EmptyBucket(Dt * (Kind == EMudMachine::Excavator ? 4.8 : 6.5), Terrain, Fleet);
        if (Cargo.Volume() < Epsilon)
            bUnload = false;
    }
    else if (Kind == EMudMachine::Excavator && bTool)
    {
        const double Reach = Terrain.Height(XY) - Point.Z;
        if (Reach > -0.18)
            Cargo.Receive(Terrain.Excavate(XY, 1.15, FMath::Min(Dt * 2.1, Capacity() - Cargo.Volume()), Point.Z - 0.22));
    }
    else if (Kind == EMudMachine::Dozer && bTool && Speed > 0.16)
    {
        if (bGrade)
            Terrain.Grade(XY, 2.65, FMath::Min(0.65 * Dt, 1.0));
        else
        {
            const auto Material = Terrain.Excavate(XY, 1.8, FMath::Min(Dt * 4, Capacity() - Cargo.Volume()), Point.Z);
            Cargo.Receive(Material);
        }
    }
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
        Tool->SetRelativeLocation(FVector(289, 0, (bTool ? -16.0 : 56.0) + Blade * 100));
    if (TipBed)
        TipBed->SetRelativeRotation(FRotator(bUnload ? 48 : 0, 0, 0));
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
        const FVector Span = Kind == EMudMachine::Excavator ? FVector(0.73, 1.13, 0.54) : FVector(1.1, 2.9, 0.57);
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
        return FString::Printf(TEXT("Blade %s | %s"), bTool ? TEXT("down") : TEXT("raised"), bGrade ? TEXT("grading") : TEXT("cutting"));
    case EMudMachine::Excavator:
        return FString::Printf(TEXT("Bucket %s | %s"), bTool ? TEXT("digging") : TEXT("idle"),
            bOtherGroup ? TEXT("curl / swing") : TEXT("boom / stick"));
    default:
        return bUnload ? TEXT("Tipping") : TEXT("Hauling");
    }
}
