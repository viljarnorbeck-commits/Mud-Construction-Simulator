#include "MudMachine.h"
#include "LoaderRig.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"

namespace
{
template <size_t Count>
TArray<FVector2D> LoaderProfile(const std::array<LoaderRig::Point, Count>& Points)
{
    TArray<FVector2D> Profile;
    Profile.Reserve(Count);
    for (const auto& Point : Points)
        Profile.Emplace(Point.X, Point.Z);
    return Profile;
}

struct FSurface
{
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<int32> Indices;
    TArray<FProcMeshTangent> Tangents;

    void Face(TConstArrayView<FVector> Points, FVector Out)
    {
        const int32 Start = Vertices.Num();
        const FVector Normal = FVector::CrossProduct(Points[2] - Points[0], Points[1] - Points[0]).GetSafeNormal();
        const bool bReverse = FVector::DotProduct(Normal, Out) < 0;
        const FVector U = (Points[1] - Points[0]).GetSafeNormal();
        const FVector V = FVector::CrossProduct(bReverse ? -Normal : Normal, U);
        for (const FVector& Point : Points)
        {
            Vertices.Add(Point);
            Normals.Add(bReverse ? -Normal : Normal);
            UVs.Emplace(FVector::DotProduct(Point, U) / 100, FVector::DotProduct(Point, V) / 100);
            Tangents.Emplace(U, false);
        }
        for (int32 I = 1; I < Points.Num() - 1; ++I)
            Indices.Append({Start, Start + I + (bReverse ? 1 : 0), Start + I + (bReverse ? 0 : 1)});
    }

    void Quad(FVector A, FVector B, FVector C, FVector D, FVector Out)
    {
        const FVector Points[] = {A, B, C, D};
        Face(Points, Out);
    }

    void Tube(TConstArrayView<FVector> Path, double Radius)
    {
        constexpr int32 Sides = 8;
        FVector Previous[Sides] = {};
        FVector Across = FVector::RightVector;
        for (int32 I = 0; I < Path.Num(); ++I)
        {
            const FVector Along = (Path[FMath::Min(I + 1, Path.Num() - 1)] -
                Path[FMath::Max(0, I - 1)]).GetSafeNormal();
            Across = FVector::VectorPlaneProject(Across, Along).GetSafeNormal();
            if (Across.IsNearlyZero())
                Across = FRotationMatrix::MakeFromZ(Along).GetUnitAxis(EAxis::X);
            const FVector Around = FVector::CrossProduct(Along, Across);
            FVector Ring[Sides];
            for (int32 J = 0; J < Sides; ++J)
            {
                const double Angle = (2 * PI * J) / Sides;
                Ring[J] = Path[I] + Radius * (Across * FMath::Cos(Angle) + Around * FMath::Sin(Angle));
            }
            if (I > 0)
                for (int32 J = 0; J < Sides; ++J)
                {
                    const int32 Next = (J + 1) % Sides;
                    Quad(Previous[J], Previous[Next], Ring[Next], Ring[J], (Ring[J] + Ring[Next]) * 0.5 - Path[I]);
                }
            for (int32 J = 0; J < Sides; ++J)
                Previous[J] = Ring[J];
        }
    }

    void Apply(UProceduralMeshComponent& Mesh, int32 Section, const TCHAR* Material) const
    {
        Mesh.CreateMeshSection_LinearColor(Section, Vertices, Indices, Normals, UVs, {}, Tangents, false, false);
        Mesh.SetMaterial(Section, LoadObject<UMaterialInterface>(nullptr,
            *FString::Printf(TEXT("/Game/Generated/%s.%s"), Material, Material)));
    }
};

UProceduralMeshComponent* Skin(AActor& Owner, USceneComponent* Parent)
{
    auto* Mesh = NewObject<UProceduralMeshComponent>(&Owner);
    Mesh->SetupAttachment(Parent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    return Mesh;
}

void MachineMark(AActor& Owner, USceneComponent* Parent, const TCHAR* Label, FVector Position,
    double Yaw, float Height, FColor Color = FColor::White)
{
    auto* Mark = NewObject<UTextRenderComponent>(&Owner);
    Mark->SetupAttachment(Parent);
    Mark->SetRelativeLocation(Position);
    Mark->SetRelativeRotation(FRotator(0, Yaw, 0));
    Mark->SetText(FText::FromString(Label));
    Mark->SetHorizontalAlignment(EHTA_Center);
    Mark->SetVerticalAlignment(EVRTA_TextCenter);
    Mark->SetWorldSize(Height);
    Mark->SetTextRenderColor(Color);
    Mark->SetCastShadow(false);
    Mark->RegisterComponent();
}
}

void AMudMachine::BevelBody(USceneComponent* Parent, const TCHAR* Paint, TConstArrayView<FVector2D> Profile,
    double CenterY, double HalfWidth, double Bevel, bool bBeam)
{
    FVector2D Center = FVector2D::ZeroVector;
    for (const FVector2D& Point : Profile)
        Center += Point;
    Center /= Profile.Num();
    const auto Ring = [&](double Width, double Inset)
    {
        TArray<FVector> Points;
        for (const FVector2D& Edge : Profile)
        {
            const FVector2D Point = Edge + (Center - Edge).GetSafeNormal() * Inset;
            Points.Emplace(Point.X, CenterY + Width, Point.Y);
        }
        return Points;
    };

    const TArray<FVector> Rings[] = {
        Ring(-HalfWidth, Bevel), Ring(-HalfWidth + Bevel, 0),
        Ring(HalfWidth - Bevel, 0), Ring(HalfWidth, Bevel)
    };
    FSurface Body;
    for (const int32 Side : {0, 3})
    {
        const FVector Out(0, Side == 0 ? -1 : 1, 0);
        const auto& Points = Rings[Side];
        if (bBeam)
        {
            // Pair the lower and upper outlines at each station of the curved beam.
            const int32 Last = Points.Num() - 1;
            for (int32 I = 0; I < Points.Num() / 2 - 1; ++I)
                Body.Quad(Points[I], Points[I + 1], Points[Last - I - 1], Points[Last - I], Out);
        }
        else
            Body.Face(Points, Out);
    }
    for (int32 I = 0; I < Profile.Num(); ++I)
    {
        const int32 J = (I + 1) % Profile.Num();
        for (int32 R = 0; R < 3; ++R)
        {
            const FVector A = Rings[R][I], B = Rings[R][J], C = Rings[R + 1][J], D = Rings[R + 1][I];
            const FVector2D Edge = Profile[J] - Profile[I];
            const FVector Out = bBeam ? FVector(Edge.Y, 0, -Edge.X) :
                (A + B + C + D) * 0.25 - FVector(Center.X, CenterY, Center.Y);
            Body.Quad(A, B, C, D, Out);
        }
    }
    auto* Mesh = Skin(*this, Parent);
    Body.Apply(*Mesh, 0, Paint);
    Mesh->RegisterComponent();
}

void AMudMachine::BuildCab(USceneComponent* Parent, const TCHAR* Paint, FVector Center, FVector SizeCm, double RakeCm)
{
    auto* Cab = Pivot(Parent, Center);
    const double X = SizeCm.X / 2;
    const double Y = SizeCm.Y / 2;
    const double Z = SizeCm.Z;
    const double Belt = Z * 0.30;
    // The lower sill is wide, with a tapered roof and raked windshield.
    const FVector Floor[] = {{-X, -Y, 0}, {X, -Y, 0}, {X, Y, 0}, {-X, Y, 0}};
    const FVector Sill[] = {{-X, -Y, Belt}, {X, -Y, Belt}, {X, Y, Belt}, {-X, Y, Belt}};
    const FVector Roof[] = {
        {-X + 8, -Y + 8, Z}, {X - RakeCm, -Y + 8, Z}, {X - RakeCm, Y - 8, Z}, {-X + 8, Y - 8, Z}
    };
    FSurface Panels, Seals, Glass;
    Panels.Face(Floor, -FVector::UpVector);
    Panels.Face(Roof, FVector::UpVector);
    for (int32 I = 0; I < 4; ++I)
    {
        const int32 J = (I + 1) % 4;
        const FVector Out = (Sill[I] + Sill[J]).GetSafeNormal2D();
        Panels.Quad(Floor[I], Floor[J], Sill[J], Sill[I], Out);
        const FVector EdgeNormal = FVector::CrossProduct(Sill[J] - Sill[I], Roof[I] - Sill[I]).GetSafeNormal();
        const FVector Offset = (FVector::DotProduct(EdgeNormal, Out) < 0 ? -EdgeNormal : EdgeNormal) * 0.6;
        const auto Point = [&](double U, double V)
        {
            return FMath::Lerp(FMath::Lerp(Sill[I], Sill[J], U), FMath::Lerp(Roof[I], Roof[J], U), V) + Offset;
        };
        const auto Strip = [&](FSurface& Surface, double Left, double Right, double Below, double Above)
        {
            Surface.Quad(Point(Left, Below), Point(Right, Below), Point(Right, Above), Point(Left, Above), Out);
        };
        const double Border = I % 2 == 0 ? 0.067 : 0.042;
        Strip(Seals, 0, 1, 0, 0.073);
        Strip(Seals, 0, 1, 0.928, 1);
        Strip(Seals, 0, Border, 0.073, 0.928);
        Strip(Seals, 1 - Border, 1, 0.073, 0.928);
        if (I % 2 == 0)
        {
            Strip(Seals, 0.68, 0.72, 0.073, 0.928);
            Strip(Glass, Border, 0.68, 0.073, 0.928);
            Strip(Glass, 0.72, 1 - Border, 0.073, 0.928);
        }
        else
            Strip(Glass, Border, 1 - Border, 0.073, 0.928);
    }
    auto* Mesh = Skin(*this, Cab);
    Panels.Apply(*Mesh, 0, Paint);
    Seals.Apply(*Mesh, 1, TEXT("Rubber"));
    Mesh->RegisterComponent();
    auto* Windows = Skin(*this, Cab);
    Windows->SetCastShadow(false);
    Glass.Apply(*Windows, 0, TEXT("CabGlazing"));
    Windows->RegisterComponent();

    Part(TEXT("Cube"), Paint, Cab, FVector(-RakeCm * 0.35, 0, Z + 3),
        {(SizeCm.X - RakeCm + 11) / 100, (SizeCm.Y - 4) / 100, 0.075});
    const auto Rail = [&](FVector A, FVector B, double Width)
    {
        Part(TEXT("Cube"), TEXT("Chassis"), Cab, (A + B) * 0.5,
            FVector(Width, Width, (B - A).Size() / 100), FRotationMatrix::MakeFromZ(B - A).Rotator());
    };
    for (int32 I = 0; I < 4; ++I)
    {
        const int32 J = (I + 1) % 4;
        Rail(Sill[I], Roof[I], 0.061);
        Rail(Sill[I], Sill[J], 0.064);
        Rail(Roof[I], Roof[J], 0.060);
        if (I % 2 == 0)
            Rail(FMath::Lerp(Sill[I], Sill[J], 0.70), FMath::Lerp(Roof[I], Roof[J], 0.70), 0.045);
    }
    const auto Windshield = [&](double U, double V)
    {
        return FMath::Lerp(FMath::Lerp(Sill[1], Sill[2], U), FMath::Lerp(Roof[1], Roof[2], U), V) +
            FVector(1.8, 0, 0);
    };
    Rail(Windshield(0.70, 0.04), Windshield(0.47, 0.25), 0.013);
    Rail(Windshield(0.30, 0.20), Windshield(0.60, 0.288), 0.018);
    if (Kind == EMudMachine::Dumper || Kind == EMudMachine::Truck)
    {
        Rail(Windshield(0.32, 0.04), Windshield(0.18, 0.17), 0.013);
        Rail(Windshield(0.07, 0.13), Windshield(0.34, 0.228), 0.018);
    }
    for (double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), TEXT("Chassis"), Cab, FVector(0, Side * (Y + 0.3), 8),
            {(SizeCm.X - 5) / 100, 0.027, 0.095});
        Part(TEXT("Cube"), TEXT("Rubber"), Cab, FVector(-X * 0.35, Side * (Y + 1), Belt - 7),
            {0.15, 0.035, 0.032});
        Part(TEXT("Cube"), TEXT("Steel"), Cab, {-X * 0.35, Side * (Y + 2.5), Belt - 7}, {0.12, 0.023, 0.018});
    }
    BuildCabControls(Cab, SizeCm);
}

