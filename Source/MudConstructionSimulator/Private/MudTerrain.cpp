#include "MudTerrain.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RunningGear.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
using namespace Earthwork;

FVector ReadPosition(const FJsonObject& Data)
{
    return FVector(Data.GetNumberField(TEXT("x")), Data.GetNumberField(TEXT("y")), Data.GetNumberField(TEXT("z")));
}

FVector2D ReadXY(const FJsonObject& Data)
{
    return FVector2D(Data.GetNumberField(TEXT("x")), Data.GetNumberField(TEXT("y")));
}

FLinearColor GroundMasks(Material Type, double Moisture, double SlopeUp, FVector2D XY, bool bUndisturbed)
{
    const double Patch = 0.50 + 0.57 * FMath::PerlinNoise2D(XY * 0.039)
        + 0.26 * FMath::PerlinNoise2D(XY * 0.12);
    const double Wet = FMath::SmoothStep(0.12, 0.77, Moisture);
    const double Growth = bUndisturbed ? FMath::SmoothStep(0.19, 0.69, Patch)
        * FMath::SmoothStep(0.67, 0.94, SlopeUp) * (1.0 - Wet) : 0.0;
    const double Aggregate = Type == Material::Soil ? 0.0 : 1.0;
    const double Tone = (Type == Material::Rock ? 0.24 : 0.57) + 0.35 * FMath::PerlinNoise2D(XY * 0.065)
        + 0.15 * FMath::PerlinNoise2D(XY * 0.29);
    // Ground material decodes cover, aggregate, patch tone and moisture from RGBA.
    return FLinearColor(Growth, Aggregate, FMath::Clamp(Tone, 0.0, 1.0), Wet);
}

UMaterialInterface* GeneratedMaterial(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Generated/%s.%s"), Name, Name));
}
}

AMudTerrain::AMudTerrain()
{
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TerrainRoot"));
    Trunks = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Trunks"));
    Crowns = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Crowns"));
    Meadow = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Meadow"));
    Meadow->SetupAttachment(RootComponent);
    Meadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Meadow->SetCastShadow(false);
    Meadow->SetCullDistances(4200, 7600);
    for (const TCHAR* Name : {TEXT("FallingSoil"), TEXT("FallingGravel"), TEXT("FallingRock")})
    {
        auto* Pieces = CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(Name));
        Pieces->SetupAttachment(RootComponent);
        Pieces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pieces->SetCastShadow(false);
        Pieces->SetCullDistances(5500, 9500);
        Spoil.Add(Pieces);
    }
    for (auto* Instances : { Trunks.Get(), Crowns.Get() })
    {
        Instances->SetupAttachment(RootComponent);
        Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Instances->SetCullDistances(42000, 64000);
    }
}

