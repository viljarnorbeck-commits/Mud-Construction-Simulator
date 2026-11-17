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
    const auto Windshield = [&](double U, double V)
    {
        return FMath::Lerp(FMath::Lerp(Sill[1], Sill[2], U), FMath::Lerp(Roof[1], Roof[2], U), V) +
            FVector(1.8, 0, 0);
    };
    Rail(Windshield(0.70, 0.04), Windshield(0.47, 0.29), 0.019);
    Rail(Windshield(0.29, 0.23), Windshield(0.58, 0.34), 0.027);
    if (Kind == EMudMachine::Dumper || Kind == EMudMachine::Truck)
    {
        Rail(Windshield(0.32, 0.04), Windshield(0.18, 0.19), 0.019);
        Rail(Windshield(0.07, 0.14), Windshield(0.34, 0.24), 0.027);
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

    if (bHauler || Kind == EMudMachine::Loader)
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
        if (bHauler)
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
        CabLeverA->SetRelativeRotation(FRotator(FMath::Clamp(-ValveA * InputScale, -22.0, 22.0), 0,
            FMath::Clamp(ValveB * 0.38, -22.0, 22.0)));
    }
    if (CabLeverB)
    {
        const double Input = Kind == EMudMachine::Dozer ? SteeringCommand * 20 : -ValveCurl * 0.38;
        CabLeverB->SetRelativeRotation(FRotator(FMath::Clamp(Input, -22.0, 22.0), 0,
            FMath::Clamp(ValveSwing * 0.38, -22.0, 22.0)));
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
    const bool bShortTail = Kind == EMudMachine::Dozer;
    const double Rear = bShortTail ? -169 : -229;
    const FVector2D Tail[] = {{Rear, 29}, {Rear + 24, 8}, {85, 8}, {93, 51}, {65, 98},
        {-22, 119}, {Rear + 38, 121}, {Rear + 10, 106}};
    BevelBody(Turret, Paint, Tail, 0, 114, bShortTail ? 24 : 15);
    Part(TEXT("Cylinder"), TEXT("Chassis"), GetRootComponent(), {0, 0, 123}, {1.83, 1.83, 0.24});
    BuildCab(Turret, TEXT("Chassis"), {16, -59, 102}, {156, 101, 131}, bShortTail ? 13 : 19);
    const double VentX = bShortTail ? -52 : -80;
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {VentX, 114, 72}, {1.28, 0.028, 0.39});
    TArray<FTransform> Vents;
    for (int32 I = 0; I < 8; ++I)
        Vents.Emplace(FRotator::ZeroRotator, FVector(VentX - 52 + I * 15.0, 116, 72), FVector(0.028, 0.035, 0.28));
    RepeatedBoxes(TEXT("Steel"), Turret, Vents);
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {-99, 110, 26}, {1.79, 0.047, 0.15});
    Part(TEXT("Cube"), TEXT("Steel"), Turret, {-6, -118, 38}, {0.48, 0.23, 0.067});
    Part(TEXT("Cylinder"), TEXT("Chassis"), Turret, {Rear + 58, 61, 139}, {0.16, 0.16, 0.50});
    Part(TEXT("Cylinder"), TEXT("Steel"), Turret, {Rear + 58, 61, 166}, {0.21, 0.21, 0.045}, FRotator(17, 0, 0));
    Part(TEXT("Cube"), TEXT("Chassis"), Turret, {85, -94, 239}, {0.13, 0.26, 0.12});
    Part(TEXT("Cube"), TEXT("Headlight"), Turret, {92, -94, 239}, {0.025, 0.22, 0.093});
    Part(TEXT("Cube"), TEXT("RedLamp"), Turret, {Rear - 1, -50, 79}, {0.04, 0.22, 0.085});
    const TCHAR* Model = bShortTail ? TEXT("ECR255") : TEXT("EC300E");
    for (const double Side : {-1.0, 1.0})
    {
        MachineMark(*this, Turret, Model, {Rear + 89, Side * 117, 103}, Side * 90, 15, FColor(34, 37, 39));
        MachineMark(*this, BoomPivot, TEXT("VOLVO"), {109, Side * 20, 33}, Side * 90, 21);
    }
    MachineMark(*this, Turret, TEXT("VOLVO"), {Rear - 3, 0, 66}, 180, 21, FColor(34, 37, 39));

    TArray<FTransform> Handrails;
    for (const double X : {VentX - 48, VentX + 8, VentX + 64})
        Handrails.Emplace(FRotator::ZeroRotator, FVector(X, 100, 136), FVector(0.038, 0.038, 0.39));
    Handrails.Emplace(FRotator::ZeroRotator, FVector(VentX + 8, 100, 157), FVector(1.18, 0.039, 0.039));
    RepeatedBoxes(TEXT("Ochre"), Turret, Handrails);

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

    // Expose the three pivot axles and support the cylinders on their actual pins.
    Part(TEXT("Cylinder"), TEXT("Chassis"), BoomPivot, {163, 0, 22}, {0.27, 0.27, 0.63}, FRotator(0, 0, 90));
    Part(TEXT("Cube"), TEXT("Chassis"), BoomPivot, {158, 0, 77}, {0.34, 0.29, 0.34});
    const FVector2D RamEar[] = {{StickRamPin.X - 12, 53}, {-21, 16}, {14, 10}, {8, 38},
        {StickRamPin.X - 3, 72}, {StickRamPin.X - 13, 65}};
    BevelBody(StickPivot, TEXT("Chassis"), RamEar, 0, 11, 3);
    if (!bShortTail)
    {
        const double X = BucketRamBase.X, Z = BucketRamBase.Z;
        const FVector2D BucketRamEar[] = {{X - 18, Z - 32}, {X + 19, Z - 32}, {X + 11, Z + 7}, {X - 9, Z + 9}};
        for (const double Side : {-1.0, 1.0})
            BevelBody(StickPivot, TEXT("Chassis"), BucketRamEar, Side * 8.5, 3.5, 1.5);
    }
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
    // The ECR255 works on its front blade with the digging boom folded clear of it.
    BoomPivot->SetRelativeRotation(FRotator(39, 0, 0));
    StickPivot->SetRelativeRotation(FRotator(-145, 0, 0));
    BuildExcavatorBody(Paint);
    auto* ParkedBucket = Pivot(StickPivot, BucketMount);
    ParkedBucket->SetRelativeRotation(FRotator(100, 0, 0));
    const FVector2D Bucket[] = {{-65, -29}, {21, -26}, {29, -5}, {22, 11}, {-23, 9}, {-59, -9}};
    BevelBody(ParkedBucket, TEXT("WornSteel"), Bucket, 0, 44, 5);
    Part(TEXT("Cube"), TEXT("CuttingEdge"), ParkedBucket, {-62, 0, -28}, {0.14, 0.91, 0.055});
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