void AMudMachine::BuildCabControls(USceneComponent* Cab, FVector Size)
{
    const bool bTruck = Kind == EMudMachine::Truck;
    const bool bExcavator = Kind == EMudMachine::Excavator;
    const bool bDozer = Kind == EMudMachine::Dozer;
    const bool bWheeled = !bExcavator && !bDozer;
    const double DriverY = bTruck ? -Size.Y * 0.22 : 0;
    const double SeatX = -Size.X * 0.25;
    const double SeatHeight = Size.Z * 0.26;
    const FVector Eye(-Size.X * 0.19, DriverY, Size.Z * 0.79);
    CabEye = Pivot(Cab, Eye);

    // Trim goes inside the glass and below the original window opening.
    Part(TEXT("Cube"), TEXT("CabFloor"), Cab, {0, 0, 3}, {Size.X * 0.0096, Size.Y * 0.0094, 0.061});
    Part(TEXT("Cube"), TEXT("CabLiner"), Cab, {-Size.X * 0.065, 0, Size.Z - 2.5},
        {Size.X * 0.0071, Size.Y * 0.0083, 0.045});
    Part(TEXT("Cube"), TEXT("Headlight"), Cab, {-Size.X * 0.25, 0, Size.Z - 5.2}, {0.09, 0.19, 0.016});
    for (const double Side : {-1.0, 1.0})
    {
        auto* DoorTrim = Pivot(Cab, {-Size.X * 0.06, Side * (Size.Y * 0.5 - 4.0), Size.Z * 0.18});
        Part(TEXT("Cube"), TEXT("CabTrim"), DoorTrim, {}, {Size.X * 0.0068, 0.063, Size.Z * 0.0023});
        Part(TEXT("Cube"), TEXT("CabFloor"), DoorTrim, {-12, -Side * 4, 3}, {0.35, 0.082, 0.041});
        Part(TEXT("Cube"), TEXT("WornSteel"), DoorTrim, {Size.X * 0.17, -Side * 4, 5}, {0.096, 0.031, 0.029});
    }

    const FVector2D Cushion[] = {{SeatX - 21, SeatHeight - 5}, {SeatX + 22, SeatHeight - 5},
        {SeatX + 24, SeatHeight + 3}, {SeatX + 15, SeatHeight + 8}, {SeatX - 21, SeatHeight + 7}};
    const FVector2D Backrest[] = {{SeatX - 25, SeatHeight + 3}, {SeatX - 15, SeatHeight + 3},
        {SeatX - 23, SeatHeight + 49}, {SeatX - 34, SeatHeight + 53}, {SeatX - 37, SeatHeight + 38}};
    BevelBody(Cab, TEXT("CabCloth"), Cushion, DriverY, 22, 4);
    BevelBody(Cab, TEXT("CabCloth"), Backrest, DriverY, 21, 4);
    Part(TEXT("Cube"), TEXT("CabCloth"), Cab, {SeatX - 35, DriverY, SeatHeight + 58},
        {0.11, 0.30, 0.16}, FRotator(-10, 0, 0));
    Part(TEXT("Cube"), TEXT("CabTrim"), Cab, {SeatX, DriverY, SeatHeight * 0.50}, {0.31, 0.34, SeatHeight * 0.007});
    TArray<FTransform> FloorRibs;
    for (int32 I = 0; I < 7; ++I)
        FloorRibs.Emplace(FRotator::ZeroRotator, FVector(Size.X * 0.04 + I * 4.0, DriverY, 6.8), FVector(0.014, 0.43, 0.009));
    RepeatedBoxes(TEXT("CabFloor"), Cab, FloorRibs)->SetCastShadow(false);

    const double PedalX = Size.X * 0.29;
    for (const double Offset : {-10.0, 10.0})
    {
        const FVector Pedal(PedalX, DriverY + Offset, 12);
        Part(TEXT("Cube"), TEXT("WornSteel"), Cab, Pedal, {0.17, 0.10, 0.021}, FRotator(28, 0, 0));
        Part(TEXT("Cube"), TEXT("CabFloor"), Cab, Pedal + FVector(-1, 0, 1.7),
            {0.14, 0.087, 0.022}, FRotator(28, 0, 0));
        if (bExcavator)
        {
            Part(TEXT("Cylinder"), TEXT("WornSteel"), Cab, Pedal + FVector(4, 0, 23),
                {0.019, 0.019, 0.44}, FRotator(-14, 0, 0));
            Part(TEXT("Sphere"), TEXT("CabFloor"), Cab, Pedal + FVector(9, 0, 47),
                {0.049, 0.047, 0.093});
        }
    }

    const double DashTop = Size.Z * (bTruck ? 0.45 : bWheeled ? 0.40 : 0.30);
    const double DashBack = Size.X * (bTruck ? 0.22 : 0.31);
    const double DashFront = Size.X * (bTruck ? 0.38 : 0.43);
    const FVector2D Dashboard[] = {{DashBack + 7, 16}, {DashFront, 15}, {DashFront + 1, DashTop - 9},
        {DashFront - 4, DashTop}, {DashBack + 3, DashTop}, {DashBack, DashTop - 7}};
    BevelBody(Cab, TEXT("CabTrim"), Dashboard, 0, Size.Y * 0.46, 4);

    const double ScreenY = bTruck ? DriverY :
        bWheeled ? FMath::Min(37.0, Size.Y * 0.22) : Size.Y * (bExcavator ? 0.29 : 0.27);
    const double ScreenWidth = bDozer ? 28 : bExcavator ? 18 : bTruck ? 37 : 28;
    const double ScreenHeight = bExcavator ? 20 : bDozer ? 16 : 14;
    const FVector Screen(Size.X * (bTruck ? 0.25 : 0.31), ScreenY,
        bWheeled ? DashTop + (bTruck ? 16 : 11) : Size.Z * (bExcavator ? 0.47 : 0.45));
    const double MountHeight = Screen.Z - DashTop;
    Part(TEXT("Cube"), TEXT("CabTrim"), Cab, {Screen.X + 4.2, Screen.Y, DashTop + MountHeight * 0.5},
        {0.085, 0.12, (MountHeight + 2) / 100});
    auto* Monitor = Pivot(Cab, Screen);
    Monitor->SetRelativeRotation(FRotator(-23, bTruck ? 0 : 14, 0));
    Part(TEXT("Cube"), TEXT("CabTrim"), Monitor, {}, {0.072, (ScreenWidth + 4) / 100, (ScreenHeight + 3) / 100});
    Part(TEXT("Cube"), TEXT("CabFloor"), Monitor, {-3.9, 0, ScreenHeight * 0.53},
        {0.092, (ScreenWidth + 5) / 100, 0.022});
    Part(TEXT("Cube"), TEXT("CabLCD"), Monitor, {-4.1, 0, 0},
        {0.014, ScreenWidth / 100, ScreenHeight / 100});
    Part(TEXT("Cube"), TEXT("CabFloor"), Monitor, {-4.95, 0, -ScreenHeight * 0.31},
        {0.004, ScreenWidth * 0.0094, 0.006});
    Part(TEXT("Cube"), TEXT("Headlight"), Monitor, {-4.95, -ScreenWidth * 0.41, ScreenHeight * 0.37},
        {0.005, 0.009, 0.006});
    CabDisplay = NewObject<UTextRenderComponent>(this);
    CabDisplay->SetupAttachment(Monitor);
    CabDisplay->SetRelativeLocation(FVector(-5.1, 0, 0));
    CabDisplay->SetRelativeRotation(FRotator(0, 180, 0));
    CabDisplay->SetHorizontalAlignment(EHTA_Center);
    CabDisplay->SetVerticalAlignment(EVRTA_TextCenter);
    CabDisplay->SetWorldSize(bExcavator ? 2.45f : 3.2f);
    CabDisplay->SetTextRenderColor(FColor(194, 229, 245));
    CabDisplay->SetCastShadow(false);
    CabDisplay->RegisterComponent();

    TArray<FTransform> Vents, Switches;
    const auto Vent = [&](FVector Center)
    {
        for (int32 I = -2; I <= 2; ++I)
            Vents.Emplace(FRotator(8, 0, 0), Center + FVector(0, 0, I * 1.25), FVector(0.014, 0.16, 0.006));
    };
    Vent({DashBack - 1, -Size.Y * 0.36, DashTop - 8});
    Vent({DashBack - 1, Size.Y * 0.35, DashTop - 8});
    if (bTruck)
    {
        auto* CenterStack = Pivot(Cab, {DashBack - 4, Size.Y * 0.13, DashTop - 3});
        CenterStack->SetRelativeRotation(FRotator(-12, 12, 0));
        Part(TEXT("Cube"), TEXT("CabTrim"), CenterStack, {}, {0.19, 0.44, 0.40});
        Part(TEXT("Cube"), TEXT("CabLCD"), CenterStack, {-10, 0, 9}, {0.015, 0.29, 0.15});
        MachineMark(*this, CenterStack, TEXT("FMX"), {-11.2, 0, 9}, 180, 3.8f, FColor(153, 206, 232));
        for (const double Side : {-1.0, 1.0})
            Part(TEXT("Cylinder"), TEXT("Rubber"), CenterStack, {-12, Side * 12, -12},
                {0.056, 0.056, 0.025}, FRotator(90, 0, 0));
        Part(TEXT("Cube"), TEXT("RedLamp"), CenterStack, {-10.5, 0, -12}, {0.018, 0.042, 0.030});
        TArray<FTransform> Keys;
        for (int32 I = -3; I <= 3; ++I)
            Keys.Emplace(FRotator::ZeroRotator, FVector(-10, I * 5.2, -3), FVector(0.021, 0.038, 0.032));
        RepeatedBoxes(TEXT("CabFloor"), CenterStack, Keys)->SetCastShadow(false);
    }

    if (bWheeled)
    {
        auto* Column = Pivot(Cab, {Size.X * (bTruck ? 0.085 : 0.17), DriverY, Size.Z * (bTruck ? 0.47 : 0.43)});
        Column->SetRelativeRotation(FRotator(-38, 0, 0));
        Part(TEXT("Cube"), TEXT("CabTrim"), Column, {11, 0, -3}, {0.33, 0.13, 0.15});
        Part(TEXT("Cube"), TEXT("CabFloor"), Column, {8, -11, 2}, {0.035, 0.14, 0.032}, FRotator(0, 12, 0));
        Part(TEXT("Cube"), TEXT("CabFloor"), Column, {8, 11, 2}, {0.035, 0.14, 0.032}, FRotator(0, -12, 0));
        CabWheel = Pivot(Column, FVector::ZeroVector);
        FSurface Rim;
        constexpr int32 Segments = 28;
        const double WheelRadius = bTruck ? 21.0 : 17.5;
        const auto RingPoint = [WheelRadius](int32 Segment, int32 Around)
        {
            const double Angle = 2 * PI * Segment / Segments;
            const double SectionAngle = 2 * PI * Around / 8;
            const double Radius = WheelRadius + 1.7 * FMath::Cos(SectionAngle);
            return FVector(1.7 * FMath::Sin(SectionAngle), Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));
        };
        for (int32 I = 0; I < Segments; ++I)
            for (int32 J = 0; J < 8; ++J)
            {
                const FVector A = RingPoint(I, J), B = RingPoint(I + 1, J);
                const FVector C = RingPoint(I + 1, J + 1), D = RingPoint(I, J + 1);
                const FVector Center = RingPoint(I, 0) * (WheelRadius / (WheelRadius + 1.7));
                Rim.Quad(A, B, C, D, (A + D) * 0.5 - Center);
            }
        auto* Mesh = Skin(*this, CabWheel);
        Rim.Apply(*Mesh, 0, TEXT("Rubber"));
        Mesh->RegisterComponent();
        Part(TEXT("Cylinder"), TEXT("CabFloor"), CabWheel, {-1.2, 0, 0},
            {0.152, 0.152, 0.064}, FRotator(90, 0, 0));
        MachineMark(*this, CabWheel, TEXT("VOLVO"), {-4.6, 0, 0}, 180, 1.8f, FColor(187, 192, 199));
        for (const double Angle : {15.0, 90.0, 165.0})
        {
            const FRotator Direction(0, 0, Angle);
            Part(TEXT("Cube"), TEXT("CabTrim"), CabWheel, Direction.RotateVector({0, (WheelRadius + 7) / 2, 0}),
                {0.036, (WheelRadius - 5) / 100, 0.051}, Direction);
        }
    }
    for (const double Side : {-1.0, 1.0})
    {
        const double ConsoleY = DriverY + Side * FMath::Min(35.0, Size.Y * 0.33);
        const FVector Mount(-Size.X * 0.10, ConsoleY, Size.Z * 0.31);
        Part(TEXT("Cube"), TEXT("CabCloth"), Cab, Mount + FVector(-21, 0, 7), {0.27, 0.095, 0.058});
        if (bWheeled && Side < 0)
            continue;
        const FVector2D Console[] = {{Mount.X - 23, 12}, {Mount.X + 19, 12},
            {Mount.X + 23, Mount.Z - 6}, {Mount.X + 17, Mount.Z + 2}, {Mount.X - 22, Mount.Z + 2}};
        BevelBody(Cab, TEXT("CabTrim"), Console, ConsoleY, bTruck ? 12 : 9, 2.5);
        if (Side > 0 && (Kind == EMudMachine::Dumper || bTruck))
        {
            const FVector Holder = Mount + FVector(-16, 0, 2.5);
            Part(TEXT("Cylinder"), TEXT("CabFloor"), Cab, Holder, {0.084, 0.084, 0.012});
            FVector Lip[17];
            for (int32 I = 0; I < UE_ARRAY_COUNT(Lip); ++I)
            {
                const double Angle = UE_TWO_PI * I / (UE_ARRAY_COUNT(Lip) - 1);
                Lip[I] = Holder + FVector(3.85 * FMath::Cos(Angle), 3.85 * FMath::Sin(Angle), 0.8);
            }
            FSurface Rim;
            Rim.Tube(Lip, 0.49);
            auto* HolderMesh = Skin(*this, Cab);
            Rim.Apply(*HolderMesh, 0, TEXT("CabTrim"));
            HolderMesh->RegisterComponent();
        }
        for (int32 I = 0; I < 4; ++I)
            Switches.Emplace(FRotator::ZeroRotator, Mount + FVector(-8 + I * 4.5, Side * 6, 3),
                FVector(0.030, 0.035, 0.015));
        auto* Lever = Pivot(Cab, Mount + FVector(12, -Side * 2, 6));
        (Side < 0 ? CabLeverA : CabLeverB) = Lever;
        for (int32 I = 0; I < 3; ++I)
        {
            const double Diameter = 0.091 - I * 0.017;
            Part(TEXT("Cylinder"), TEXT("CabFloor"), Cab, Mount + FVector(12, -Side * 2, 3.8 + I * 1.35),
                {Diameter, Diameter, 0.016});
        }
        Part(TEXT("Cylinder"), TEXT("WornSteel"), Lever, {0, 0, 5}, {0.025, 0.025, 0.105});
        Part(TEXT("Sphere"), TEXT("CabFloor"), Lever, {0, 0, 12}, {0.085, 0.071, 0.11}, FRotator(-14, 0, 0));
        Part(TEXT("Cube"), bDozer ? TEXT("AmberLamp") : TEXT("RedLamp"), Lever, {-3.2, 0, 13},
            {0.015, 0.026, 0.026});
    }
    RepeatedBoxes(TEXT("CabFloor"), Cab, Vents)->SetCastShadow(false);
    RepeatedBoxes(TEXT("CabFloor"), Cab, Switches)->SetCastShadow(false);
    UpdateCabInstruments();
}