bool AMudTerrain::Initialize()
{
    const FString Folder = FPaths::ProjectContentDir() / TEXT("World");
    FString Json;
    TSharedPtr<FJsonObject> Data;
    if (!FFileHelper::LoadFileToString(Json, *(Folder / TEXT("world.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data) || !Data.IsValid())
        return false;

    const auto& Map = *Data->GetObjectField(TEXT("heightmap"));
    Samples = Map.GetIntegerField(TEXT("samples"));
    if (Samples < TileSize + 1 || Samples > 2049 || (Samples - 1) % TileSize != 0)
        return false;
    TilesPerSide = (Samples - 1) / TileSize;
    Seed = Data->GetIntegerField(TEXT("seed"));
    const double Minimum = Map.GetNumberField(TEXT("minElevation"));
    const double Maximum = Map.GetNumberField(TEXT("maxElevation"));

    FImage Image;
    if (!FImageUtils::LoadImage(*(Folder / Map.GetStringField(TEXT("file"))), Image) ||
        Image.SizeX != Samples || Image.SizeY != Samples || Image.Format != ERawImageFormat::G16)
        return false;
    const uint16* Readings = reinterpret_cast<const uint16*>(Image.RawData.GetData());
    Original.SetNumUninitialized(Samples * Samples);
    for (int32 Cell = 0; Cell < Original.Num(); ++Cell)
        Original[Cell] = FMath::Lerp(Minimum, Maximum, Readings[Cell] / 65535.0);
    Heights = Original;
    Start = ReadPosition(*Data->GetObjectField(TEXT("start")));
    Goal = ReadPosition(*Data->GetObjectField(TEXT("goal")));
    for (const auto& Value : Data->GetArrayField(TEXT("mudPits")))
    {
        const FJsonObject& Pit = *Value->AsObject();
        Pits.Add({ ReadXY(Pit), FVector2D(Pit.GetNumberField(TEXT("rx")), Pit.GetNumberField(TEXT("ry"))) });
    }
    for (const auto& Value : Data->GetArrayField(TEXT("piles")))
    {
        const FJsonObject& Pile = *Value->AsObject();
        const FString Type = Pile.GetStringField(TEXT("material"));
        const Material Kind = Type == TEXT("gravel") ? Material::Gravel :
            (Type == TEXT("crushed-rock") ? Material::Rock : Material::Soil);
        Piles.Add({ ReadXY(Pile), Pile.GetNumberField(TEXT("radius")), Pile.GetNumberField(TEXT("height")),
            Kind, Pile.GetNumberField(TEXT("volumeM3")) });
    }
    const auto& RiverData = *Data->GetObjectField(TEXT("river"));
    for (const auto& Value : RiverData.GetArrayField(TEXT("centerline")))
    {
        const FJsonObject& Point = *Value->AsObject();
        River.Add({ ReadXY(Point), Point.GetNumberField(TEXT("z")), Point.GetNumberField(TEXT("width")) });
    }
    if (River.Num() < 2)
        return false;
    for (const auto& Value : RiverData.GetArrayField(TEXT("fords")))
        Fords.Add(ReadXY(*Value->AsObject()));

    Overview = FImageUtils::ImportFileAsTexture2D(Folder / TEXT("overview.png"));
    GroundMaterial = GeneratedMaterial(TEXT("Ground"));
    if (!GroundMaterial)
        return false;

    Trunks->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    auto* CanopyMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/PineCanopy.PineCanopy"));
    CrownInstancesPerTree = CanopyMesh ? 1 : CrownTiers;
    Crowns->SetStaticMesh(CanopyMesh ? CanopyMesh : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")));
    Trunks->SetMaterial(0, GeneratedMaterial(TEXT("Bark")));
    Crowns->SetMaterial(0, GeneratedMaterial(TEXT("Pine")));
    TArray<FTransform> TrunkTransforms, CrownTransforms;
    FRandomStream TreeVariation(Seed ^ 0x50494e45);
    for (const auto& Value : Data->GetArrayField(TEXT("trees")))
    {
        const FJsonObject& Tree = *Value->AsObject();
        const FVector Position = ReadPosition(Tree) * 100.0;
        const double Scale = Tree.GetNumberField(TEXT("scale"));
        FTree Entry;
        Entry.XY = ReadXY(Tree);
        const double TrunkWidth = TreeVariation.FRandRange(0.25, 0.44);
        Entry.Trunk = FTransform(FRotator::ZeroRotator, Position + FVector(0, 0, 187 * Scale),
            FVector(TrunkWidth, TrunkWidth, 3.74) * Scale);
        const double Bearing = FMath::Fmod(Entry.XY.X * 11 + Entry.XY.Y * 13, 360.0);
        if (CanopyMesh)
        {
            const double Width = TreeVariation.FRandRange(0.70, 1.21);
            const double HeightScale = TreeVariation.FRandRange(0.78, 1.17);
            Entry.Crown[0] = FTransform(FRotator(0, Bearing, 0), Position,
                FVector(Width, Width * TreeVariation.FRandRange(0.85, 1.13), HeightScale) * Scale);
            CrownTransforms.Add(Entry.Crown[0]);
        }
        else
        {
            const FVector Lean = FRotator(0, Bearing, 0).Vector() * 36.0;
            for (int32 Tier = 0; Tier < CrownInstancesPerTree; ++Tier)
            {
                const double Height = 368 + Tier * 151;
                const double Width = 4.05 - Tier * 0.93;
                Entry.Crown[Tier] = FTransform(FRotator(3.5, Bearing + Tier * 39, -2.0),
                    Position + (FVector(0, 0, Height) + Lean * (0.2 + 0.38 * Tier)) * Scale,
                    FVector(Width, Width * 0.90, 3.63 - Tier * 0.44) * Scale);
                CrownTransforms.Add(Entry.Crown[Tier]);
            }
        }
        TreeTiles.Add(TileIndex(Entry.XY.X, Entry.XY.Y), Trees.Add(Entry));
        TrunkTransforms.Add(Entry.Trunk);
    }
    Trunks->AddInstances(TrunkTransforms, false, false, false);
    Crowns->AddInstances(CrownTransforms, false, false, false);
    BuildGroundCover();
    auto* ClumpMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/SpoilClump.SpoilClump"));
    if (!ClumpMesh)
        ClumpMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    const TCHAR* Materials[] = { TEXT("Loose"), TEXT("LooseGravel"), TEXT("LooseRock") };
    for (int32 Kind = 0; Kind < Spoil.Num(); ++Kind)
    {
        Spoil[Kind]->SetStaticMesh(ClumpMesh);
        Spoil[Kind]->SetMaterial(0, GeneratedMaterial(Materials[Kind]));
        auto& Batch = Spills[Kind];
        Batch.Pieces.SetNum(36);
        Batch.Transforms.Init(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001)), Batch.Pieces.Num());
        Spoil[Kind]->AddInstances(Batch.Transforms, false, false, false);
    }

    TerrainMeshes.SetNum(TilesPerSide * TilesPerSide);
    TileSteps.Init(0, TerrainMeshes.Num());
    for (int32 Id = 0; Id < TerrainMeshes.Num(); ++Id)
        BuildTile(Id, 4);
    BuildWater();
    BuildMarkers();
    return true;
}

int32 AMudTerrain::TileIndex(int32 X, int32 Y) const
{
    return FMath::Clamp(Y / TileSize, 0, TilesPerSide - 1) * TilesPerSide
        + FMath::Clamp(X / TileSize, 0, TilesPerSide - 1);
}

double AMudTerrain::Height(FVector2D XY) const
{
    XY.X = FMath::Clamp(XY.X, 0.0, static_cast<double>(GetWorldSize()));
    XY.Y = FMath::Clamp(XY.Y, 0.0, static_cast<double>(GetWorldSize()));
    const int32 X = FMath::Min(FMath::FloorToInt(XY.X), Samples - 2);
    const int32 Y = FMath::Min(FMath::FloorToInt(XY.Y), Samples - 2);
    const double FX = XY.X - X, FY = XY.Y - Y;
    return FMath::Lerp(
        FMath::Lerp<double>(Heights[Index(X, Y)], Heights[Index(X + 1, Y)], FX),
        FMath::Lerp<double>(Heights[Index(X, Y + 1)], Heights[Index(X + 1, Y + 1)], FX), FY);
}

FVector AMudTerrain::Normal(FVector2D XY) const
{
    return FVector(
        Height(XY - FVector2D(1, 0)) - Height(XY + FVector2D(1, 0)),
        Height(XY - FVector2D(0, 1)) - Height(XY + FVector2D(0, 1)), 2.0).GetSafeNormal();
}

AMudTerrain::FRiverSample AMudTerrain::RiverAt(double Y) const
{
    const double Row = FMath::Clamp(Y / 8.0, 0.0, static_cast<double>(River.Num() - 1));
    const int32 I = FMath::Min(FMath::FloorToInt(Row), River.Num() - 2);
    const double Fraction = Row - I;
    return { FMath::Lerp(River[I].XY, River[I + 1].XY, Fraction),
        FMath::Lerp(River[I].Z, River[I + 1].Z, Fraction),
        FMath::Lerp(River[I].Width, River[I + 1].Width, Fraction) };
}

double AMudTerrain::WaterDepth(FVector2D XY) const
{
    const auto Point = RiverAt(XY.Y);
    return FMath::Abs(XY.X - Point.XY.X) <= Point.Width * 0.5 + 12.0
        ? FMath::Max(0.0, Point.Z - Height(XY)) : 0.0;
}

double AMudTerrain::Wetness(FVector2D XY) const
{
    double Wet = 0.10;
    for (const FMudPit& Pit : Pits)
    {
        const FVector2D Offset = XY - Pit.Center;
        const double Distance = FVector2D(Offset.X / Pit.Radii.X, Offset.Y / Pit.Radii.Y).Size();
        Wet = FMath::Max(Wet, 1.0 - FMath::SmoothStep(0.0, 1.0, Distance));
    }
    const auto Point = RiverAt(XY.Y);
    const double Distance = FMath::Abs(XY.X - Point.XY.X);
    if (Distance < Point.Width * 0.5 + 20.0)
        Wet = FMath::Max(Wet, 0.84 * (1.0 - FMath::SmoothStep(Point.Width * 0.5, Point.Width * 0.5 + 20.0, Distance)));
    return Wet;
}

Earthwork::Material AMudTerrain::Surface(FVector2D XY) const
{
    const int32 X = FMath::Clamp(FMath::RoundToInt(XY.X), 0, Samples - 1);
    const int32 Y = FMath::Clamp(FMath::RoundToInt(XY.Y), 0, Samples - 1);
    if (const auto* Column = Changed.Find(Index(X, Y)))
        return Column->Surface();
    for (const FMudPile& Pile : Piles)
        if (FVector2D::DistSquared(Pile.Center, FVector2D(X, Y)) < FMath::Square(Pile.Radius))
            return Pile.Material;
    return Material::Soil;
}

double AMudTerrain::Grip(FVector2D XY) const
{
    return SurfaceGrip(Surface(XY), Wetness(XY), WaterDepth(XY));
}

Earthwork::Column AMudTerrain::OriginalColumn(int32 Cell) const
{
    const FVector2D XY(Cell % Samples, Cell / Samples);
    for (const FMudPile& Pile : Piles)
    {
        const double Deposit = Pile.Height * (1.0 - FVector2D::Distance(Pile.Center, XY) / Pile.Radius);
        if (Deposit > 0.0)
            return { Original[Cell] - Deposit - 5.0, {{Material::Soil, 5.0}, {Pile.Material, Deposit}} };
    }
    return { Original[Cell] - 5.0, {{Material::Soil, 5.0}} };
}

Earthwork::Column& AMudTerrain::Edit(int32 Cell)
{
    if (auto* Column = Changed.Find(Cell))
        return *Column;
    return Changed.Add(Cell, OriginalColumn(Cell));
}

void AMudTerrain::UpdateCell(int32 Cell, double WorkDelta)
{
    Heights[Cell] = Changed[Cell].Height();
    if (FMath::Abs(WorkDelta) > Epsilon)
    {
        const double Blend = 1.0 - FMath::Exp(-FMath::Abs(WorkDelta) * 16.0);
        WorkFinish.Add(Cell, static_cast<float>(FMath::Lerp<double>(WorkFinish.FindRef(Cell), FMath::Sign(WorkDelta), Blend)));
    }
    DirtyCell(Cell);
}

void AMudTerrain::DirtyCell(int32 Cell)
{
    if (const int32* Plant = PlantByCell.Find(Cell))
        DirtyPlants.Add(*Plant);
    const int32 X = Cell % Samples, Y = Cell / Samples;
    for (const int32 DX : {-1, 1})
        for (const int32 DY : {-1, 1})
            Dirty.Add(TileIndex(X + DX, Y + DY));
}

TArray<int32> AMudTerrain::Cells(FVector2D XY, double Radius) const
{
    TArray<int32> Area;
    for (int32 Y = FMath::Max(0, FMath::CeilToInt(XY.Y - Radius)); Y <= FMath::Min(Samples - 1, FMath::FloorToInt(XY.Y + Radius)); ++Y)
        for (int32 X = FMath::Max(0, FMath::CeilToInt(XY.X - Radius)); X <= FMath::Min(Samples - 1, FMath::FloorToInt(XY.X + Radius)); ++X)
            if (FVector2D::DistSquared(XY, FVector2D(X, Y)) <= FMath::Square(Radius))
                Area.Add(Index(X, Y));
    return Area;
}

Earthwork::Load AMudTerrain::Excavate(FVector2D XY, double Radius, double Amount, double ToolHeight)
{
    return CutPass(XY, FVector2D(0, 1), FVector2D(Radius, Radius), Amount, ToolHeight);
}

TArray<TPair<int32, double>> AMudTerrain::ToolFootprint(FVector2D XY, FVector2D Across, FVector2D Reach) const
{
    TArray<TPair<int32, double>> Result;
    const FVector2D Along(Across.Y, -Across.X);
    for (int32 Cell : Cells(XY, FMath::Max(Reach.X, Reach.Y)))
    {
        const FVector2D Offset = FVector2D(Cell % Samples, Cell / Samples) - XY;
        const double Distance = FVector2D(FVector2D::DotProduct(Offset, Across) / Reach.X,
            FVector2D::DotProduct(Offset, Along) / Reach.Y).Size();
        if (Distance < 1)
            Result.Emplace(Cell, 1.0 - FMath::Square(Distance));
    }
    return Result;
}

Earthwork::Load AMudTerrain::CutPass(FVector2D XY, FVector2D Across, FVector2D Reach, double Amount, double ToolHeight,
    double CrossSlope)
{
    Load Result;
    if (Amount <= Epsilon)
        return Result;
    const auto Area = ToolFootprint(XY, Across, Reach);
    const auto CutDepth = [&](int32 Cell)
    {
        const FVector2D Offset = FVector2D(Cell % Samples, Cell / Samples) - XY;
        const double CutPlane = ToolHeight + FVector2D::DotProduct(Offset, Across) * CrossSlope;
        return Heights[Cell] - CutPlane;
    };
    double TotalWeight = 0;
    for (const auto& Sample : Area)
        if (CutDepth(Sample.Key) > Epsilon)
            TotalWeight += Sample.Value;
    if (TotalWeight <= Epsilon)
        return Result;
    for (const auto& Sample : Area)
    {
        const int32 Cell = Sample.Key;
        const double Cut = FMath::Min(Amount * Sample.Value / TotalWeight, CutDepth(Cell));
        if (Cut <= Epsilon)
            continue;
        const Load Removed = Edit(Cell).Cut(Cut);
        Result.Receive(Removed);
        if (Removed.Volume() > Epsilon)
            UpdateCell(Cell, -Removed.Volume());
    }
    return Result;
}

Earthwork::Load AMudTerrain::DrawFromPile(const FMudPile& Pile, FVector2D Approach, double Amount)
{
    Load Result;
    if (Amount <= Epsilon)
        return Result;
    const FVector2D Face = Pile.Center + (Approach - Pile.Center).GetSafeNormal() * (Pile.Radius * 0.36);
    const auto Area = Cells(Face, 3.75);
    const double Portion = Amount / FMath::Max(1, Area.Num());
    for (int32 Cell : Area)
    {
        const FVector2D Point(Cell % Samples, Cell / Samples);
        const double ConeHeight = Pile.Height * FMath::Max(0.0, 1.0 - FVector2D::Distance(Pile.Center, Point) / Pile.Radius);
        const double Floor = Original[Cell] - ConeHeight;
        const double Cut = FMath::Min(Portion, Heights[Cell] - Floor);
        if (Cut <= Epsilon)
            continue;
        const Load Removed = Edit(Cell).Cut(Cut);
        Result.Receive(Removed);
        UpdateCell(Cell, -Removed.Volume());
    }
    return Result;
}

void AMudTerrain::Deposit(FVector2D XY, double Radius, const Earthwork::Load& Load)
{
    const auto Area = Cells(XY, Radius);
    if (Area.IsEmpty() || Load.Volume() <= 0)
        return;
    TArray<double> Weights;
    double Total = 0;
    const double Center = Height(XY);
    for (int32 Cell : Area)
    {
        const double Distance = FVector2D::Distance(XY, FVector2D(Cell % Samples, Cell / Samples));
        const double Downhill = FMath::Clamp(Center - Heights[Cell], -0.65, 1.40);
        Total += Weights.Add_GetRef(FMath::Max(0.05, 1.0 - Distance / Radius) * (1.0 + 0.62 * Downhill));
    }
    for (int32 I = 0; I < Area.Num(); ++I)
    {
        Earthwork::Load Share;
        for (size_t M = 0; M < MaterialCount; ++M)
            Share.Volumes[M] = Load.Volumes[M] * Weights[I] / Total;
        Edit(Area[I]).Fill(Share);
        if (Share.Volume() > 0.075)
            TrackWear.Remove(Area[I]);
        UpdateCell(Area[I], Share.Volume());
    }
}

void AMudTerrain::PressTrack(FVector2D Center, FVector2D Forward, double HalfLength, double HalfWidth,
    double Outward, double Scrub, bool bTracked)
{
    const FVector2D Right(-Forward.Y, Forward.X);
    const double Wet = Wetness(Center);
    const double EdgeWidth = HalfWidth + Scrub * 0.22;
    const double Radius = FMath::Sqrt(FMath::Square(HalfLength) + FMath::Square(EdgeWidth));
    Load Displaced;
    for (const int32 Cell : Cells(Center, Radius))
    {
        const FVector2D Point(Cell % Samples, Cell / Samples);
        const FVector2D Offset = Point - Center;
        const double Along = FMath::Abs(FVector2D::DotProduct(Offset, Forward)) / HalfLength;
        const double Across = FMath::Abs(FVector2D::DotProduct(Offset, Right)) / EdgeWidth;
        if (Along >= 1 || Across >= 1)
            continue;
        const Material Kind = Surface(Point);
        const double Softness = Kind == Material::Soil ? 1.0 : (Kind == Material::Gravel ? 0.35 : 0.0);
        const double Contact = (1.0 - Along * Along) * (1.0 - Across * Across);
        const int32 OldWear = TrackWear.FindRef(Cell);
        const auto Pressure = RunningGear::PressGround(OldWear, Contact, Softness, Wet, Scrub, bTracked);
        if (Pressure.Wear == OldWear)
            continue;
        TrackWear.Add(Cell, static_cast<uint8>(Pressure.Wear));
        if (Pressure.Depth > Epsilon)
        {
            const Load Cut = Edit(Cell).Cut(Pressure.Depth);
            if (Cut.Volume() > 0)
            {
                Displaced.Receive(Cut);
                UpdateCell(Cell);
                continue;
            }
        }
        DirtyCell(Cell);
    }
    if (Displaced.Volume() <= 0)
        return;
    const FVector2D Edge = Center + Right * Outward * (EdgeWidth + 0.92);
    const double WorldEdge = GetWorldSize();
    const FVector2D Bank(FMath::Clamp(Edge.X, 0.0, WorldEdge), FMath::Clamp(Edge.Y, 0.0, WorldEdge));
    Deposit(Bank, bTracked ? 1.22 : 0.95, Displaced);
    if (Scrub > 0.22)
        ShowSpill(FVector(Bank.X, Bank.Y, Height(Bank)), Displaced.Volume(), Displaced.DominantMaterial(),
            FVector(Right.X, Right.Y, 0) * Outward);
}

void AMudTerrain::ShowSpill(FVector Origin, double Volume, Material Type, FVector Direction, double FloorZ)
{
    auto& Batch = Spills[static_cast<uint8>(Type)];
    Batch.Credit += Volume * (Type == Material::Gravel ? 26 : 18);
    const int32 Count = FMath::Min(FMath::FloorToInt(Batch.Credit), 9);
    Batch.Credit = FMath::Min(Batch.Credit - Count, 1.0);
    FVector Forward = Direction.GetSafeNormal2D();
    if (Forward.IsNearlyZero())
        Forward = FVector::ForwardVector;
    const FVector Across(-Forward.Y, Forward.X, 0);
    for (int32 I = 0; I < Count; ++I)
    {
        auto& Clump = Batch.Pieces[Batch.Next];
        Batch.Next = (Batch.Next + 1) % Batch.Pieces.Num();
        Clump.Position = Origin + Across * SpillRandom.FRandRange(-0.62, 0.62) +
            Forward * SpillRandom.FRandRange(-0.12, 0.26) + FVector(0, 0, 0.12);
        Clump.Velocity = Across * SpillRandom.FRandRange(-0.7, 0.7) +
            Forward * SpillRandom.FRandRange(0.22, 1.85) + FVector(0, 0, SpillRandom.FRandRange(-0.4, 0.88));
        Clump.Life = 1.65;
        Clump.Radius = Type == Material::Gravel ? SpillRandom.FRandRange(0.025, 0.074) : SpillRandom.FRandRange(0.045, 0.16);
        Clump.Spin = SpillRandom.FRandRange(-180, 180);
        Clump.FloorZ = FloorZ;
        Clump.bBounced = false;
    }
}

void AMudTerrain::UpdateSpills(double Dt)
{
    for (int32 Kind = 0; Kind < Spoil.Num(); ++Kind)
    {
        auto& Batch = Spills[Kind];
        bool bChanged = false;
        for (int32 I = 0; I < Batch.Pieces.Num(); ++I)
        {
            FSpill& Clump = Batch.Pieces[I];
            if (Clump.Life <= 0)
                continue;
            bChanged = true;
            Clump.Life -= Dt;
            Clump.Spin += (220 + I * 9) * Dt;
            Clump.Velocity.Z -= 9.81 * Dt;
            Clump.Position += Clump.Velocity * Dt;
            const double Ground = FMath::Max(Height(FVector2D(Clump.Position.X, Clump.Position.Y)), Clump.FloorZ);
            if (Clump.Position.Z <= Ground + Clump.Radius * 0.15)
            {
                if (Kind != static_cast<int32>(Material::Soil) && !Clump.bBounced && Clump.Velocity.Z < -0.8)
                {
                    Clump.Position.Z = Ground + Clump.Radius * 0.15;
                    Clump.Velocity *= FVector(0.56, 0.56, -0.28);
                    Clump.bBounced = true;
                }
                else
                    Clump.Life = 0;
            }
            Batch.Transforms[I] = Clump.Life > 0
                ? FTransform(FRotator(Clump.Spin, I * 61, Clump.Spin * 0.43), Clump.Position * 100,
                    FVector(1.35 + 0.08 * (I % 4), 1.06, 0.86) * Clump.Radius)
                : FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001));
        }
        if (bChanged)
            Spoil[Kind]->BatchUpdateInstancesTransforms(0, Batch.Transforms, false, true, true);
    }
}

double AMudTerrain::Grade(FVector2D XY, FVector2D Across, FVector2D Reach, double Rate)
{
    const auto Area = ToolFootprint(XY, Across, Reach);
    if (Area.Num() < 2)
        return 0;
    double Average = 0;
    double TotalWeight = 0;
    for (const auto& Sample : Area)
    {
        Average += Heights[Sample.Key] * Sample.Value;
        TotalWeight += Sample.Value;
    }
    Average /= TotalWeight;
    const double Fraction = FMath::Clamp(Rate, 0.0, 1.0);
    Load Moved;
    double Deficit = 0;
    TArray<TPair<int32, double>> LowCells;
    for (const auto& Sample : Area)
    {
        const int32 Cell = Sample.Key;
        const double Excess = Heights[Cell] - Average;
        if (Excess > Epsilon)
        {
            const Load Cut = Edit(Cell).Cut(Excess * Sample.Value * Fraction);
            Moved.Receive(Cut);
            if (Cut.Volume() > 0)
                UpdateCell(Cell, -Cut.Volume());
        }
        else if (Excess < 0.0)
        {
            Deficit -= Excess * Sample.Value;
            LowCells.Emplace(Cell, -Excess * Sample.Value);
        }
    }
    if (Deficit <= 0 || Moved.Volume() <= 0)
        return 0;
    for (const auto& Entry : LowCells)
    {
        Load Share;
        for (size_t M = 0; M < MaterialCount; ++M)
            Share.Volumes[M] = Moved.Volumes[M] * Entry.Value / Deficit;
        Edit(Entry.Key).Fill(Share);
        UpdateCell(Entry.Key, Share.Volume());
    }
    return Moved.Volume();
}

double AMudTerrain::Remaining(const FMudPile& Pile) const
{
    double Volume = 0;
    for (int32 Cell : Cells(Pile.Center, Pile.Radius))
    {
        const double Distance = FVector2D::Distance(Pile.Center, FVector2D(Cell % Samples, Cell / Samples));
        const double Before = Pile.Height * FMath::Max(0.0, 1.0 - Distance / Pile.Radius);
        const auto* Column = Changed.Find(Cell);
        if (!Column)
        {
            Volume += Before;
            continue;
        }
        const double Floor = Original[Cell] - Before;
        double Z = Column->Bedrock;
        for (const auto& Layer : Column->Layers)
        {
            if (Layer.Type == Pile.Material)
                Volume += FMath::Max(0.0, Z + Layer.Volume - FMath::Max(Z, Floor));
            Z += Layer.Volume;
        }
    }
    return Volume;
}

double AMudTerrain::EstimateFordFill(FVector2D Ford, double Clearance) const
{
    double Volume = 0;
    for (int32 Y = -3; Y <= 3; ++Y)
    {
        const auto Row = RiverAt(Ford.Y + Y);
        for (int32 X = -26; X <= 26; ++X)
            Volume += FMath::Max(0.0, WaterDepth(Row.XY + FVector2D(X, 0)) - Clearance);
    }
    return Volume;
}

void AMudTerrain::BuildTile(int32 Id, int32 Step)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MudRebuildTerrainTile);
    const int32 X0 = (Id % TilesPerSide) * TileSize, Y0 = (Id / TilesPerSide) * TileSize;
    const int32 Edge = TileSize / Step + 1;
    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UV, FinishMasks;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    const int32 BorderCount = (Edge - 1) * 4;
    const int32 VertexCount = Edge * Edge + BorderCount;
    Vertices.Reserve(VertexCount);
    Normals.Reserve(VertexCount);
    UV.Reserve(VertexCount);
    FinishMasks.Reserve(VertexCount);
    Colors.Reserve(VertexCount);
    Tangents.Reserve(VertexCount);
    Triangles.Reserve(6 * ((Edge - 1) * (Edge - 1) + BorderCount));
    for (int32 Y = 0; Y < Edge; ++Y)
    {
        for (int32 X = 0; X < Edge; ++X)
        {
            const int32 GX = X0 + X * Step, GY = Y0 + Y * Step;
            const FVector2D XY(GX, GY);
            const int32 Cell = Index(GX, GY);
            const double Z = Heights[Cell];
            const Material Kind = Surface(XY);
            const double Finish = WorkFinish.FindRef(Cell);
            bool bGrass = Kind == Material::Soil && FMath::Abs(Original[Cell] - Z) < 0.035 &&
                FMath::Abs(Finish) < 0.16;
            for (const FMudPile& Pile : Piles)
                bGrass &= FVector2D::DistSquared(XY, Pile.Center) >= FMath::Square(Pile.Radius);
            Vertices.Emplace(X * Step * 100.0, Y * Step * 100.0, Z * 100.0);
            const FVector Up = Normal(XY);
            Normals.Add(Up);
            UV.Add(XY);
            FinishMasks.Emplace(FMath::Max(0.0, -Finish), FMath::Max(0.0, Finish));
            const double Exposed = FMath::Clamp((Original[Cell] - Z) * 0.95, 0.0, 0.52);
            FLinearColor Masks = GroundMasks(Kind, FMath::Max(Wetness(XY), Exposed), Up.Z, XY, bGrass);
            const float Wear = TrackWear.FindRef(Cell) / 255.0f;
            Masks.R *= 1.0f - 0.92f * Wear;
            Masks.B *= 1.0f - 0.60f * Wear;
            Colors.Add(Masks);
            Tangents.Emplace(FVector::CrossProduct(FVector::RightVector, Up).GetSafeNormal(), false);
            if (X == 0 || Y == 0)
                continue;
            const int32 I = Y * Edge + X;
            Triangles.Append({ I - Edge - 1, I - 1, I - Edge, I - Edge, I - 1, I });
        }
    }
    // Skirts close the seam when adjoining tiles use different sample intervals.
    TArray<int32> Border;
    Border.Reserve(BorderCount);
    for (int32 I = 0; I < Edge - 1; ++I) Border.Add(I);
    for (int32 I = 0; I < Edge - 1; ++I) Border.Add(I * Edge + Edge - 1);
    for (int32 I = Edge - 1; I > 0; --I) Border.Add((Edge - 1) * Edge + I);
    for (int32 I = Edge - 1; I > 0; --I) Border.Add(I * Edge);
    const int32 Bottom = Vertices.Num();
    for (int32 I : Border)
    {
        const FVector NormalCopy = Normals[I];
        const FVector2D UVCopy = UV[I];
        const FVector2D FinishCopy = FinishMasks[I];
        const FLinearColor ColorCopy = Colors[I];
        const FProcMeshTangent TangentCopy = Tangents[I];
        Vertices.Add(Vertices[I] - FVector(0, 0, 350));
        Normals.Add(NormalCopy);
        UV.Add(UVCopy);
        FinishMasks.Add(FinishCopy);
        Colors.Add(ColorCopy);
        Tangents.Add(TangentCopy);
    }
    for (int32 I = 0; I < Border.Num(); ++I)
    {
        const int32 Next = (I + 1) % Border.Num();
        Triangles.Append({ Border[I], Border[Next], Bottom + I, Bottom + I, Border[Next], Bottom + Next });
    }

    auto* Mesh = TerrainMeshes[Id].Get();
    if (!Mesh)
    {
        Mesh = NewObject<UProceduralMeshComponent>(this);
        TerrainMeshes[Id] = Mesh;
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetRelativeLocation(FVector(X0 * 100.0, Y0 * 100.0, 0));
        Mesh->bUseAsyncCooking = true;
        Mesh->bUseComplexAsSimpleCollision = true;
        Mesh->SetMaterial(0, GroundMaterial);
        Mesh->RegisterComponent();
    }
    if (TileSteps[Id] == Step)
    {
        Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UV, FinishMasks, {}, {}, Colors, Tangents, false);
    }
    else
    {
        Mesh->SetCollisionEnabled(Step == 1 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV, FinishMasks, {}, {},
            Colors, Tangents, Step == 1, false);
    }
    TileSteps[Id] = Step;
}