void AMudMachine::BuildTyreShell(USceneComponent* Hub, double WidthScale)
{
    // Cross section in centimetres from one bead to the other.
    const FVector2D Profile[] = {
        {-28, 33}, {-28, 44}, {-26, 53}, {-22, 59}, {-16, 62}, {-8, 62.4},
        {8, 62.4}, {16, 62}, {22, 59}, {26, 53}, {28, 44}, {28, 33}};
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
            Tyre.UVs.Emplace(4.0 * Segment / Segments, (Sample.X + 28.0) / 56.0);
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

void AMudMachine::BuildBucketEdge(double HalfWidth, FVector Lip, bool bLoader)
{
    if (bLoader)
    {
        Part(TEXT("Cube"), TEXT("CuttingEdge"), Tool, Lip + FVector(3, 0, 0),
            {0.16, (2 * HalfWidth + 3) / 100, 0.065}, FRotator(-9, 0, 0));
        return;
    }

    FSurface Teeth;
    constexpr int32 ToothCount = 5;
    for (int32 I = 0; I < ToothCount; ++I)
    {
        const double Y = (I - (ToothCount - 1) * 0.5) * HalfWidth * 0.37;
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
    auto* Mesh = Skin(*this, Tool);
    Teeth.Apply(*Mesh, 0, TEXT("CuttingEdge"));
    Mesh->RegisterComponent();
}

void AMudMachine::BuildLoaderBody(const TCHAR* Paint)
{
    auto* Front = GetRootComponent();
    const FVector2D Hood[] = {{-294, 86}, {-55, 86}, {-35, 111}, {-70, 178},
        {-240, 173}, {-287, 151}};
    BevelBody(RearFrame, Paint, Hood, 0, 95, 15);
    BuildCab(RearFrame, TEXT("Chassis"), {-56, 0, 139}, {126, 154, 164}, 13);
    Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-294, 0, 122}, {0.085, 1.62, 0.54});
    MachineMark(*this, RearFrame, TEXT("VOLVO"), {-300, 0, 125}, 180, 17);
    Part(TEXT("Cylinder"), TEXT("Chassis"), RearFrame, {-123, 48, 198}, {0.16, 0.16, 0.67});
    const FVector2D LiftArm[] = {{-18, -17}, {151, -24}, {222, -16},
        {233, 0}, {215, 18}, {89, 31}, {-18, 16}};
    for (const double Side : {-1.0, 1.0})
    {
        BevelBody(BoomPivot, Paint, LiftArm, Side * 48, 9, 4);
        Part(TEXT("Cube"), TEXT("Chassis"), Front, {107, Side * 55, 108}, {0.84, 0.16, 0.92});
        Part(TEXT("Cube"), TEXT("Headlight"), Front, {143, Side * 58, 154}, {0.09, 0.21, 0.13});
        Part(TEXT("Cube"), TEXT("Chassis"), RearFrame, {-154, Side * 126, 142}, {1.04, 0.47, 0.10});
        Part(TEXT("Cube"), TEXT("Steel"), RearFrame, {-20, Side * 92, 103}, {0.33, 0.28, 0.075});
        MachineMark(*this, RearFrame, TEXT("L120H"), {-208, Side * 97, 137}, Side * 90, 15, FColor(34, 37, 39));
    }
    Part(TEXT("Cylinder"), TEXT("Steel"), Tool, {0, 0, 6}, {0.25, 0.25, 1.10}, FRotator(0, 0, 90));
}