void AMudMachine::UpdateCabInstruments()
{
    if (CabWheel)
        CabWheel->SetRelativeRotation(FRotator(0, 0, -SteeringCommand * 150));
    if (CabLeverA)
    {
        const bool bDozer = Kind == EMudMachine::Dozer;
        const double Pitch = bDozer ? -DriveCommand * 22.0 : -ValveA * 0.38;
        const double Roll = bDozer ? SteeringCommand * 20.0 : ValveB * 0.38;
        CabLeverA->SetRelativeRotation(FRotator(FMath::Clamp(Pitch, -22.0, 22.0), 0,
            FMath::Clamp(Roll, -22.0, 22.0)));
    }
    if (CabLeverB)
    {
        const double Input = Kind == EMudMachine::Dozer ? -ValveA * 29 :
            Kind == EMudMachine::Dumper || Kind == EMudMachine::Truck ? (bUnload ? -17.0 : 0.0) :
            Kind == EMudMachine::Loader ? -ValveA * 0.65 : -ValveCurl * 0.38;
        CabLeverB->SetRelativeRotation(FRotator(FMath::Clamp(Input, -22.0, 22.0), 0,
            FMath::Clamp((Kind == EMudMachine::Loader ? ValveCurl : ValveSwing) * 0.38, -22.0, 22.0)));
    }
    const TCHAR* Direction = Speed > 0.12 ? TEXT("F") : Speed < -0.12 ? TEXT("R") : TEXT("N");
    const double Kmh = FMath::Abs(Speed) * 3.6;
    FString Readout;
    switch (Kind)
    {
    case EMudMachine::Excavator:
        Readout = FString::Printf(TEXT("EC300E\n%.1f t\nBOOM %+.0f"), Cargo.Mass(), Boom);
        break;
    case EMudMachine::Dozer:
        Readout = FString::Printf(TEXT("D6   %s\n%02.0f km/h\nBLADE %+.0f%%"), Direction, Kmh, Blade * 100);
        break;
    case EMudMachine::Dumper:
    case EMudMachine::Truck:
        Readout = FString::Printf(TEXT("%s    %02.0f km/h\n%.1f t     %02.0f deg"), Direction, Kmh, Cargo.Mass(), Bed.Angle);
        break;
    default:
        Readout = FString::Printf(TEXT("%s    %02.0f km/h\nBUCKET   %.1f t"), Direction, Kmh, Cargo.Mass());
        break;
    }
    if (Readout != CabReadout)
    {
        CabReadout = Readout;
        CabDisplay->SetText(FText::FromString(Readout));
    }
}