void AMudTerrain::BuildGroundCover()
{
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/MeadowTuft.MeadowTuft"));
    auto* Material = GeneratedMaterial(TEXT("Meadow"));
    if (!Mesh || !Material)
        return;
    Meadow->SetStaticMesh(Mesh);
    Meadow->SetMaterial(0, Material);
    FRandomStream Scatter(Seed ^ 0x4d454144);
    TArray<FTransform> Transforms;
    const FVector2D StartXY(Start.X, Start.Y);
    const FVector2D GoalXY(Goal.X, Goal.Y);
    for (int32 Y = 5; Y < Samples - 5; Y += 5)
    {
        for (int32 X = 5; X < Samples - 5; X += 5)
        {
            const FVector2D Point(X + Scatter.FRandRange(-2, 2), Y + Scatter.FRandRange(-2, 2));
            if (FVector2D::DistSquared(Point, StartXY) < FMath::Square(12.0) ||
                FVector2D::DistSquared(Point, GoalXY) < FMath::Square(11.0) ||
                Surface(Point) != Earthwork::Material::Soil || WaterDepth(Point) > 0.015)
                continue;
            const FVector Up = Normal(Point);
            const double Wet = Wetness(Point);
            const double Cover = GroundMasks(Earthwork::Material::Soil, Wet, Up.Z, Point, true).R;
            if (Scatter.FRand() > Cover * 0.62)
                continue;
            const int32 Cell = Index(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y));
            if (PlantByCell.Contains(Cell))
                continue;
            const double Width = Scatter.FRandRange(0.74, 1.55);
            const double HeightScale = Scatter.FRandRange(0.56, 1.46);
            const FTransform Transform(FRotator(0, Scatter.FRandRange(0, 360), 0),
                FVector(Point.X, Point.Y, Height(Point)) * 100.0, FVector(Width, Width, HeightScale));
            PlantByCell.Add(Cell, Plants.Add({Transform, Cell}));
            Transforms.Add(Transform);
        }
    }
    Meadow->AddInstances(Transforms, false, false, false);
    UE_LOG(LogTemp, Display, TEXT("Valley foliage: %d trees, %d grass tufts"), Trees.Num(), Plants.Num());
}

