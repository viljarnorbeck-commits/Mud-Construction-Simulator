#include "MudMachine.h"

#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"

namespace
{
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
}

void AMudMachine::BevelBody(USceneComponent* Parent, const TCHAR* Paint, TConstArrayView<FVector2D> Profile,
    double CenterY, double HalfWidth, double Bevel)
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
    Body.Face(Rings[0], FVector(0, -1, 0));
    Body.Face(Rings[3], FVector(0, 1, 0));
    for (int32 I = 0; I < Profile.Num(); ++I)
    {
        const int32 J = (I + 1) % Profile.Num();
        for (int32 R = 0; R < 3; ++R)
        {
            const FVector A = Rings[R][I], B = Rings[R][J], C = Rings[R + 1][J], D = Rings[R + 1][I];
            Body.Quad(A, B, C, D, (A + B + C + D) * 0.25 - FVector(Center.X, CenterY, Center.Y));
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
        Seals.Quad(Sill[I], Sill[J], Roof[J], Roof[I], Out);
        const FVector EdgeNormal = FVector::CrossProduct(Sill[J] - Sill[I], Roof[I] - Sill[I]).GetSafeNormal();
        const FVector Offset = (FVector::DotProduct(EdgeNormal, Out) < 0 ? -EdgeNormal : EdgeNormal) * 0.6;
        const auto Point = [&](double U, double V)
        {
            return FMath::Lerp(FMath::Lerp(Sill[I], Sill[J], U), FMath::Lerp(Roof[I], Roof[J], U), V) + Offset;
        };
        const auto Window = [&](double Left, double Right)
        {
            Glass.Quad(Point(Left, 0.07), Point(Right, 0.07), Point(Right, 0.93), Point(Left, 0.93), Out);
        };
        if (I % 2 == 0)
        {
            Window(0.07, 0.68);
            Window(0.72, 0.93);
        }
        else
            Window(0.045, 0.955);
    }
    auto* Mesh = Skin(*this, Cab);
    Panels.Apply(*Mesh, 0, Paint);
    Seals.Apply(*Mesh, 1, TEXT("Rubber"));
    Glass.Apply(*Mesh, 2, TEXT("Glass"));
    Mesh->RegisterComponent();

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
    for (double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), TEXT("Chassis"), Cab, FVector(0, Side * (Y + 0.3), 8),
            {(SizeCm.X - 5) / 100, 0.027, 0.095});
        Part(TEXT("Cube"), TEXT("Rubber"), Cab, FVector(-X * 0.35, Side * (Y + 1), Belt - 7),
            {0.15, 0.035, 0.032});
    }
    BuildCabControls(Cab, SizeCm);
}