void AMudMachine::BuildExcavatorBody(const TCHAR* Paint)
{
    constexpr double Rear = -229;
    const FVector2D Tail[] = {{Rear, 29}, {Rear + 24, 8}, {85, 8}, {93, 51}, {65, 98},
        {-22, 119}, {Rear + 38, 121}, {Rear + 10, 106}};
    BevelBody(Turret, Paint, Tail, 0, 114, 15);
    Part(TEXT("Cylinder"), TEXT("Chassis"), GetRootComponent(), {0, 0, 123}, {1.83, 1.83, 0.24});
    BuildCab(Turret, TEXT("Chassis"), {16, -59, 102}, {156, 101, 131}, 19);
    constexpr double VentX = -80;
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {VentX, 114, 72}, {1.28, 0.028, 0.39});
    TArray<FTransform> Vents;
    for (int32 I = 0; I < 8; ++I)
        Vents.Emplace(FRotator::ZeroRotator, FVector(VentX - 52 + I * 15.0, 116, 72), FVector(0.028, 0.035, 0.28));
    RepeatedBoxes(TEXT("Steel"), Turret, Vents);
    TArray<FTransform> CoolingSlots;
    for (int32 I = 0; I < 10; ++I)
        CoolingSlots.Emplace(FRotator(2, 0, 0), FVector(-172 + I * 10.5, 38, 122), FVector(0.037, 0.76, 0.023));
    RepeatedBoxes(TEXT("Chassis"), Turret, CoolingSlots);
    const FVector2D RearServiceDoor[] = {{-221, 44}, {-153, 39}, {-143, 50}, {-143, 96}, {-157, 110}, {-216, 104}};
    BevelBody(Turret, TEXT("Chassis"), RearServiceDoor, 112, 2.5, 1.5);
    Part(TEXT("Cube"), TEXT("Steel"), Turret, {-170, 115, 53}, {0.21, 0.028, 0.031});
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {-99, 110, 26}, {1.79, 0.047, 0.15});
    Part(TEXT("Cube"), TEXT("Steel"), Turret, {-6, -118, 38}, {0.48, 0.23, 0.067});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Turret, {Rear + 58, 61, 139}, {0.16, 0.16, 0.50});
    Part(TEXT("Cylinder"), TEXT("Steel"), Turret, {Rear + 58, 61, 166}, {0.21, 0.21, 0.045}, FRotator(17, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {85, -94, 239}, {0.13, 0.26, 0.12});
    Part(TEXT("Cube"), TEXT("Headlight"), Turret, {92, -94, 239}, {0.025, 0.22, 0.093});
    Part(TEXT("Cube"), TEXT("RedLamp"), Turret, {Rear - 1, -50, 79}, {0.04, 0.22, 0.085});
    for (const double Side : {-1.0, 1.0})
    {
        MachineMark(*this, Turret, TEXT("EC300E"), {Rear + 89, Side * 117, 103}, Side * 90, 15, FColor(34, 37, 39));
        MachineMark(*this, BoomPivot, TEXT("VOLVO"), {109, Side * 20, 33}, Side * 90, 21);
    }
    MachineMark(*this, Turret, TEXT("VOLVO"), {Rear - 3, 0, 66}, 180, 21, FColor(34, 37, 39));

    FSurface Handrails;
    const FVector DeckRail[] = {{VentX - 48, 100, 111}, {VentX - 48, 100, 144}, {VentX - 42, 100, 155},
        {VentX + 55, 100, 155}, {VentX + 64, 100, 147}, {VentX + 64, 100, 113}};
    const FVector StairRail[] = {{72, 113, 27}, {78, 126, 51}, {75, 126, 90}, {63, 114, 116}, {46, 104, 118}};
    Handrails.Tube(DeckRail, 2.1);
    Handrails.Tube(StairRail, 2.2);
    auto* Rails = Skin(*this, Turret);
    Handrails.Apply(*Rails, 0, TEXT("Ochre"));
    Rails->RegisterComponent();

    TArray<FTransform> Steps, Grips;
    for (int32 I = 0; I < 3; ++I)
    {
        const FVector Step(64 - I * 4, 126 - I * 3, 16 + I * 22);
        Steps.Emplace(FRotator::ZeroRotator, Step, FVector(0.32, 0.25, 0.044));
        for (int32 J = -2; J <= 2; ++J)
            Grips.Emplace(FRotator::ZeroRotator, Step + FVector(0, J * 4.0, 2.7), FVector(0.27, 0.013, 0.014));
    }
    RepeatedBoxes(TEXT("Chassis"), Turret, Steps);
    RepeatedBoxes(TEXT("Steel"), Turret, Grips);
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {Rear + 38, 96, 113}, {0.22, 0.22, 0.14});
    Part(TEXT("Cylinder"), TEXT("Glass"), Turret, {Rear + 33, 103, 110}, {0.09, 0.09, 0.05}, FRotator(-90, -32, 0));

    const double Elbow = StickMount.X;
    const FVector2D BoomShape[] = {
        {-26, -17}, {4, -26}, {68, -11}, {139, 18}, {189, 29}, {262, 6}, {Elbow, -19}, {Elbow + 18, -12},
        {Elbow + 18, 11}, {Elbow, 21}, {262, 53}, {189, 78}, {139, 73}, {68, 36}, {4, 23}, {-26, 15}};
    BevelBody(BoomPivot, TEXT("Chassis"), BoomShape, 0, 19, 4, true);
    const double BucketEnd = BucketMount.X;
    const FVector2D StickShape[] = {
        {-19, -14}, {0, -23}, {63, -20}, {134, -17}, {BucketEnd, -13}, {BucketEnd + 14, -8},
        {BucketEnd + 14, 9}, {BucketEnd, 15}, {134, 29}, {63, 32}, {0, 23}, {-19, 14}};
    BevelBody(StickPivot, TEXT("Chassis"), StickShape, 0, 14, 3, true);

    FSurface BoomPipes, StickPipes;
    for (const double Side : {-1.0, 1.0})
    {
        for (const double Lane : {0.0, 6.0})
        {
            const double BoomY = Side * (15 + Lane);
            const FVector OnBoom[] = {{9, BoomY, 27}, {45, BoomY, 36}, {91, BoomY, 58}, {150, BoomY, 79},
                {184, BoomY, 84}, {244, BoomY, 65}, {308, BoomY, 42}, {330, BoomY, 31}};
            BoomPipes.Tube(OnBoom, 1.8);
            const double StickY = Side * (11 + Lane);
            const FVector OnStick[] = {{12, StickY, 27}, {42, StickY, 34}, {90, StickY, 36},
                {158, StickY, 30}, {196, StickY, 22}};
            StickPipes.Tube(OnStick, 1.6);
        }
    }
    auto* BoomLines = Skin(*this, BoomPivot);
    BoomPipes.Apply(*BoomLines, 0, TEXT("Steel"));
    BoomLines->RegisterComponent();
    auto* StickLines = Skin(*this, StickPivot);
    StickPipes.Apply(*StickLines, 0, TEXT("Steel"));
    StickLines->RegisterComponent();

    // Expose the three pivot axles and support the cylinders on their actual pins.
    Part(TEXT("Cylinder"), TEXT("Chassis"), BoomPivot, {163, 0, 22}, {0.27, 0.27, 0.63}, FRotator(0, 0, 90));
    Part(TEXT("Cube"), TEXT("Chassis"), BoomPivot, {158, 0, 77}, {0.34, 0.29, 0.34});
    const FVector2D RamEar[] = {{StickRamPin.X - 12, 53}, {-21, 16}, {14, 10}, {8, 38},
        {StickRamPin.X - 3, 72}, {StickRamPin.X - 13, 65}};
    BevelBody(StickPivot, TEXT("Chassis"), RamEar, 0, 11, 3);
    const double X = BucketRamBase.X, Z = BucketRamBase.Z;
    const FVector2D BucketRamEar[] = {{X - 18, Z - 32}, {X + 19, Z - 32}, {X + 11, Z + 7}, {X - 9, Z + 9}};
    for (const double Side : {-1.0, 1.0})
        BevelBody(StickPivot, TEXT("Chassis"), BucketRamEar, Side * 8.5, 3.5, 1.5);
    for (const double Side : {-1.0, 1.0})
    {
        const FVector2D MountPlate[] = {{-28, -32}, {21, -33}, {25, 4}, {12, 24}, {-13, 24}, {-30, 9}};
        auto* Mount = Pivot(Turret, BoomMount + FVector(0, Side * 24, 0));
        BevelBody(Mount, TEXT("Chassis"), MountPlate, 0, 4, 1);
        Part(TEXT("Cylinder"), TEXT("CylinderRod"), Mount, {0, Side * 5, 0}, {0.34, 0.34, 0.064}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("CylinderRod"), BoomPivot, StickMount + FVector(0, Side * 21, 0),
            {0.39, 0.39, 0.066}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("CylinderRod"), StickPivot, BucketMount + FVector(0, Side * 15, 0),
            {0.26, 0.26, 0.072}, FRotator(0, 0, 90));
    }
}

void AMudMachine::BuildDozerBody(const TCHAR* Paint)
{
    auto* Frame = GetRootComponent();
    const FVector2D Skid[] = {{-207, 65}, {166, 65}, {198, 89}, {192, 135}, {-210, 142}, {-223, 114}};
    const FVector2D Hood[] = {{-19, 116}, {183, 117}, {205, 139}, {194, 197}, {169, 220}, {-19, 229}};
    const FVector2D Rear[] = {{-229, 120}, {-193, 109}, {1, 138}, {22, 179}, {-170, 191}, {-221, 167}};
    const FVector2D Nose[] = {{188, 121}, {211, 135}, {200, 199}, {184, 208}, {185, 181}};
    BevelBody(Frame, TEXT("Chassis"), Skid, 0, 81, 10);
    BevelBody(Frame, Paint, Hood, 0, 70, 9);
    BevelBody(Frame, Paint, Rear, 0, 102, 12);
    BevelBody(Frame, TEXT("Chassis"), Nose, 0, 62, 6);
    BuildCab(Frame, TEXT("Chassis"), {-77, 0, 173}, {168, 173, 150}, 18);
    Part(TEXT("Cube"), Paint, Frame, {-80, 0, 332}, {1.65, 1.79, 0.075}, FRotator(1, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-159, 0, 301}, {0.17, 1.20, 0.20});
    Part(TEXT("Cube"), TEXT("Glass"), Frame, {-170, 0, 298}, {0.042, 0.17, 0.095});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Frame, {58, 30, 250}, {0.12, 0.12, 0.64}, FRotator(8, 0, 0));
    Part(TEXT("Cylinder"), TEXT("WornSteel"), Frame, {61, 30, 277}, {0.14, 0.14, 0.17}, FRotator(26, 0, 0));
    Part(TEXT("Cylinder"), TEXT("Chassis"), Frame, {88, -31, 241}, {0.12, 0.12, 0.40});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Frame, {88, -31, 261}, {0.22, 0.22, 0.069});

    TArray<FTransform> Grille, Louvers, Treads;
    for (int32 I = 0; I < 12; ++I)
        Grille.Emplace(FRotator::ZeroRotator, FVector(211 - I * 1.1, 0, 140 + I * 4.8), FVector(0.019, 1.03, 0.015));
    RepeatedBoxes(TEXT("Steel"), Frame, Grille);
    MachineMark(*this, Frame, TEXT("CAT"), {201, 0, 203}, 0, 11);

    const FVector2D PushBeam[] = {{-18, -15}, {146, -17}, {320, -15}, {DozerPushLength + 8, -10},
        {DozerPushLength + 8, 11}, {320, 17}, {146, 20}, {-18, 15}};
    FSurface Handrails;
    for (const double Side : {-1.0, 1.0})
    {
        const double Y = Side * DozerPushHalfWidth;
        BevelBody(DozerPushArms, Paint, PushBeam, Y, 10.5, 4, true);
        Part(TEXT("Cylinder"), Paint, Frame, DozerPushPivot + FVector(0, Side * 143, 0),
            {0.37, 0.37, 0.40}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("Steel"), DozerPushArms, {0, Y + Side * 11, 0},
            {0.23, 0.23, 0.047}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("Steel"), Tool, DozerBladePin + FVector(0, Y + Side * 10, 0),
            {0.26, 0.26, 0.14}, FRotator(0, 0, 90));
        const FVector2D Tower[] = {{128, 99}, {177, 99}, {180, 150}, {163, 179}, {141, 174}, {126, 132}};
        BevelBody(Frame, Paint, Tower, Side * 77, 11, 4);
        const FVector2D ServiceDoor[] = {{-5, 131}, {148, 131}, {156, 155}, {143, 192}, {0, 205}};
        BevelBody(Frame, TEXT("Ochre"), ServiceDoor, Side * 70, 2.6, 1.4);
        for (int32 I = 0; I < 8; ++I)
            Louvers.Emplace(FRotator::ZeroRotator, FVector(82 + I * 7.5, Side * 73, 158), FVector(0.026, 0.012, 0.30));
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {15, Side * 74, 145}, {0.15, 0.028, 0.044});
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-177, Side * 102, 157}, {0.61, 0.023, 0.28});
        MachineMark(*this, Frame, TEXT("CAT"), {-178, Side * 104, 157}, Side * 90, 15);
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {26, Side * 72, 204}, {0.36, 0.023, 0.21});
        MachineMark(*this, Frame, TEXT("D6"), {26, Side * 74, 204}, Side * 90, 13);

        Part(TEXT("Cube"), Paint, Frame, {-18, Side * 99, 175}, {0.82, 0.33, 0.073});
        for (int32 I = 0; I < 6; ++I)
            Treads.Emplace(FRotator::ZeroRotator, FVector(-53 + I * 14.0, Side * 100, 179), FVector(0.023, 0.29, 0.014));
        const FVector GrabRail[] = {{-56, Side * 97, 177}, {-59, Side * 100, 190}, {-55, Side * 99, 227},
            {-49, Side * 92, 237}, {-42, Side * 85, 239}};
        Handrails.Tube(GrabRail, 1.9);

        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {173, Side * 59, 219}, {0.13, 0.18, 0.13});
        Part(TEXT("Cube"), TEXT("Headlight"), Frame, {180, Side * 59, 219}, {0.027, 0.13, 0.088});
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-158, Side * 74, 321}, {0.14, 0.20, 0.12});
        Part(TEXT("Cube"), TEXT("Headlight"), Frame, {-166, Side * 74, 321}, {0.025, 0.15, 0.085});
    }
    RepeatedBoxes(TEXT("Chassis"), Frame, Louvers);
    RepeatedBoxes(TEXT("Steel"), Frame, Treads);
    auto* Rails = Skin(*this, Frame);
    Handrails.Apply(*Rails, 0, TEXT("Chassis"));
    Rails->RegisterComponent();

    Part(TEXT("Cube"), Paint, Frame, {-225, 0, 101}, {0.36, 1.26, 0.31});
    Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-250, 0, 80}, {0.38, 0.29, 0.11});
    Part(TEXT("Cylinder"), TEXT("Steel"), Frame, {-260, 0, 84}, {0.09, 0.09, 0.16});
}