void AMudTerrain::UpdateGroundCover()
{
    if (DirtyPlants.IsEmpty())
        return;
    bool bChanged = false;
    for (const int32 Id : DirtyPlants)
    {
        FGroundPlant& Plant = Plants[Id];
        const bool bHide = FMath::Abs(Heights[Plant.Cell] - Original[Plant.Cell]) > 0.075 ||
            FMath::Abs(WorkFinish.FindRef(Plant.Cell)) > 0.22 ||
            TrackWear.FindRef(Plant.Cell) >= 72;
        if (Plant.bHidden == bHide)
            continue;
        Plant.bHidden = bHide;
        FTransform Transform = Plant.Transform;
        if (bHide)
            Transform.SetScale3D(FVector(0.001));
        Meadow->UpdateInstanceTransform(Id, Transform, false, false, true);
        bChanged = true;
    }
    if (bChanged)
        Meadow->MarkRenderStateDirty();
    DirtyPlants.Reset();
}

void AMudTerrain::UpdateTiles(FVector2D Focus)
{
    UpdateGroundCover();
    TArray<TPair<double, int32>> Candidates;
    for (int32 Id = 0; Id < TerrainMeshes.Num(); ++Id)
    {
        const FVector2D Center((Id % TilesPerSide + 0.5) * TileSize, (Id / TilesPerSide + 0.5) * TileSize);
        const double Distance = FVector2D::DistSquared(Center, Focus);
        const int32 Target = Distance < FMath::Square(TileSteps[Id] == 1 ? 170.0 : 145.0) ? 1 : 4;
        if (Dirty.Contains(Id) || TileSteps[Id] != Target)
            Candidates.Emplace(Distance, Id);
    }
    Candidates.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < FMath::Min(Candidates.Num(), 2); ++I)
    {
        const auto Entry = Candidates[I];
        BuildTile(Entry.Value, Entry.Key < FMath::Square(TileSteps[Entry.Value] == 1 ? 170.0 : 145.0) ? 1 : 4);
        Dirty.Remove(Entry.Value);
    }
}