void AMudMachine::BuildCabControls(USceneComponent* Cab, FVector Size)
{
    const bool bHauler = Kind == EMudMachine::Truck || Kind == EMudMachine::Dumper;
    const double DriverY = bHauler ? -Size.Y * 0.22 : 0;
    CabEye = Pivot(Cab, {-Size.X * 0.19, DriverY, Size.Z * 0.79});
    Part(TEXT("Cube"), TEXT("Chassis"), Cab, {0, 0, 5}, {Size.X * 0.0095, Size.Y * 0.0095, 0.09});
    Part(TEXT("Cube"), TEXT("Rubber"), Cab, {-Size.X * 0.24, DriverY, Size.Z * 0.23},
        {0.42, 0.44, 0.11}, FRotator(5, 0, 0));
    Part(TEXT("Cube"), TEXT("Rubber"), Cab, {-Size.X * 0.39, DriverY, Size.Z * 0.42},
        {0.12, 0.45, 0.49}, FRotator(-8, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Cab, {Size.X * 0.36, 0, Size.Z * 0.34},
        {Size.X * 0.0026, Size.Y * 0.009, 0.16}, FRotator(10, 0, 0));

    const FVector Screen(Size.X * 0.34, DriverY + (bHauler ? 47 : 0), Size.Z * 0.45);
    auto* Monitor = Pivot(Cab, Screen);
    Monitor->SetRelativeRotation(FRotator(-18, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Monitor, {}, {0.055, 0.34, 0.14});
    Part(TEXT("Cube"), TEXT("Glass"), Monitor, {-3.1, 0, 0}, {0.018, 0.31, 0.115});
    CabDisplay = NewObject<UTextRenderComponent>(this);
    CabDisplay->SetupAttachment(Monitor);
    CabDisplay->SetRelativeLocation(FVector(-4.3, 0, 0));
    CabDisplay->SetRelativeRotation(FRotator(0, 180, 0));
    CabDisplay->SetHorizontalAlignment(EHTA_Center);
    CabDisplay->SetVerticalAlignment(EVRTA_TextCenter);
    CabDisplay->SetWorldSize(4.1);
    CabDisplay->SetTextRenderColor(FColor(166, 227, 184));
    CabDisplay->RegisterComponent();
    UpdateCabInstruments();

    if (bHauler)
    {
        auto* Column = Pivot(Cab, {Size.X * 0.21, DriverY, Size.Z * 0.47});
        Column->SetRelativeRotation(FRotator(-38, 0, 0));
        Part(TEXT("Cube"), TEXT("Chassis"), Column, {9, 0, 0}, {0.28, 0.10, 0.11});
        CabWheel = Pivot(Column, FVector::ZeroVector);
        FSurface Rim;
        constexpr int32 Segments = 28;
        const auto RingPoint = [](int32 Segment, int32 Around)
        {
            const double Angle = 2 * PI * Segment / Segments;
            const double SectionAngle = 2 * PI * Around / 8;
            const double Radius = 17.5 + 1.7 * FMath::Cos(SectionAngle);
            return FVector(1.7 * FMath::Sin(SectionAngle), Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle));
        };
        for (int32 I = 0; I < Segments; ++I)
            for (int32 J = 0; J < 8; ++J)
            {
                const FVector A = RingPoint(I, J), B = RingPoint(I + 1, J);
                const FVector C = RingPoint(I + 1, J + 1), D = RingPoint(I, J + 1);
                const FVector Center = RingPoint(I, 0) * (17.5 / 19.2);
                Rim.Quad(A, B, C, D, (A + D) * 0.5 - Center);
            }
        auto* Mesh = Skin(*this, CabWheel);
        Rim.Apply(*Mesh, 0, TEXT("Rubber"));
        Mesh->RegisterComponent();
        Part(TEXT("Cube"), TEXT("Chassis"), CabWheel, {}, {0.053, 0.13, 0.105});
        for (const double Angle : {30.0, 150.0, 270.0})
        {
            const FRotator Direction(0, 0, Angle);
            Part(TEXT("Cube"), TEXT("Chassis"), CabWheel, Direction.RotateVector({0, 11, 0}),
                {0.028, 0.13, 0.034}, Direction);
        }
        return;
    }
    for (const double Side : {-1.0, 1.0})
    {
        const FVector Mount(-Size.X * 0.09, Side * Size.Y * 0.35, Size.Z * 0.30);
        Part(TEXT("Cube"), TEXT("Chassis"), Cab, Mount, {0.37, 0.18, 0.12});
        auto* Lever = Pivot(Cab, Mount + FVector(9, 0, 6));
        (Side < 0 ? CabLeverA : CabLeverB) = Lever;
        Part(TEXT("Cylinder"), TEXT("Steel"), Lever, {0, 0, 6}, {0.03, 0.03, 0.13});
        Part(TEXT("Cube"), TEXT("Rubber"), Lever, {0, 0, 15}, {0.073, 0.065, 0.089}, FRotator(-12, 0, 0));
    }
}

void AMudMachine::UpdateCabInstruments()
{
    if (CabWheel)
        CabWheel->SetRelativeRotation(FRotator(0, 0, -SteeringCommand * 150));
    if (CabLeverA)
    {
        const double InputScale = Kind == EMudMachine::Dozer ? 29 : 0.38;
        CabLeverA->SetRelativeRotation(FRotator(FMath::Clamp(-ValveA * InputScale, -22.0, 22.0), 0, 0));
    }
    if (CabLeverB)
    {
        const double Input = Kind == EMudMachine::Dozer ? SteeringCommand * 20 : -ValveB * 0.38;
        CabLeverB->SetRelativeRotation(FRotator(FMath::Clamp(Input, -22.0, 22.0), 0, 0));
    }
    const TCHAR* Direction = Speed > 0.12 ? TEXT("F") : Speed < -0.12 ? TEXT("R") : TEXT("N");
    const FString Readout = FString::Printf(TEXT("%s  %02.0f km/h\nPAYLOAD  %.1f t"), Direction,
        FMath::Abs(Speed) * 3.6, Cargo.Mass());
    if (Readout != CabReadout)
    {
        CabReadout = Readout;
        CabDisplay->SetText(FText::FromString(Readout));
    }
}

void AMudMachine::BuildExcavatorBody(const TCHAR* Paint)
{
    const FVector2D Tail[] = {{-229, 22}, {-205, 8}, {85, 8}, {93, 51}, {65, 96}, {-52, 116}, {-187, 116}, {-226, 94}};
    BevelBody(Turret, Paint, Tail, 0, 114, 14);
    Part(TEXT("Cylinder"), TEXT("Chassis"), GetRootComponent(), {0, 0, 123}, {1.83, 1.83, 0.24});
    BuildCab(Turret, Paint, {16, -59, 102}, {156, 101, 131}, 19);
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {-80, 113, 76}, {1.49, 0.028, 0.43});
    TArray<FTransform> Vents;
    for (int32 I = 0; I < 8; ++I)
        Vents.Emplace(FRotator::ZeroRotator, FVector(-142 + I * 17.0, 116, 76), FVector(0.024, 0.035, 0.34));
    RepeatedBoxes(TEXT("Steel"), Turret, Vents);
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {-99, 110, 26}, {1.79, 0.047, 0.15});
    Part(TEXT("Cube"), TEXT("Steel"), Turret, {-6, -118, 38}, {0.48, 0.23, 0.067});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Turret, {-171, 61, 139}, {0.16, 0.16, 0.50});
    Part(TEXT("Cylinder"), TEXT("Steel"), Turret, {-171, 61, 166}, {0.21, 0.21, 0.045}, FRotator(17, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {85, -94, 239}, {0.13, 0.26, 0.12});
    Part(TEXT("Cube"), TEXT("Headlight"), Turret, {92, -94, 239}, {0.025, 0.22, 0.093});
    Part(TEXT("Cube"), TEXT("RedLamp"), Turret, {-216, -50, 79}, {0.04, 0.22, 0.085});

    const FVector2D BoomShape[] = {{-27, -14}, {77, -21}, {193, -23}, {339, -16}, {364, -7}, {360, 15},
        {207, 41}, {109, 34}, {-14, 25}};
    BevelBody(BoomPivot, Paint, BoomShape, 0, 19, 4);
    const FVector2D StickShape[] = {{-18, -12}, {174, -16}, {229, -12}, {242, 2}, {224, 14}, {131, 22}, {-15, 22}};
    BevelBody(StickPivot, Paint, StickShape, 0, 14, 3);
    for (const double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cylinder"), TEXT("CylinderRod"), StickPivot, {0, Side * 19, 0}, {0.37, 0.37, 0.075}, FRotator(0, 0, 90));
        Part(TEXT("Cylinder"), TEXT("CylinderRod"), Tool, {0, Side * 15, 0}, {0.26, 0.26, 0.072}, FRotator(0, 0, 90));
    }
}