void AMudMachine::BuildBlade(const TCHAR* Paint)
{
    const FVector2D Curve[] = {{12, -26.5}, {0, -11}, {-9, 8}, {-16, 30}, {-15, 52}, {-6, 74}, {13, 94}};
    const double Stations[] = {-172, -147, -117, 117, 147, 172};
    const auto Point = [](FVector2D Profile, double Y, double Thickness = 0)
    {
        return FVector(Profile.X + BladeWingOffset(Y) - Thickness, Y, Profile.Y);
    };
    constexpr int32 Last = UE_ARRAY_COUNT(Curve) - 1;
    constexpr double Thickness = 8;
    FSurface Face, Back, CuttingSteel, Scuffs;
    for (int32 Span = 0; Span < UE_ARRAY_COUNT(Stations) - 1; ++Span)
    {
        const double L = Stations[Span], R = Stations[Span + 1];
        for (int32 I = 0; I < Last; ++I)
        {
            auto& Outside = I == 0 ? CuttingSteel : Face;
            Outside.Quad(Point(Curve[I], L), Point(Curve[I], R), Point(Curve[I + 1], R),
                Point(Curve[I + 1], L), FVector::ForwardVector);
            Back.Quad(Point(Curve[I], L, Thickness), Point(Curve[I], R, Thickness), Point(Curve[I + 1], R, Thickness),
                Point(Curve[I + 1], L, Thickness), -FVector::ForwardVector);
        }
        Back.Quad(Point(Curve[Last], L), Point(Curve[Last], R), Point(Curve[Last], R, Thickness),
            Point(Curve[Last], L, Thickness), FVector::UpVector);
        CuttingSteel.Quad(Point(Curve[0], L), Point(Curve[0], R), Point(Curve[0], R, Thickness),
            Point(Curve[0], L, Thickness), -FVector::UpVector);
    }
    for (const double Side : {-1.0, 1.0})
    {
        const double Y = Side * 172;
        for (int32 I = 0; I < Last; ++I)
            Back.Quad(Point(Curve[I], Y), Point(Curve[I], Y, Thickness), Point(Curve[I + 1], Y, Thickness),
                Point(Curve[I + 1], Y), FVector(0, Side, 0));
    }

    auto* Mesh = Skin(*this, Tool);
    Face.Apply(*Mesh, 0, TEXT("WornSteel"));
    Back.Apply(*Mesh, 1, Paint);
    CuttingSteel.Apply(*Mesh, 2, TEXT("CuttingEdge"));
    for (int32 I = -8; I <= 8; ++I)
    {
        const double Y = I * 18.0;
        const double Width = 1.2 + (I + 8) % 4 * 1.1;
        const FVector2D Low = FMath::Lerp(Curve[1], Curve[2], 0.18) + FVector2D(0.18, 0);
        const FVector2D High = FMath::Lerp(Curve[1], Curve[2], 0.48 + ((I + 8) % 5) * 0.09) + FVector2D(0.18, 0);
        Scuffs.Quad(Point(Low, Y - Width), Point(Low, Y + Width), Point(High, Y + 0.7), Point(High, Y - 0.3),
            FVector::ForwardVector);
    }
    Scuffs.Apply(*Mesh, 3, TEXT("CuttingEdge"));
    Mesh->RegisterComponent();

    TArray<FTransform> Fasteners;
    for (int32 I = -8; I <= 8; ++I)
    {
        const double Y = I * 20.0;
        Fasteners.Emplace(FRotator::ZeroRotator, Point({7.7, -19.0}, Y), FVector(0.026, 0.048, 0.048));
    }
    RepeatedBoxes(TEXT("Steel"), Tool, Fasteners);

    const FVector2D Brace[] = {{-41, 10}, {-13, -4}, {8, 15}, {5, 50}, {-14, 67}, {-31, 52}};
    const FVector2D LiftEar[] = {{-33, 52}, {-11, 52}, {-5, 83}, {-15, 92}, {-29, 85}};
    for (const double Side : {-1.0, 1.0})
    {
        BevelBody(Tool, Paint, Brace, Side * DozerPushHalfWidth, 12, 3);
        BevelBody(Tool, Paint, LiftEar, Side * 98, 7, 2.5);
    }
    Part(TEXT("Cube"), Paint, Tool, {-22, 0, 37}, {0.26, 2.53, 0.22});
}

void AMudMachine::BuildTyreShell(USceneComponent* Hub, double WidthScale)
{
    // Cross section in centimetres from one bead to the other.
    const FVector2D Profile[] = {
        {-28, 33}, {-28, 40}, {-29, 41}, {-29, 42.5}, {-28, 44}, {-27, 48}, {-28, 49},
        {-27.5, 50.2}, {-26, 53}, {-22, 59}, {-16, 62}, {-8, 62.4},
        {8, 62.4}, {16, 62}, {22, 59}, {26, 53}, {27.5, 50.2}, {28, 49},
        {27, 48}, {28, 44}, {29, 42.5}, {29, 41}, {28, 40}, {28, 33}};
    constexpr int32 Segments = 40;
    constexpr int32 RingSize = Segments + 1;
    constexpr int32 RingCount = UE_ARRAY_COUNT(Profile);
    FSurface Tyre;
    for (int32 Ring = 0; Ring < RingCount; ++Ring)
    {
        const FVector2D Sample = Profile[Ring];
        const FVector2D Across = Profile[FMath::Min(Ring + 1, RingCount - 1)] - Profile[FMath::Max(Ring - 1, 0)];
        const FVector2D SectionNormal(Across.X * WidthScale, -Across.Y);
        for (int32 Segment = 0; Segment < RingSize; ++Segment)
        {
            const double Angle = 2 * PI * Segment / Segments;
            const FVector Radial(FMath::Cos(Angle), 0, FMath::Sin(Angle));
            Tyre.Vertices.Add(Radial * Sample.Y + FVector(0, Sample.X * WidthScale, 0));
            Tyre.Normals.Add((Radial * SectionNormal.X + FVector(0, SectionNormal.Y, 0)).GetSafeNormal());
            Tyre.UVs.Emplace(4.0 * Segment / Segments, (Sample.X + 29.0) / 58.0);
            Tyre.Tangents.Emplace(FVector(-Radial.Z, 0, Radial.X), false);
            if (Ring == 0 || Segment == 0)
                continue;
            const int32 I = Ring * RingSize + Segment;
            Tyre.Indices.Append({I - RingSize - 1, I - RingSize, I - 1, I - RingSize, I, I - 1});
        }
    }
    auto* Mesh = Skin(*this, Hub);
    Tyre.Apply(*Mesh, 0, TEXT("Tyre"));
    Mesh->RegisterComponent();
}

void AMudMachine::BuildWheelRim(USceneComponent* Hub, double Side, double WidthScale, const TCHAR* Paint)
{
    // Radius and offset from the wheel center in centimetres, from the recessed center to the tyre bead.
    const FVector2D Profile[] = {{11.0, 32.4}, {24.0, 32.4}, {24.8, 29.6}, {30.8, 27.7},
        {36.8, 27.7}, {38.2, 28.6}, {39.6, 27.3}};
    constexpr int32 Segments = 48;
    FSurface Disc, Lip;
    const auto Point = [Side, WidthScale](FVector2D ProfilePoint, int32 Segment)
    {
        const double Angle = 2 * PI * Segment / Segments;
        return FVector(ProfilePoint.X * FMath::Cos(Angle), Side * ProfilePoint.Y * WidthScale,
            ProfilePoint.X * FMath::Sin(Angle));
    };
    for (int32 I = 0; I < Segments; ++I)
        for (int32 Ring = 0; Ring < UE_ARRAY_COUNT(Profile) - 1; ++Ring)
        {
            // Expose the dark hub behind each opening in the stamped wheel disc.
            if (Ring == 2 && I % 8 >= 2 && I % 8 <= 5)
                continue;
            auto& Surface = Ring >= 4 ? Lip : Disc;
            Surface.Quad(Point(Profile[Ring], I), Point(Profile[Ring], I + 1), Point(Profile[Ring + 1], I + 1),
                Point(Profile[Ring + 1], I), FVector(0, Side, 0));
        }
    auto* Mesh = Skin(*this, Hub);
    Disc.Apply(*Mesh, 0, Paint);
    Lip.Apply(*Mesh, 1, TEXT("WornSteel"));
    Mesh->RegisterComponent();
}

void AMudMachine::BuildBucketEdge(double HalfToothSpan, FVector Lip)
{
    FSurface Teeth;
    constexpr int32 ToothCount = 7;
    for (int32 I = 0; I < ToothCount; ++I)
    {
        const double Y = FMath::Lerp(-HalfToothSpan, HalfToothSpan, static_cast<double>(I) / (ToothCount - 1));
        const FVector Back = Lip + FVector(27, Y, 1);
        const FVector Tip = Lip + FVector(-2, Y, 0);
        const FVector A = Back + FVector(0, -8, 5), B = Back + FVector(0, 8, 5);
        const FVector C = Tip + FVector(0, 5, 2), D = Tip + FVector(0, -5, 2);
        const FVector E = Back + FVector(0, -8, -6), F = Back + FVector(0, 8, -6);
        const FVector G = Tip + FVector(0, 5, -1), H = Tip + FVector(0, -5, -1);
        Teeth.Quad(A, B, C, D, FVector::UpVector);
        Teeth.Quad(E, F, G, H, -FVector::UpVector);
        Teeth.Quad(A, D, H, E, -FVector::RightVector);
        Teeth.Quad(B, C, G, F, FVector::RightVector);
        Teeth.Quad(A, B, F, E, FVector::ForwardVector);
        Teeth.Quad(D, C, G, H, -FVector::ForwardVector);
    }
    auto* Mesh = Skin(*this, BucketTiltPivot);
    Teeth.Apply(*Mesh, 0, TEXT("CuttingEdge"));
    Mesh->RegisterComponent();
}