void AMudTerrain::BuildWater()
{
    auto* Mesh = NewObject<UProceduralMeshComponent>(this);
    Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    Mesh->SetMaterial(0, GeneratedMaterial(TEXT("River")));
    TArray<FVector> Points, Normals;
    TArray<FVector2D> UV;
    TArray<int32> Triangles;
    for (int32 I = 0; I < River.Num(); ++I)
    {
        const FRiverSample& Sample = River[I];
        for (double Side : {-1.0, 1.0})
        {
            const double X = Sample.XY.X + Side * (Sample.Width * 0.5 + 14.0);
            Points.Emplace(X * 100, Sample.XY.Y * 100, Sample.Z * 100);
            Normals.Add(FVector::UpVector);
            UV.Emplace(X, Sample.XY.Y);
        }
        const int32 J = I * 2;
        if (J > 0)
            Triangles.Append({ J - 2, J, J - 1, J - 1, J, J + 1 });
    }
    Mesh->CreateMeshSection_LinearColor(0, Points, Triangles, Normals, UV, {}, {}, false, false);
    Mesh->RegisterComponent();
}

void AMudTerrain::BuildMarkers()
{
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (bool bStart : {true, false})
    {
        const FVector Position = bStart ? Start : Goal;
        auto* Paint = GeneratedMaterial(bStart ? TEXT("GoldMarker") : TEXT("RedMarker"));
        const auto Add = [&](FVector Offset, FVector Size)
        {
            auto* Part = NewObject<UStaticMeshComponent>(this);
            Part->SetupAttachment(RootComponent);
            Part->SetStaticMesh(Cube);
            Part->SetMaterial(0, Paint);
            Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Part->SetRelativeLocation(Position * 100 + Offset);
            Part->SetRelativeScale3D(Size);
            Part->RegisterComponent();
        };
        for (double Side : {-1.0, 1.0})
        {
            if (bStart)
            {
                Add(FVector(0, Side * 410, 55), FVector(0.22, 0.22, 1.1));
                Add(FVector(0, Side * 410, 115), FVector(0.12, 0.44, 0.16));
            }
            else
                Add(FVector(0, Side * 410, 245), FVector(0.24, 0.24, 4.9));
        }
        if (!bStart)
            Add(FVector(0, 0, 500), FVector(0.26, 8.45, 0.27));
    }
}