void AMudMachine::BuildHaulerBody(const TCHAR* Paint)
{
    auto* Root = GetRootComponent();
    const bool bDumper = Kind == EMudMachine::Dumper;
    if (bDumper)
    {
        const FVector2D Nose[] = {{202, 111}, {317, 111}, {329, 135}, {317, 177}, {225, 198}, {198, 180}};
        BevelBody(Root, Paint, Nose, 0, 106, 16);
        BuildCab(Root, TEXT("Chassis"), {180, 0, 146}, {150, 204, 157}, 29);
    }
    else
    {
        BuildCab(Root, Paint, {203, 0, 118}, {200, 228, 209}, 22);
        // The cab-over FMX has a shallow steel bumper and a stacked grille below the windshield.
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {302, 0, 124}, {0.32, 2.59, 0.37});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {304, 0, 165}, {0.105, 1.57, 0.35});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {310, 0, 166}, {0.019, 1.39, 0.045}, FRotator(0, 0, 15));
        MachineMark(*this, Root, TEXT("VOLVO"), {311, 0, 186}, 0, 14);
    }

    TArray<FTransform> Grille;
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector Offset = bDumper ? FVector(329 - I * 1.7, 0, 126 + I * 5.2) : FVector(311, 0, 149 + I * 4.15);
        Grille.Emplace(FRotator::ZeroRotator, Offset, FVector(0.027, 1.24, 0.016));
    }
    RepeatedBoxes(TEXT("Steel"), Root, Grille);
    if (bDumper)
    {
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {321, 0, 103}, {0.23, 2.56, 0.23});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {318, 0, 146}, {0.095, 1.41, 0.47}, FRotator(-17, 0, 0));
        MachineMark(*this, Root, TEXT("VOLVO"), {319, 0, 169}, 0, 15);
    }
    for (const double Side : {-1.0, 1.0})
    {
        const TCHAR* Fender = bDumper ? Paint : TEXT("Chassis");
        Part(TEXT("Cube"), Fender, Root, {185, Side * 126, 137}, {0.85, 0.55, 0.11});
        Part(TEXT("Cube"), Fender, Root, {130, Side * 126, 124}, {0.40, 0.55, 0.10}, FRotator(32, 0, 0));
        Part(TEXT("Cube"), Fender, Root, {240, Side * 126, 124}, {0.40, 0.55, 0.10}, FRotator(-32, 0, 0));
        if (bDumper)
        {
            const FVector2D Intake[] = {{247, 153}, {312, 135}, {319, 150}, {309, 167}, {253, 177}};
            BevelBody(Root, TEXT("Chassis"), Intake, Side * 106, 2.4, 0.8);
            Part(TEXT("Cube"), Paint, Root, {282, Side * 109, 155}, {0.67, 0.017, 0.033}, FRotator(-12, 0, 0));
            Part(TEXT("Cube"), TEXT("Ochre"), Root, {107, Side * 107, 183}, {0.044, 0.044, 0.92});
            Part(TEXT("Cube"), TEXT("Ochre"), Root, {107, Side * 127, 228}, {0.044, 0.44, 0.044});
            Part(TEXT("Cube"), TEXT("Headlight"), Root, {332, Side * 98, 111}, {0.055, 0.29, 0.10});
            Part(TEXT("Cube"), TEXT("AmberLamp"), Root, {330, Side * 117, 111}, {0.055, 0.073, 0.10});
        }
        else
        {
            Part(TEXT("Cube"), TEXT("Rubber"), Root, {320, Side * 99, 126}, {0.045, 0.41, 0.26});
            Part(TEXT("Cube"), TEXT("Headlight"), Root, {323, Side * 91, 124}, {0.02, 0.19, 0.037}, FRotator(0, 0, Side * 32));
            Part(TEXT("Cube"), TEXT("Headlight"), Root, {323, Side * 107, 124}, {0.02, 0.19, 0.037}, FRotator(0, 0, -Side * 32));
            MachineMark(*this, Root, TEXT("FMX"), {247, Side * 115, 166}, Side * 90, 12);
        }
        Part(TEXT("Cube"), TEXT("Steel"), Root, {102, Side * 123, 111}, {0.32, 0.24, 0.07});
        Part(TEXT("Cube"), TEXT("Steel"), Root, {99, Side * 126, 92}, {0.28, 0.20, 0.07});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 120, 243}, {0.055, 0.62, 0.05});
        Part(TEXT("Cube"), TEXT("Chassis"), Root, {205, Side * 147, 247}, {0.12, 0.16, 0.27});
        Part(TEXT("Cube"), TEXT("CylinderRod"), Root, {198, Side * 147, 247}, {0.016, 0.12, 0.22});
    }

    auto* BedFrame = RearFrame ? RearFrame.Get() : Root;
    Part(TEXT("Cube"), TEXT("WornSteel"), TipBed, {189, 0, 0}, {3.82, 2.39, 0.16});
    Part(TEXT("Cube"), Paint, TipBed, {373, 0, bDumper ? 57.0 : 44.0}, {0.16, 2.57, bDumper ? 1.16 : 0.91});
    if (bDumper)
        Part(TEXT("Cube"), Paint, TipBed, {385, 0, 112}, {0.58, 2.59, 0.09}, FRotator(-5, 0, 0));
    for (const double Side : {-1.0, 1.0})
    {
        const FVector2D Wall[] = {{1, 7}, {364, 7}, {370, bDumper ? 101.0 : 87.0}, {63, 83}, {16, 59}};
        BevelBody(TipBed, Paint, Wall, Side * 122, 8, 3);
        if (bDumper)
        {
            Part(TEXT("Cube"), Paint, TipBed, {201, Side * 131, 22}, {3.15, 0.10, 0.14});
            Part(TEXT("Cube"), TEXT("Chassis"), TipBed, {207, Side * 131, 58}, {1.73, 0.038, 0.36});
            MachineMark(*this, TipBed, TEXT("VOLVO  A30G"), {207, Side * 134, 58}, Side * 90, 20);
        }
        else
        {
            TArray<FTransform> Stiffeners;
            for (const double X : {86.0, 172.0, 258.0, 343.0})
                Stiffeners.Emplace(FRotator(15, 0, 0), FVector(X, Side * 134, 41), FVector(0.13, 0.09, 0.64));
            RepeatedBoxes(Paint, TipBed, Stiffeners);
        }
        Part(TEXT("Cube"), TEXT("Chassis"), BedFrame, {-298, Side * 108, 100}, {0.16, 0.41, 0.22});
        Part(TEXT("Cube"), TEXT("RedLamp"), BedFrame, {-308, Side * 110, 103}, {0.07, 0.30, 0.12});
    }
}