void AMudMachine::BuildDozerBody(const TCHAR* Paint)
{
    const FVector2D Hood[] = {{-82, 5}, {176, 5}, {195, 26}, {177, 89}, {143, 99}, {-67, 105}};
    BevelBody(Turret, Paint, Hood, 0, 79, 10);
    BuildCab(Turret, Paint, {-107, 0, 62}, {122, 154, 144}, 11);
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {187, 0, 53}, {0.12, 1.15, 0.61}, FRotator(-13, 0, 0));
    TArray<FTransform> Grille;
    for (int32 I = 0; I < 9; ++I)
        Grille.Emplace(FRotator(-13, 0, 0), FVector(194, -48 + I * 12.0, 53), FVector(0.058, 0.029, 0.48));
    RepeatedBoxes(TEXT("Steel"), Turret, Grille);
    Part(TEXT("Cylinder"), TEXT("Chassis"), Turret, {101, 44, 115}, {0.14, 0.14, 0.51});
    for (double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), TEXT("Headlight"), Turret, {-44, Side * 59, 194}, {0.13, 0.24, 0.13});
        Part(TEXT("Cube"), TEXT("Chassis"), Turret, {-109, Side * 88, 51}, {0.47, 0.23, 0.08});
    }
}

void AMudMachine::BuildBlade()
{
    const FVector2D Curve[] = {{12, -26.5}, {3, -15}, {-5, 0}, {-10, 16}, {-9, 31}, {-1, 46}, {15, 62}};
    FSurface Face, Back;
    for (int32 I = 0; I < UE_ARRAY_COUNT(Curve) - 1; ++I)
    {
        const FVector2D& A = Curve[I];
        const FVector2D& B = Curve[I + 1];
        Face.Quad({A.X, -172, A.Y}, {A.X, 172, A.Y}, {B.X, 172, B.Y}, {B.X, -172, B.Y}, FVector::ForwardVector);
        Back.Quad({A.X - 8, -172, A.Y}, {A.X - 8, 172, A.Y}, {B.X - 8, 172, B.Y},
            {B.X - 8, -172, B.Y}, -FVector::ForwardVector);
        for (const double Side : {-1.0, 1.0})
            Back.Quad({A.X, Side * 172, A.Y}, {A.X - 8, Side * 172, A.Y},
                {B.X - 8, Side * 172, B.Y}, {B.X, Side * 172, B.Y}, FVector(0, Side, 0));
    }
    Back.Quad({7, -172, 62}, {7, 172, 62}, {15, 172, 62}, {15, -172, 62}, FVector::UpVector);
    auto* Mesh = Skin(*this, Tool);
    Face.Apply(*Mesh, 0, TEXT("WornSteel"));
    Back.Apply(*Mesh, 1, TEXT("Steel"));
    Mesh->RegisterComponent();
    Part(TEXT("Cube"), TEXT("CuttingEdge"), Tool, {11, 0, -22.5}, {0.12, 3.53, 0.08}, FRotator(-30, 0, 0));
    TArray<FTransform> Fasteners;
    for (int32 I = -8; I <= 8; ++I)
        Fasteners.Emplace(FRotator::ZeroRotator, FVector(18, I * 20.0, -21), FVector(0.025, 0.048, 0.048));
    RepeatedBoxes(TEXT("Steel"), Tool, Fasteners);
}