bool AMudTerrain::HitsTree(FVector2D XY, double Radius, bool bClear)
{
    bool bBlocked = false;
    TSet<int32> Tiles;
    for (const double DX : {-Radius, Radius})
        for (const double DY : {-Radius, Radius})
            Tiles.Add(TileIndex(XY.X + DX, XY.Y + DY));
    for (int32 Tile : Tiles)
    {
        for (auto It = TreeTiles.CreateConstKeyIterator(Tile); It; ++It)
        {
            const int32 Id = It.Value();
            if (Trees[Id].bRemoved || FVector2D::DistSquared(XY, Trees[Id].XY) > FMath::Square(Radius))
                continue;
            if (bClear)
                HideTree(Id);
            else
                bBlocked = true;
        }
    }
    return bBlocked;
}

void AMudTerrain::HideTree(int32 Id)
{
    if (Trees[Id].bRemoved)
        return;
    Trees[Id].bRemoved = true;
    FTransform Hidden = Trees[Id].Trunk;
    Hidden.SetScale3D(FVector(0.001));
    Hidden.AddToTranslation(FVector(0, 0, -2000));
    Trunks->UpdateInstanceTransform(Id, Hidden, false, true, true);
    for (int32 Tier = 0; Tier < CrownInstancesPerTree; ++Tier)
        Crowns->UpdateInstanceTransform(Id * CrownInstancesPerTree + Tier, Hidden, false, Tier == CrownInstancesPerTree - 1, true);
}