void AMudMachine::BuildLoaderBucket(double HalfWidth)
{
    // The quick-coupler pins are behind the shell. Only the welded mounting plates meet the shell.
    const auto& Profile = LoaderRig::BucketProfile;
    constexpr int32 Last = LoaderRig::BucketProfile.size() - 1;
    const double InsideWidth = HalfWidth - 4;
    FSurface Shell, Edge;
    const auto Inner = [&](int32 I, double Y) { return FVector(Profile[I].X, Y, Profile[I].Z); };
    const auto Outer = [](int32 I, double Y)
    {
        const auto Point = LoaderRig::OuterBucketPoint(I);
        return FVector(Point.X, Y, Point.Z);
    };
    for (int32 I = 0; I < Last; ++I)
    {
        const auto Span = Profile[I + 1] - Profile[I];
        const FVector Inward(-Span.Z, 0, Span.X);
        Shell.Quad(Inner(I, -InsideWidth), Inner(I, InsideWidth), Inner(I + 1, InsideWidth),
            Inner(I + 1, -InsideWidth), Inward);
        Shell.Quad(Outer(I, -HalfWidth), Outer(I, HalfWidth), Outer(I + 1, HalfWidth),
            Outer(I + 1, -HalfWidth), -Inward);
    }
    Shell.Quad(Inner(0, -InsideWidth), Inner(0, InsideWidth), Outer(0, HalfWidth),
        Outer(0, -HalfWidth), FVector::UpVector);
    for (const double Side : {-1.0, 1.0})
    {
        const FVector Out(0, Side, 0);
        const double Inside = Side * InsideWidth, Outside = Side * HalfWidth;
        for (int32 I = 1; I < Last; ++I)
        {
            const FVector Cheek[] = {Outer(0, Outside), Outer(I, Outside), Outer(I + 1, Outside)};
            const FVector Liner[] = {Inner(0, Inside), Inner(I, Inside), Inner(I + 1, Inside)};
            Shell.Face(Cheek, Out);
            Shell.Face(Liner, -Out);
        }
        Shell.Quad(Inner(0, Inside), Inner(Last, Inside), Outer(Last, Outside), Outer(0, Outside), FVector::UpVector);
    }
    const FVector Upper = Inner(Last, 0) + FVector(-16, 0, 0.8);
    Edge.Quad(Upper - FVector(0, HalfWidth, 0), Upper + FVector(0, HalfWidth, 0),
        Inner(Last, HalfWidth), Inner(Last, -HalfWidth), FVector::UpVector);
    Edge.Quad(Inner(Last, -HalfWidth), Inner(Last, HalfWidth), Outer(Last, HalfWidth),
        Outer(Last, -HalfWidth), FVector::ForwardVector);
    auto* Mesh = Skin(*this, Tool);
    Shell.Apply(*Mesh, 0, TEXT("WornSteel"));
    Edge.Apply(*Mesh, 1, TEXT("CuttingEdge"));
    Mesh->RegisterComponent();

    Part(TEXT("Cube"), TEXT("Chassis"), Tool, {30, 0, 52}, {0.12, 2.70, 0.13}, FRotator(6, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Tool, {27, 0, 7}, {0.13, 1.64, 0.29});
    const FVector2D Cheek[] = {{-12, -11}, {-12, 11}, {4, 19}, {29, 27}, {35, 19},
        {34, -21}, {23, -25}, {5, -18}};
    for (const double Side : {-1.0, 1.0})
    {
        const double Y = Side * LoaderRig::LiftArmAcross;
        for (const double Offset : {-16.0, 16.0})
            BevelBody(Tool, TEXT("Chassis"), Cheek, Y + Offset, 3.5, 1.5);
        Part(TEXT("Cylinder"), TEXT("Steel"), Tool, {0, Y, 0}, {0.23, 0.23, 0.43}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("CuttingEdge"), Tool, {0, Y + Side * 23, 0},
            {0.15, 0.15, 0.035}, FRotator(0, 0, 90));
        Part(TEXT("Cube"), TEXT("Steel"), Tool, {25, Y, 26}, {0.19, 0.31, 0.08});
    }
    const FVector2D TiltEar[] = {{-12, 39}, {-10, 55}, {6, 58}, {33, 62}, {38, 54}, {31, 38}, {10, 32}};
    for (const double Side : {-1.0, 1.0})
        BevelBody(Tool, TEXT("Chassis"), TiltEar, Side * 12, 3, 1.2);
    Part(TEXT("Cylinder"), TEXT("Steel"), Tool, {LoaderRig::BucketEar.X, 0, LoaderRig::BucketEar.Z},
        {0.17, 0.17, 0.34}, FRotator(0, 0, 90));
    TArray<FTransform> WearStrips, Fasteners;
    for (const double Y : {-101.0, -51.0, 0.0, 51.0, 101.0})
        WearStrips.Emplace(FRotator::ZeroRotator, FVector(LoaderRig::Lip.X - 54, Y, -55), FVector(0.72, 0.115, 0.044));
    for (int32 I = -6; I <= 6; ++I)
        Fasteners.Emplace(FRotator::ZeroRotator, FVector(LoaderRig::Lip.X - 10, I * 21.0, -47), FVector(0.033, 0.033, 0.030));
    RepeatedBoxes(TEXT("Steel"), Tool, WearStrips);
    RepeatedBoxes(TEXT("CuttingEdge"), Tool, Fasteners);
}

void AMudMachine::BuildLoaderBody(const TCHAR* Paint)
{
    auto* Front = GetRootComponent();
    const FVector2D Hood[] = {{-327, 127}, {-118, 127}, {-111, 156}, {-134, 203},
        {-255, 203}, {-308, 191}, {-328, 168}};
    const FVector2D Counterweight[] = {{-341, 83}, {-274, 74}, {-244, 103}, {-253, 129}, {-320, 143}, {-343, 123}};
    BevelBody(RearFrame, Paint, Hood, 0, 82, 12);
    BevelBody(RearFrame, TEXT("Chassis"), Counterweight, 0, 118, 12);
    BuildCab(RearFrame, TEXT("Chassis"), {-68, 0, 148}, {143, 162, 176}, 26);
    Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-329, 0, 158}, {0.063, 1.31, 0.45}, FRotator(-4, 0, 0));
    MachineMark(*this, RearFrame, TEXT("VOLVO"), {-334, 0, 156}, 180, 12);
    TArray<FTransform> Grille;
    for (int32 I = 0; I < 7; ++I)
        Grille.Emplace(FRotator::ZeroRotator, FVector(-332, 0, 140 + I * 5.9), FVector(0.024, 1.24, 0.016));
    RepeatedBoxes(TEXT("Rubber"), RearFrame, Grille);
    TArray<FTransform> HoodSeams;
    for (const double Side : {-1.0, 1.0})
    {
        HoodSeams.Emplace(FRotator::ZeroRotator, FVector(-226, Side * 82, 136), FVector(1.77, 0.023, 0.036));
        HoodSeams.Emplace(FRotator(8, 0, 0), FVector(-302, Side * 82, 151), FVector(0.031, 0.024, 0.29));
    }
    RepeatedBoxes(TEXT("Chassis"), RearFrame, HoodSeams);
    Part(TEXT("Cylinder"), TEXT("Chassis"), RearFrame, {-159, 54, 226}, {0.14, 0.14, 0.64});
    Part(TEXT("Cylinder"), TEXT("Steel"), RearFrame, {-159, 54, 255}, {0.18, 0.18, 0.045});
    Part(TEXT("Cylinder"), TEXT("Steel"), Front, {0, 0, 103}, {0.33, 0.33, 0.56});
    Part(TEXT("Cube"), TEXT("Chassis"), Front, {25, 0, 101}, {0.53, 0.60, 0.35});
    Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-28, 0, 101}, {0.55, 0.60, 0.40});
    const FVector2D TiltSupport[] = {{61, 102}, {103, 102}, {105, 141}, {97, 158}, {76, 164}, {61, 151}};
    BevelBody(Front, TEXT("Chassis"), TiltSupport, 0, 14, 5);

    const auto LiftArm = LoaderProfile(LoaderRig::ArmProfile);
    const FVector2D Tower[] = {{38, 73}, {114, 73}, {121, 111}, {96, 151}, {67, 159}, {39, 145}, {34, 107}};
    const FVector2D Vent[] = {{-310, 144}, {-188, 144}, {-169, 177}, {-283, 178}, {-306, 168}};
    const auto Rocker = LoaderProfile(LoaderRig::RockerProfile);
    LoaderRocker = Pivot(BoomPivot, {LoaderRig::RockerPin.X, 0, LoaderRig::RockerPin.Z});
    BevelBody(LoaderRocker, Paint, Rocker, 0, 14, 4);
    Part(TEXT("Cylinder"), TEXT("Steel"), LoaderRocker, {}, {0.24, 0.24, 0.33}, FRotator(0, 0, 90));
    Part(TEXT("Cylinder"), TEXT("Steel"), LoaderRocker, {LoaderRig::RockerLength, 0, 0},
        {0.16, 0.16, 0.33}, FRotator(0, 0, 90));
    Part(TEXT("Cube"), Paint, BoomPivot, {LoaderRig::RockerPin.X, 0, 6}, {0.33, 1.01, 0.30});
    FSurface LiftPipes, AccessRails;
    for (const double Side : {-1.0, 1.0})
    {
        const double ArmY = Side * LoaderRig::LiftArmAcross;
        BevelBody(BoomPivot, Paint, LiftArm, ArmY, 10, 4, true);
        Part(TEXT("Cylinder"), Paint, BoomPivot, {LoaderRig::Coupler.X, ArmY, 0},
            {0.29, 0.29, 0.22}, FRotator(0, 0, 90));
        BevelBody(Front, Paint, Tower, Side * 68, 8, 4);
        Part(TEXT("Cube"), TEXT("Chassis"), Front, {68, ArmY, 78}, {0.35, 0.27, 0.30});
        BevelBody(RearFrame, TEXT("Chassis"), Vent, Side * 81, 4, 1.5);
        Part(TEXT("Cylinder"), TEXT("Steel"), Front, {LoaderRig::Mount.X, Side * 63, LoaderRig::Mount.Z},
            {0.31, 0.31, 0.39}, FRotator(0, 0, 90));
        Part(TEXT("Cube"), TEXT("Chassis"), Front, {110, Side * 79, 164}, {0.28, 0.30, 0.20}, FRotator(0, Side * 8, 0));
        Part(TEXT("Cube"), TEXT("Headlight"), Front, {125, Side * 78, 164}, {0.044, 0.24, 0.117}, FRotator(0, Side * 8, 0));
        Part(TEXT("Cube"), TEXT("RedLamp"), RearFrame, {-340, Side * 87, 116}, {0.06, 0.28, 0.115});
        Part(TEXT("Cube"), TEXT("Rubber"), RearFrame, {-15, Side * 81, 270}, {0.085, 0.48, 0.065});
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-17, Side * 106, 273}, {0.10, 0.17, 0.24});
        Part(TEXT("Cube"), TEXT("Glass"), RearFrame, {-22, Side * 106, 273}, {0.022, 0.12, 0.19});
        const FVector MirrorStem[] = {{-13, Side * 75, 265}, {-4, Side * 96, 270}, {-5, Side * 117, 268}};
        AccessRails.Tube(MirrorStem, 1.7);
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-5, Side * 120, 258}, {0.090, 0.16, 0.24}, FRotator(0, Side * 7, 0));
        Part(TEXT("Cube"), TEXT("CylinderRod"), RearFrame, {-10, Side * 120, 258}, {0.014, 0.127, 0.205}, FRotator(0, Side * 7, 0));
        MachineMark(*this, RearFrame, TEXT("L120H"), {-222, Side * 86, 159}, Side * 90, 13);
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-35, Side * 93, 124}, {0.39, 0.27, 0.08});
        Part(TEXT("Cube"), TEXT("Steel"), RearFrame, {-35, Side * 100, 99}, {0.39, 0.23, 0.055});
        Part(TEXT("Cube"), TEXT("Steel"), RearFrame, {-35, Side * 105, 73}, {0.38, 0.21, 0.052});
        const double Y = Side * 39;
        const FVector LiftLine[] = {{9, Y, 19}, {30, Y, 22}, {68, Y, 30}, {108, Y, 33}, {140, Y, 29},
            {183, Y, 27}, {216, Y, 20}, {234, Y, 17}};
        LiftPipes.Tube(LiftLine, 1.6);
        const FVector Handrail[] = {{-57, Side * 90, 120}, {-61, Side * 94, 146}, {-58, Side * 93, 176},
            {-51, Side * 87, 191}, {-48, Side * 79, 193}};
        AccessRails.Tube(Handrail, 2.0);
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-25, Side * 69, 323}, {0.17, 0.24, 0.12});
        Part(TEXT("Cube"), TEXT("Headlight"), RearFrame, {-16, Side * 69, 323}, {0.022, 0.17, 0.078});
    }
    auto* LiftLines = Skin(*this, BoomPivot);
    LiftPipes.Apply(*LiftLines, 0, TEXT("Steel"));
    LiftLines->RegisterComponent();
    auto* Rails = Skin(*this, RearFrame);
    AccessRails.Apply(*Rails, 0, TEXT("Ochre"));
    Rails->RegisterComponent();

    for (const double Axle : {-LoaderAxleCm, LoaderAxleCm})
    {
        auto* Frame = Axle < 0 ? RearFrame.Get() : Front;
        FSurface Fenders;
        for (const double Side : {-1.0, 1.0})
        {
            const auto Point = [Axle, Side](int32 I, double Across, double Radius)
            {
                const double Angle = FMath::DegreesToRadians(29.0 + I * 15.25);
                return FVector(Axle + Radius * FMath::Cos(Angle), Side * Across,
                    LoaderTyreRadiusM * 100 + Radius * FMath::Sin(Angle));
            };
            for (int32 I = 0; I < 8; ++I)
            {
                const FVector A = Point(I, 81, 88), B = Point(I + 1, 81, 88);
                const FVector C = Point(I + 1, 137, 88), D = Point(I, 137, 88);
                Fenders.Quad(A, B, C, D, FVector::UpVector);
                Fenders.Quad(D, C, Point(I + 1, 137, 82), Point(I, 137, 82), FVector(0, Side, 0));
                Fenders.Quad(Point(I, 81, 82), Point(I + 1, 81, 82),
                    Point(I + 1, 137, 82), Point(I, 137, 82), -FVector::UpVector);
            }
        }
        auto* Mesh = Skin(*this, Frame);
        Fenders.Apply(*Mesh, 0, TEXT("Chassis"));
        Mesh->RegisterComponent();
    }
}