void AMudMachine::BuildHaulerBody(const TCHAR* Paint)
{
    auto* Root = GetRootComponent();
    const bool bDumper = Kind == EMudMachine::Dumper;
    if (bDumper)
    {
        const FVector2D Nose[] = {{222, 113}, {315, 113}, {326, 139}, {313, 190}, {230, 211}, {204, 178}};
        BevelBody(Root, Paint, Nose, 0, 112, 12);
        BuildCab(Root, Paint, {166, 0, 149}, {153, 202, 151}, 26);
    }
    else
    {
        const FVector2D Nose[] = {{213, 110}, {314, 110}, {327, 132}, {309, 183}, {286, 209}, {218, 219}};
        BevelBody(Root, Paint, Nose, 0, 79, 10);
        BuildCab(Root, Paint, {173, 0, 116}, {152, 218, 186}, 15);
    }
    Part(TEXT("Cube"), TEXT("Chassis"), Root, {324, 0, 103}, {0.18, 2.64, 0.24});
    Part(TEXT("Cube"), TEXT("Chassis"), Root, {320, 0, 152}, {0.13, 1.34, 0.45}, FRotator(-9, 0, 0));
    TArray<FTransform> Grille;
    for (int32 I = 0; I < 8; ++I)
        Grille.Emplace(FRotator(-9, 0, 0), FVector(328 - I * 0.8, 0, 133 + I * 5.2), FVector(0.027, 1.24, 0.016));
    RepeatedBoxes(TEXT("Steel"), Root, Grille);
    for (const double Side : {-1.0, 1.0})
    {
        Part(TEXT("Cube"), Paint, Root, {185, Side * 126, 130}, {0.85, 0.55, 0.11});
        Part(TEXT("Cube"), Paint, Root, {130, Side * 126, 118}, {0.40, 0.55, 0.10}, FRotator(32, 0, 0));
        Part(TEXT("Cube"), Paint, Root, {240, Side * 126, 118}, {0.40, 0.55, 0.10}, FRotator(-32, 0, 0));
        Part(TEXT("Cube"), TEXT("Headlight"), Root, {331, Side * 99, 115}, {0.10, 0.33, 0.13});
        Part(TEXT("Cube"), TEXT("AmberLamp"), Root, {327, Side * 121, 115}, {0.10, 0.088, 0.13});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {102, Side * 123, 111}, {0.32, 0.24, 0.07});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {99, Side * 126, 92}, {0.28, 0.20, 0.07});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 120, 243}, {0.055, 0.62, 0.05});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 147, 247}, {0.12, 0.16, 0.27});
        Part(TEXT("Cube"), TEXT("CylinderRod"), Root, {198, Side * 147, 247}, {0.016, 0.12, 0.22});
    }

    auto* BedFrame = RearFrame ? RearFrame.Get() : Root;
    Part(TEXT("Cube"), TEXT("WornSteel"), TipBed, {189, 0, 0}, {3.82, 2.39, 0.16});
    Part(TEXT("Cube"), Paint, TipBed, {373, 0, 44}, {0.16, 2.57, 0.91});
    for (const double Side : {-1.0, 1.0})
    {
        const FVector2D Wall[] = {{1, 7}, {364, 7}, {370, 87}, {63, 83}, {16, 59}};
        BevelBody(TipBed, Paint, Wall, Side * 122, 8, 3);
        for (const double X : {86.0, 172.0, 258.0, 343.0})
            Part(TEXT("Cube"), Paint, TipBed, {X, Side * 134, 41}, {0.13, 0.09, 0.64}, FRotator(15, 0, 0));
        Part(TEXT("Cube"), TEXT("Chassis"), BedFrame, {-298, Side * 108, 100}, {0.16, 0.41, 0.22});
        Part(TEXT("Cube"), TEXT("RedLamp"), BedFrame, {-308, Side * 110, 103}, {0.07, 0.30, 0.12});
    }
}