TSharedRef<FJsonObject> AMudTerrain::SaveTerrain() const
{
    auto Data = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Edits, Removed, Tracks;
    for (const auto& Entry : Changed)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("cell"), Entry.Key);
        TArray<TSharedPtr<FJsonValue>> JsonLayers;
        for (const auto& Layer : Entry.Value.Layers)
        {
            JsonLayers.Add(MakeShared<FJsonValueNumber>(static_cast<int32>(Layer.Type)));
            JsonLayers.Add(MakeShared<FJsonValueNumber>(Layer.Volume));
        }
        Row->SetArrayField(TEXT("layers"), JsonLayers);
        if (const float* Finish = WorkFinish.Find(Entry.Key))
            Row->SetNumberField(TEXT("finish"), *Finish);
        Edits.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (int32 Id = 0; Id < Trees.Num(); ++Id)
        if (Trees[Id].bRemoved)
            Removed.Add(MakeShared<FJsonValueNumber>(Id));
    Data->SetArrayField(TEXT("edits"), Edits);
    Data->SetArrayField(TEXT("removedTrees"), Removed);
    for (const auto& Entry : TrackWear)
    {
        Tracks.Add(MakeShared<FJsonValueNumber>(Entry.Key));
        Tracks.Add(MakeShared<FJsonValueNumber>(Entry.Value));
    }
    Data->SetArrayField(TEXT("trackWear"), Tracks);
    return Data;
}