void AMudMachine::BuildTruckBody(const TCHAR* Paint)
{
    auto* Frame = GetRootComponent();
    BuildCab(Frame, Paint, {258, 0, 142}, {225, 240, 181}, 24);
    Part(TEXT("Cube"), TEXT("Chassis"), Frame, {344, 0, 310}, {0.27, 2.22, 0.09}, FRotator(-6, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Frame, {233, 0, 110}, {2.65, 1.22, 0.52});

    const FVector2D Bumper[] = {{350, 75}, {376, 72}, {395, 83}, {395, 113}, {378, 128}, {349, 131}};
    BevelBody(Frame, TEXT("Chassis"), Bumper, 0, 127, 8);
    Part(TEXT("Cube"), TEXT("WornSteel"), Frame, {395, 0, 84}, {0.05, 1.37, 0.13});
    Part(TEXT("Cube"), TEXT("Rubber"), Frame, {396, 0, 101}, {0.052, 0.40, 0.17});
    Part(TEXT("Cylinder"), TEXT("Steel"), Frame, {399, 0, 101}, {0.06, 0.06, 0.26}, FRotator(0, 0, 90));

    FSurface Grille, Lamps;
    Grille.Quad({374, -114, 203}, {374, 114, 203}, {381, 99, 175}, {381, -99, 175}, FVector::ForwardVector);
    Grille.Quad({385, -79, 162}, {385, 79, 162}, {392, 61, 112}, {392, -61, 112}, FVector::ForwardVector);
    const FVector Slash[] = {{383.5, -58, 178}, {379.5, 58, 197}, {379.0, 58, 201}, {382.5, -58, 182}};
    FSurface Badge;
    Badge.Face(Slash, FVector::ForwardVector);
    auto* Nose = Skin(*this, Frame);
    Grille.Apply(*Nose, 0, TEXT("Chassis"));
    Badge.Apply(*Nose, 1, TEXT("Steel"));
    Nose->RegisterComponent();
    Part(TEXT("Cylinder"), TEXT("Steel"), Frame, {381, 0, 190}, {0.17, 0.17, 0.027}, FRotator(90, 0, 0));
    MachineMark(*this, Frame, TEXT("VOLVO"), {384, 0, 190}, 0, 5);

    TArray<FTransform> GrilleBars, FrameRails, SideSteps;
    for (int32 I = 0; I < 7; ++I)
        GrilleBars.Emplace(FRotator::ZeroRotator, FVector(392 - I, 0, 116 + I * 6.1),
            FVector(0.024, 1.19 + I * 0.050, 0.017));
    RepeatedBoxes(TEXT("WornSteel"), Frame, GrilleBars);
    TArray<FTransform> UpperBars;
    for (int32 I = 0; I < 5; ++I)
    {
        const double Z = 176.5 + I * 5.5;
        const double Along = (Z - 175.0) / 28.0;
        UpperBars.Emplace(FRotator(-14, 0, 0), FVector(FMath::Lerp(382.0, 375.1, Along), 0, Z),
            FVector(0.024, FMath::Lerp(1.92, 2.20, Along), 0.027));
    }
    RepeatedBoxes(TEXT("WornSteel"), Frame, UpperBars);

    FSurface Fenders;
    for (const double Side : {-1.0, 1.0})
    {
        const FVector2D Corner[] = {{329, 111}, {375, 103}, {388, 123}, {380, 157}, {346, 165}, {331, 144}};
        BevelBody(Frame, Paint, Corner, Side * 100, 25, 6);
        const FVector2D LightPocket[] = {{376, 116}, {391, 122}, {391, 140}, {378, 151}};
        BevelBody(Frame, TEXT("Glass"), LightPocket, Side * 102, 16, 3);
        const FVector Turn(392, Side * 96, 122);
        Lamps.Quad({392, Side * 81, 137}, Turn, Turn + FVector(0, 0, 3.2),
            {392, Side * 81, 140.2}, FVector::ForwardVector);
        Lamps.Quad(Turn, {392, Side * 115, 133}, {392, Side * 115, 136.2},
            Turn + FVector(0, 0, 3.2), FVector::ForwardVector);
        Part(TEXT("Cube"), TEXT("AmberLamp"), Frame, {359, Side * 125, 128}, {0.11, 0.021, 0.054});
        MachineMark(*this, Frame, TEXT("FMX"), {329, Side * 121, 164}, Side * 90, 8);
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {169, Side * 122, 179}, {0.17, 0.028, 0.061});
        const FVector MirrorArm[] = {{348, Side * 118, 264}, {350, Side * 134, 274}, {350, Side * 148, 274}};
        const FVector LowerArm[] = {{354, Side * 121, 227}, {352, Side * 140, 231}, {350, Side * 148, 243}};
        FSurface MirrorBrackets;
        MirrorBrackets.Tube(MirrorArm, 2.5);
        MirrorBrackets.Tube(LowerArm, 2.3);
        auto* MirrorMesh = Skin(*this, Frame);
        MirrorBrackets.Apply(*MirrorMesh, 0, TEXT("Chassis"));
        MirrorMesh->RegisterComponent();
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {349, Side * 148, 258}, {0.13, 0.21, 0.42}, FRotator(0, Side * 7, 0));
        Part(TEXT("Cube"), TEXT("CylinderRod"), Frame, {341, Side * 148, 258}, {0.017, 0.16, 0.35}, FRotator(0, Side * 7, 0));
        for (int32 Step = 0; Step < 3; ++Step)
            SideSteps.Emplace(FRotator::ZeroRotator, FVector(332 - Step * 2, Side * (116 - Step * 2), 62 + Step * 22.0),
                FVector(0.44, 0.22, 0.055));
        FrameRails.Emplace(FRotator::ZeroRotator, FVector(-11, Side * 43, 112), FVector(7.60, 0.115, 0.31));

        for (const double Axle : {TruckFrontAxleCm, TruckFirstRearAxleCm, TruckSecondRearAxleCm})
        {
            const auto Arch = [&](int32 Segment, double Width, double Radius)
            {
                const double Angle = FMath::DegreesToRadians(32.0 + Segment * 14.5);
                return FVector(Axle + Radius * FMath::Cos(Angle), Side * Width,
                    TruckTyreRadiusM * 100 + Radius * FMath::Sin(Angle));
            };
            const double Outer = Axle == TruckFrontAxleCm ? 126.0 : 131.0;
            for (int32 I = 0; I < 8; ++I)
            {
                const FVector A = Arch(I, 71, 67), B = Arch(I + 1, 71, 67);
                const FVector C = Arch(I + 1, Outer, 67), D = Arch(I, Outer, 67);
                Fenders.Quad(A, B, C, D, FVector::UpVector);
                Fenders.Quad(D, C, Arch(I + 1, Outer, 61), Arch(I, Outer, 61), FVector(0, Side, 0));
                Fenders.Quad(Arch(I, 71, 61), Arch(I + 1, 71, 61), Arch(I + 1, Outer, 61),
                    Arch(I, Outer, 61), -FVector::UpVector);
            }
            if (Axle != TruckFrontAxleCm)
                Part(TEXT("Cube"), TEXT("Rubber"), Frame, {Axle - 68, Side * 100, 58}, {0.055, 0.61, 0.54}, FRotator(-7, 0, 0));
        }
    }
    auto* WheelCovers = Skin(*this, Frame);
    Fenders.Apply(*WheelCovers, 0, TEXT("Chassis"));
    WheelCovers->RegisterComponent();
    auto* Lights = Skin(*this, Frame);
    Lamps.Apply(*Lights, 0, TEXT("Headlight"));
    Lights->RegisterComponent();
    RepeatedBoxes(TEXT("Chassis"), Frame, FrameRails);
    RepeatedBoxes(TEXT("WornSteel"), Frame, SideSteps);

    for (const double Axle : {TruckFrontAxleCm, TruckFirstRearAxleCm, TruckSecondRearAxleCm})
    {
        Part(TEXT("Cylinder"), TEXT("Chassis"), Frame, {Axle, 0, TruckTyreRadiusM * 100},
            {0.20, 0.20, 1.89}, FRotator(0, 0, 90));
        Part(TEXT("Sphere"), TEXT("Chassis"), Frame, {Axle, -9, TruckTyreRadiusM * 100}, {0.39, 0.38, 0.49});
    }
    const FVector2D Tank[] = {{-49, 67}, {53, 67}, {66, 77}, {66, 112}, {52, 121}, {-50, 121}, {-64, 108}, {-64, 81}};
    BevelBody(Frame, TEXT("WornSteel"), Tank, -91, 31, 7);
    for (const double X : {-35.0, 33.0})
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {X, -121, 94}, {0.072, 0.049, 0.52});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Frame, {52, -110, 124}, {0.16, 0.16, 0.067});
    const FVector2D Locker[] = {{-46, 68}, {67, 68}, {79, 80}, {79, 112}, {-51, 112}, {-60, 99}, {-60, 81}};
    BevelBody(Frame, TEXT("Chassis"), Locker, 92, 30, 5);
    Part(TEXT("Cube"), TEXT("Steel"), Frame, {34, 123, 89}, {0.47, 0.018, 0.062});
    Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-394, 0, 53}, {0.16, 2.40, 0.135});
    for (const double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), TEXT("Steel"), Frame, {-380, Side * 73, 80}, {0.18, 0.16, 0.52}, FRotator(17, 0, 0));
        Part(TEXT("Cube"), TEXT("Chassis"), Frame, {-401, Side * 103, 94}, {0.15, 0.43, 0.22});
        Part(TEXT("Cube"), TEXT("RedLamp"), Frame, {-410, Side * 105, 96}, {0.073, 0.25, 0.11});
        Part(TEXT("Cube"), TEXT("AmberLamp"), Frame, {-410, Side * 121, 96}, {0.073, 0.055, 0.11});
    }
}

void AMudMachine::BuildTruckBed()
{
    constexpr double Front = TruckBedFrontCm;
    constexpr double Rear = 4.0;
    const FVector2D Section[] = {{99, -3}, {119, 19}, {125, 49}, {125, 117}};
    FSurface Liner, Outer;
    Liner.Quad({Rear, -99, -3}, {Front, -99, -3}, {Front, 99, -3}, {Rear, 99, -3}, FVector::UpVector);
    Outer.Quad({Rear, -102, -9}, {Front, -102, -9}, {Front, 102, -9}, {Rear, 102, -9}, -FVector::UpVector);
    for (const double Side : {-1.0, 1.0})
    {
        const FVector Out(0, Side, 0);
        for (int32 I = 0; I < UE_ARRAY_COUNT(Section) - 1; ++I)
        {
            const FVector2D A = Section[I], B = Section[I + 1];
            Liner.Quad({Rear, Side * A.X, A.Y}, {Front, Side * A.X, A.Y},
                {Front, Side * B.X, B.Y}, {Rear, Side * B.X, B.Y}, -Out + FVector(0, 0, 0.65));
            Outer.Quad({Rear, Side * (A.X + 4), A.Y - 5}, {Front, Side * (A.X + 4), A.Y - 5},
                {Front, Side * (B.X + 4), B.Y - 5}, {Rear, Side * (B.X + 4), B.Y - 5}, Out - FVector(0, 0, 0.65));
        }
        Outer.Quad({Rear, Side * 125, 117}, {Front, Side * 125, 117},
            {Front, Side * 129, 112}, {Rear, Side * 129, 112}, FVector::UpVector);
        Part(TEXT("Cube"), TEXT("Steel"), TipBed, {(Front + Rear) * 0.5, Side * 127, 115}, {4.91, 0.088, 0.095});
        Part(TEXT("Cube"), TEXT("Chassis"), TipBed, {246, Side * 52, -15}, {4.66, 0.14, 0.22});
        Part(TEXT("Cylinder"), TEXT("Steel"), GetRootComponent(), TruckBedHinge + FVector(0, Side * 85, 0),
            {0.23, 0.23, 0.19}, FRotator(0, 0, 90));
    }
    auto* Shell = Skin(*this, TipBed);
    Liner.Apply(*Shell, 0, TEXT("WornSteel"));
    Outer.Apply(*Shell, 1, TEXT("Steel"));
    Shell->RegisterComponent();
    Part(TEXT("Cube"), TEXT("Steel"), TipBed, {Front, 0, 65}, {0.069, 2.53, 1.33});
    Part(TEXT("Cube"), TEXT("Chassis"), TipBed, {Front + 13, 0, 134}, {0.48, 2.60, 0.10}, FRotator(4, 0, 0));
    Part(TEXT("Cube"), TEXT("WornSteel"), TruckTailgate, {-3, 0, -47}, {0.076, 2.50, 1.04});
    Part(TEXT("Cube"), TEXT("Steel"), TruckTailgate, {-8, 0, -51}, {0.082, 2.46, 0.15});

    TArray<FTransform> Ribs, Underfloor, Reflectors;
    for (const double X : {31.0, 139.0, 248.0, 357.0, 466.0})
    {
        Underfloor.Emplace(FRotator::ZeroRotator, FVector(X, 0, -13), FVector(0.11, 2.20, 0.13));
        for (const double Side : {-1.0, 1.0})
        {
            Ribs.Emplace(FRotator(-10, 0, 0), FVector(X, Side * 128, 72), FVector(0.12, 0.054, 0.84));
            Reflectors.Emplace(FRotator::ZeroRotator, FVector(X, Side * 125, 37), FVector(0.073, 0.028, 0.024));
        }
    }
    RepeatedBoxes(TEXT("Chassis"), TipBed, Underfloor);
    RepeatedBoxes(TEXT("WornSteel"), TipBed, Ribs);
    RepeatedBoxes(TEXT("AmberLamp"), TipBed, Reflectors);
}

void AMudMachine::BuildHaulerBody(const TCHAR* Paint)
{
    auto* Root = GetRootComponent();
    const FVector2D Nose[] = {{202, 111}, {317, 111}, {329, 135}, {317, 177}, {225, 198}, {198, 180}};
    BevelBody(Root, Paint, Nose, 0, 106, 16);
    BuildCab(Root, TEXT("Chassis"), {180, 0, 146}, {150, 204, 157}, 29);

    TArray<FTransform> Grille;
    for (int32 I = 0; I < 8; ++I)
        Grille.Emplace(FRotator::ZeroRotator, FVector(329 - I * 1.7, 0, 126 + I * 5.2), FVector(0.027, 1.24, 0.016));
    RepeatedBoxes(TEXT("Steel"), Root, Grille);
    Part(TEXT("Cube"), TEXT("Chassis"), Root, {321, 0, 103}, {0.23, 2.56, 0.23});
    Part(TEXT("Cube"), TEXT("Chassis"), Root, {318, 0, 146}, {0.095, 1.41, 0.47}, FRotator(-17, 0, 0));
    MachineMark(*this, Root, TEXT("VOLVO"), {319, 0, 169}, 0, 15);
    Part(TEXT("Cylinder"), TEXT("Steel"), Root, {DumperHingeCm, 0, 89}, {0.34, 0.34, 0.64});
    Part(TEXT("Cube"), TEXT("Chassis"), Root, {DumperHingeCm + 21, 0, 90}, {0.55, 0.66, 0.44});
    Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {DumperHingeCm - 25, 0, 90}, {0.60, 0.72, 0.44});
    for (const double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), Paint, Root, {185, Side * 126, 137}, {0.85, 0.55, 0.11});
        Part(TEXT("Cube"), Paint, Root, {130, Side * 126, 124}, {0.40, 0.55, 0.10}, FRotator(32, 0, 0));
        Part(TEXT("Cube"), Paint, Root, {240, Side * 126, 124}, {0.40, 0.55, 0.10}, FRotator(-32, 0, 0));
        const FVector2D Intake[] = {{247, 153}, {312, 135}, {319, 150}, {309, 167}, {253, 177}};
        BevelBody(Root, TEXT("Chassis"), Intake, Side * 106, 2.4, 0.8);
        Part(TEXT("Cube"), Paint, Root, {282, Side * 109, 155}, {0.67, 0.017, 0.033}, FRotator(-12, 0, 0));
        Part(TEXT("Cube"), TEXT("Ochre"), Root, {107, Side * 107, 183}, {0.044, 0.044, 0.92});
        Part(TEXT("Cube"), TEXT("Ochre"), Root, {107, Side * 127, 228}, {0.044, 0.44, 0.044});
        Part(TEXT("Cube"), TEXT("Headlight"), Root, {332, Side * 98, 111}, {0.055, 0.29, 0.10});
        Part(TEXT("Cube"), TEXT("AmberLamp"), Root, {330, Side * 117, 111}, {0.055, 0.073, 0.10});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {218, Side * 78, 278}, {0.14, 0.27, 0.15});
        Part(TEXT("Cube"), TEXT("Headlight"), Root, {226, Side * 78, 278}, {0.025, 0.21, 0.10});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {102, Side * 123, 111}, {0.32, 0.24, 0.07});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {99, Side * 126, 92}, {0.28, 0.20, 0.07});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 120, 243}, {0.055, 0.62, 0.05});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 147, 247}, {0.12, 0.16, 0.27});
        Part(TEXT("Cube"), TEXT("CylinderRod"), Root, {198, Side * 147, 247}, {0.016, 0.12, 0.22});
    }

    Part(TEXT("Cube"), TEXT("WornSteel"), TipBed, {189, 0, 0}, {3.82, 2.39, 0.16});
    Part(TEXT("Cube"), Paint, TipBed, {373, 0, 57}, {0.16, 2.57, 1.16});
    Part(TEXT("Cube"), Paint, TipBed, {375, 0, 112}, {0.20, 2.57, 0.09}, FRotator(-5, 0, 0));

    TArray<FTransform> Crossmembers;
    for (const double X : {62.0, 125.0, 199.0, 272.0, 341.0})
        Crossmembers.Emplace(FRotator::ZeroRotator, FVector(X, 0, -12), FVector(0.12, 2.15, 0.10));
    RepeatedBoxes(TEXT("Chassis"), TipBed, Crossmembers);
    FSurface BedRibs;
    for (const double Side : {-1.0, 1.0})
    {
        const FVector2D Wall[] = {{1, 7}, {364, 7}, {370, 101}, {63, 83}, {16, 59}};
        BevelBody(TipBed, Paint, Wall, Side * 122, 8, 3);
        const FVector2D Rim[] = {{12, 59}, {62, 81}, {371, 99}, {371, 106}, {63, 88}, {12, 65}};
        BevelBody(TipBed, TEXT("WornSteel"), Rim, Side * 122, 9, 2, true);
        Part(TEXT("Cylinder"), TEXT("Steel"), RearFrame, {DumperBedHingeCm, Side * 87, 135},
            {0.32, 0.32, 0.26}, FRotator(0, 0, 90));
        Part(TEXT("Cube"), Paint, TipBed, {201, Side * 131, 22}, {3.15, 0.10, 0.14});
        for (const double X : {84.0, 112.0, 303.0, 334.0})
        {
            const FVector A(X - 7, Side * 130, 20), B(X + 5, Side * 130, 20);
            const FVector C(X + 14, Side * 130, 86), D(X + 4, Side * 130, 86);
            const FVector Out(0, Side * 7, 0);
            BedRibs.Quad(A + Out, B + Out, C + Out, D + Out, Out);
            BedRibs.Quad(A, A + Out, D + Out, D, -FVector::ForwardVector);
            BedRibs.Quad(B + Out, B, C, C + Out, FVector::ForwardVector);
            BedRibs.Quad(D, D + Out, C + Out, C, FVector::UpVector);
        }
        Part(TEXT("Cube"), TEXT("Chassis"), TipBed, {207, Side * 131, 58}, {1.73, 0.038, 0.36});
        MachineMark(*this, TipBed, TEXT("VOLVO  A30G"), {207, Side * 134, 58}, Side * 90, 20);
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {DumperBedHingeCm - 5, Side * 108, 100}, {0.16, 0.41, 0.22});
        Part(TEXT("Cube"), TEXT("RedLamp"), RearFrame, {DumperBedHingeCm - 15, Side * 111, 103}, {0.06, 0.19, 0.11});
        Part(TEXT("Cube"), TEXT("AmberLamp"), RearFrame, {DumperBedHingeCm - 15, Side * 123, 103}, {0.06, 0.060, 0.11});
        Part(TEXT("Cube"), TEXT("Steel"), RearFrame, {DumperBedHingeCm - 15, Side * 98, 103}, {0.06, 0.065, 0.084});
        Part(TEXT("Cube"), TEXT("Rubber"), RearFrame, {-277, Side * 122, 72}, {0.057, 0.51, 0.48}, FRotator(-8, 0, 0));
    }
    auto* Ribs = Skin(*this, TipBed);
    BedRibs.Apply(*Ribs, 0, Paint);
    Ribs->RegisterComponent();
}