bool AMudTerrain::RestoreTerrain(const TSharedPtr<FJsonObject>& Data)
{
    const TArray<TSharedPtr<FJsonValue>> *Edits, *Removed;
    if (!Data.IsValid() || !Data->TryGetArrayField(TEXT("edits"), Edits) ||
        !Data->TryGetArrayField(TEXT("removedTrees"), Removed))
        return false;
    TMap<int32, Earthwork::Column> Restored;
    TMap<int32, float> RestoredFinish;
    TMap<int32, uint8> RestoredTracks;
    TSet<int32> RemovedIds;
    if (Data->HasField(TEXT("trackWear")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Tracks;
        if (!Data->TryGetArrayField(TEXT("trackWear"), Tracks) || Tracks->Num() % 2)
            return false;
        for (int32 I = 0; I < Tracks->Num(); I += 2)
        {
            double Cell, Wear;
            if (!(*Tracks)[I]->TryGetNumber(Cell) || Cell < 0 || Cell >= Original.Num() || Cell != FMath::FloorToDouble(Cell) ||
                !(*Tracks)[I + 1]->TryGetNumber(Wear) || Wear < 1 || Wear > 255 || Wear != FMath::FloorToDouble(Wear))
                return false;
            RestoredTracks.Add(static_cast<int32>(Cell), static_cast<uint8>(Wear));
        }
    }
    for (const auto& Value : *Edits)
    {
        const TSharedPtr<FJsonObject>* Row;
        const TArray<TSharedPtr<FJsonValue>>* JsonLayers;
        int32 Cell;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetNumberField(TEXT("cell"), Cell) ||
            !Original.IsValidIndex(Cell) || !(*Row)->TryGetArrayField(TEXT("layers"), JsonLayers) || JsonLayers->Num() % 2)
            return false;
        auto Column = OriginalColumn(Cell);
        if ((*Row)->HasField(TEXT("finish")))
        {
            double Finish;
            if (!(*Row)->TryGetNumberField(TEXT("finish"), Finish) || !FMath::IsFinite(Finish) || FMath::Abs(Finish) > 1)
                return false;
            RestoredFinish.Add(Cell, static_cast<float>(Finish));
        }
        Column.Layers.clear();
        for (int32 I = 0; I < JsonLayers->Num(); I += 2)
        {
            double Type, Volume;
            if (!(*JsonLayers)[I]->TryGetNumber(Type) || !(*JsonLayers)[I + 1]->TryGetNumber(Volume) ||
                Type < 0 || Type >= static_cast<double>(Material::Count) || Type != FMath::FloorToDouble(Type) ||
                !FMath::IsFinite(Volume) || Volume < 0)
                return false;
            Column.Add(static_cast<Material>(static_cast<uint8>(Type)), Volume);
        }
        Restored.Add(Cell, MoveTemp(Column));
    }
    for (const auto& Value : *Removed)
    {
        double Id;
        if (!Value->TryGetNumber(Id) || Id < 0 || Id >= Trees.Num() || Id != FMath::FloorToDouble(Id))
            return false;
        RemovedIds.Add(static_cast<int32>(Id));
    }
    for (const auto& Entry : Changed)
        DirtyCell(Entry.Key);
    for (const auto& Entry : TrackWear)
        DirtyCell(Entry.Key);
    TrackWear = MoveTemp(RestoredTracks);
    for (const auto& Entry : TrackWear)
        DirtyCell(Entry.Key);
    Changed = MoveTemp(Restored);
    WorkFinish = MoveTemp(RestoredFinish);
    Heights = Original;
    for (const auto& Entry : Changed)
        UpdateCell(Entry.Key);
    for (int32 Id = 0; Id < Trees.Num(); ++Id)
    {
        if (RemovedIds.Contains(Id))
            HideTree(Id);
        else if (Trees[Id].bRemoved)
        {
            Trees[Id].bRemoved = false;
            Trunks->UpdateInstanceTransform(Id, Trees[Id].Trunk, false, true, true);
            for (int32 Tier = 0; Tier < CrownInstancesPerTree; ++Tier)
                Crowns->UpdateInstanceTransform(Id * CrownInstancesPerTree + Tier, Trees[Id].Crown[Tier],
                    false, Tier == CrownInstancesPerTree - 1, true);
        }
    }
    return true;
}
