#include "MudTerrain.h"
#include "TerrainSurface.h"
#include "MudMachine.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
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
    const double Patch = 0.52 + 0.66 * FMath::PerlinNoise2D(XY * 0.036)
        + 0.32 * FMath::PerlinNoise2D(XY * 0.13);
    const double Wet = FMath::SmoothStep(0.12, 0.77, Moisture);
    const double Growth = bUndisturbed ? FMath::SmoothStep(0.20, 0.73, Patch)
        * FMath::SmoothStep(0.66, 0.94, SlopeUp) * (1.0 - Wet) : 0.0;
    const double Aggregate = Type == Material::Gravel ? 1.0 : 0.0;
    const double Tone = 0.52 + 0.16 * FMath::PerlinNoise2D(XY * 0.036)
        + 0.06 * FMath::PerlinNoise2D(XY * 0.11);
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
    Meadow->SetCullDistances(3600, 6500);
    BoulderMesh = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Boulders"));
    BoulderMesh->SetupAttachment(RootComponent);
    BoulderMesh->SetCollisionProfileName(TEXT("BlockAll"));
    BoulderMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    BoulderMesh->SetCullDistances(22000,36000);
    for (const TCHAR* Name : {TEXT("FallingSoil"), TEXT("FallingGravel"), TEXT("FallingRock"), TEXT("FallingSand"), TEXT("FallingClay")})
    {
        auto* Pieces = CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(Name));
        Pieces->SetupAttachment(RootComponent);
        Pieces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pieces->SetCastShadow(false);
        Pieces->SetCullDistances(5500, 9500);
        Spoil.Add(Pieces);
    }
    for (const TCHAR* Name : {TEXT("GroundClods"), TEXT("GroundGravel"), TEXT("GroundStones"), TEXT("GroundSand"), TEXT("GroundClay")})
    {
        auto* Pieces = CreateDefaultSubobject<UInstancedStaticMeshComponent>(FName(Name));
        Pieces->SetupAttachment(RootComponent);
        Pieces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pieces->SetCastShadow(false);
        Pieces->SetCullDistances(2000, 3200);
        GroundRubble.Add(Pieces);
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
    MaterialTable = DefaultMaterialTable;
    const TCHAR* MaterialNames[] = {TEXT("Soil"), TEXT("Gravel"), TEXT("Rock"), TEXT("Sand"), TEXT("Clay")};
    for (size_t I = 0; I < MaterialCount; ++I)
    {
        auto& P = MaterialTable[I];
        const FString Section = FString(TEXT("MudSoil.")) + MaterialNames[I];
        const auto Read = [&](const TCHAR* Key, double& Value, double Minimum, double Maximum)
        {
            double Configured;
            if (GConfig->GetDouble(*Section, Key, Configured, GGameIni) && FMath::IsFinite(Configured))
                Value = FMath::Clamp(Configured, Minimum, Maximum);
        };
        Read(TEXT("DryFriction"), P.DryFriction, 0.02, 1.5);
        Read(TEXT("WetFriction"), P.WetFriction, 0.02, 1.5);
        Read(TEXT("DryRepose"), P.DryRepose, 5, 75);
        Read(TEXT("WetRepose"), P.WetRepose, 5, 75);
        Read(TEXT("Cohesion"), P.Cohesion, 0, 1);
        Read(TEXT("Bearing"), P.Bearing, 0, 1);
        Read(TEXT("MaxCompression"), P.MaxCompression, 0, 0.45);
        Read(TEXT("DigResistance"), P.DigResistance, 0.1, 20);
    }
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
    Data->TryGetStringField(TEXT("worldId"),WorldId);
    for(TCHAR C:WorldId)if(!FChar::IsAlnum(C)&&C!='-'&&C!='_')return false;
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
    SurfaceHalfSamples.Reset();
    SoilFlow.Reset(Samples);
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
        const Material Kind = Type == TEXT("gravel") ? Material::Gravel : Type == TEXT("sand") ? Material::Sand :
            Type == TEXT("clay") ? Material::Clay : (Type == TEXT("crushed-rock") ? Material::Rock : Material::Soil);
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
    const TArray<TSharedPtr<FJsonValue>>* Regions;
    if(Data->TryGetArrayField(TEXT("geology"),Regions))for(const auto& V:*Regions)
    {
        const auto& R=*V->AsObject();
        const FString Name=R.GetStringField(TEXT("material"));
        const auto Type=Name==TEXT("clay")?Material::Clay:Name==TEXT("sand")?Material::Sand:
            Name==TEXT("gravel")?Material::Gravel:Name==TEXT("crushed-rock")?Material::Rock:Material::Soil;
        const double RX=R.GetNumberField(TEXT("rx")),RY=R.GetNumberField(TEXT("ry")),Depth=R.GetNumberField(TEXT("soilDepth"));
        if(RX<=0||RY<=0||Depth<0||Depth>256)return false;
        Geology.Add({ReadXY(R),FVector2D(RX,RY),Type,Depth});
    }
    BoulderMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    BoulderMesh->SetMaterial(0,GeneratedMaterial(TEXT("LooseRock")));
    const TArray<TSharedPtr<FJsonValue>>* Rocks;
    if(Data->TryGetArrayField(TEXT("boulders"),Rocks))for(const auto& V:*Rocks)
    {
        const auto& R=*V->AsObject();const FVector2D P=ReadXY(R);const double Radius=R.GetNumberField(TEXT("radius"));
        BoulderTiles.Add(TileIndex(P.X,P.Y),Boulders.Add({P,Radius}));
        BoulderMesh->AddInstance(FTransform(FRotator(11,FMath::Fmod(P.X*17+P.Y*13,360),17),
            FVector(P.X,P.Y,Height(P)+Radius*.35)*100,FVector(Radius*2,Radius*1.65,Radius*1.4)));
    }
    for (const auto& Value : RiverData.GetArrayField(TEXT("fords")))
        Fords.Add(ReadXY(*Value->AsObject()));
    Data->TryGetBoolField(TEXT("roadContract"), bRoadContract);
    bRoadContract &= !Fords.IsEmpty();
    FordSurveys.SetNum(Fords.Num());
    const TSharedPtr<FJsonObject>* CreekData = nullptr;
    if (Data->TryGetObjectField(TEXT("creek"), CreekData))
    {
        for (const auto& Value : (*CreekData)->GetArrayField(TEXT("centerline")))
        {
            const auto& Point = *Value->AsObject();
            Creek.Add({ ReadXY(Point), Point.GetNumberField(TEXT("z")), Point.GetNumberField(TEXT("width")) });
        }
        if (Creek.Num() < 2 || Creek.Last().XY.X <= Creek[0].XY.X)
            return false;
    }

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
        Entry.Root.Diameter=TrunkWidth*Scale;Entry.Root.Height=7.5*Scale;
        Tree.TryGetNumberField(TEXT("diameter"),Entry.Root.Diameter);
        Tree.TryGetNumberField(TEXT("height"),Entry.Root.Height);
        Entry.Trunk = FTransform(FRotator::ZeroRotator, Position + FVector(0,0,Entry.Root.Height*50),
            FVector(Entry.Root.Diameter,Entry.Root.Diameter,Entry.Root.Height));
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
    bSoilLab = FParse::Param(FCommandLine::Get(), TEXT("MudSoilLab"));
    if(bSoilLab)WorldId+=TEXT("-soil-lab");
    if (bSoilLab) InitializeSoilLab();
    BuildGroundCover();
    auto* ClumpMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/SpoilClump.SpoilClump"));
    auto* FallingMesh = ClumpMesh ? ClumpMesh :
        LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* QuarryMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Generated/QuarryFragment.QuarryFragment"));
    const TCHAR* Materials[] = { TEXT("Loose"), TEXT("LooseGravel"), TEXT("LooseRock"), TEXT("LooseSand"), TEXT("LooseClay") };
    for (int32 Kind = 0; Kind < Spoil.Num(); ++Kind)
    {
        auto* ClumpMaterial = GeneratedMaterial(Materials[Kind]);
        if (!ClumpMaterial) ClumpMaterial = GeneratedMaterial(TEXT("Loose"));
        const bool Aggregate = IsAggregate(static_cast<Material>(Kind));
        Spoil[Kind]->SetStaticMesh(Aggregate && QuarryMesh ? QuarryMesh : FallingMesh);
        Spoil[Kind]->SetMaterial(0, ClumpMaterial);
        GroundRubble[Kind]->SetStaticMesh(Aggregate && QuarryMesh ? QuarryMesh : ClumpMesh);
        GroundRubble[Kind]->SetMaterial(0, ClumpMaterial);
        auto& Batch = Spills[Kind];
        Batch.Pieces.SetNum(36);
        Batch.Transforms.Init(FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001)), Batch.Pieces.Num());
        Spoil[Kind]->AddInstances(Batch.Transforms, false, false, false);
    }

    TerrainMeshes.SetNum(TilesPerSide * TilesPerSide);
    TileSteps.Init(-1, TerrainMeshes.Num());
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
    return MeshHeight(XY);
}

double AMudTerrain::SurfaceSample(int32 X, int32 Y) const
{
    const int32 FineSize = Samples * 2 - 1;
    X = FMath::Clamp(X, 0, FineSize-1); Y = FMath::Clamp(Y, 0, FineSize-1);
    if ((X % 2) == 0 && (Y % 2) == 0) return Heights[Index(X/2,Y/2)];
    const int32 Key = Y * FineSize + X;
    if (const float* Cached = SurfaceHalfSamples.Find(Key)) return *Cached;
    const float Value = TerrainSurface::Reconstruct(X * 0.5, Y * 0.5, [this](int I, int J)
    { return Heights[Index(FMath::Clamp(I,0,Samples-1), FMath::Clamp(J,0,Samples-1))]; });
    SurfaceHalfSamples.Add(Key, Value);
    return Value;
}

double AMudTerrain::MeshHeight(FVector2D XY, FVector* SurfaceNormal) const
{
    const double X = FMath::Clamp(XY.X*2, 0.0, (Samples-1)*2.0);
    const double Y = FMath::Clamp(XY.Y*2, 0.0, (Samples-1)*2.0);
    const int32 I = FMath::Min(FMath::FloorToInt(X), (Samples-1)*2-1);
    const int32 J = FMath::Min(FMath::FloorToInt(Y), (Samples-1)*2-1);
    const double U = X-I, V = Y-J;
    const double SW = SurfaceSample(I,J), SE = SurfaceSample(I+1,J);
    const double NW = SurfaceSample(I,J+1), NE = SurfaceSample(I+1,J+1);
    if (SurfaceNormal)
    {
        const double DX = (U+V <= 1 ? SE-SW : NE-NW)*2;
        const double DY = (U+V <= 1 ? NW-SW : NE-SE)*2;
        *SurfaceNormal = FVector(-DX,-DY,1).GetSafeNormal();
    }
    return TerrainSurface::Triangle(SW,SE,NW,NE,U,V);
}

FVector AMudTerrain::Normal(FVector2D XY) const
{
    return FVector(Height(XY-FVector2D(0.5,0))-Height(XY+FVector2D(0.5,0)),
        Height(XY-FVector2D(0,0.5))-Height(XY+FVector2D(0,0.5)),1).GetSafeNormal();
}

AMudTerrain::FRiverSample AMudTerrain::RiverAt(double Y) const
{
    return SampleChannel(River, Y / 8.0);
}

AMudTerrain::FRiverSample AMudTerrain::CreekAt(double X) const
{
    const double Span = Creek.Last().XY.X - Creek[0].XY.X;
    return SampleChannel(Creek, (X - Creek[0].XY.X) * (Creek.Num() - 1) / Span);
}

AMudTerrain::FRiverSample AMudTerrain::SampleChannel(const TArray<FRiverSample>& Channel, double Row)
{
    Row = FMath::Clamp(Row, 0.0, static_cast<double>(Channel.Num() - 1));
    const int32 I = FMath::Min(FMath::FloorToInt(Row), Channel.Num() - 2);
    const double Fraction = Row - I;
    return { FMath::Lerp(Channel[I].XY, Channel[I + 1].XY, Fraction),
        FMath::Lerp(Channel[I].Z, Channel[I + 1].Z, Fraction),
        FMath::Lerp(Channel[I].Width, Channel[I + 1].Width, Fraction) };
}

FVector AMudTerrain::WaterSoundPoint(FVector2D XY) const
{
    auto Sample = RiverAt(XY.Y);
    if (!Creek.IsEmpty())
    {
        const auto Brook = CreekAt(XY.X);
        if (FVector2D::DistSquared(Brook.XY, XY) < FVector2D::DistSquared(Sample.XY, XY))
            Sample = Brook;
    }
    return FVector(Sample.XY.X, Sample.XY.Y, Sample.Z) * 100;
}

double AMudTerrain::WaterDepth(FVector2D XY) const
{
    const auto Point = RiverAt(XY.Y);
    double SurfaceZ = FMath::Abs(XY.X - Point.XY.X) <= Point.Width * 0.5 + 12.0 ? Point.Z : -BIG_NUMBER;
    if (!Creek.IsEmpty() && XY.X >= Creek[0].XY.X && XY.X <= Creek.Last().XY.X)
    {
        const auto Brook = CreekAt(XY.X);
        if (FMath::Abs(XY.Y - Brook.XY.Y) <= Brook.Width * 0.5 + CreekMargin)
            SurfaceZ = FMath::Max(SurfaceZ, Brook.Z);
    }
    return SurfaceZ > -BIG_NUMBER ? FMath::Max(0.0, SurfaceZ - Height(XY)) : 0;
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
    if (!Creek.IsEmpty() && XY.X >= Creek[0].XY.X && XY.X <= Creek.Last().XY.X)
    {
        const auto Brook = CreekAt(XY.X);
        const double BankDistance = FMath::Abs(XY.Y - Brook.XY.Y);
        Wet = FMath::Max(Wet, 0.73 * (1.0 - FMath::SmoothStep(Brook.Width * 0.5, Brook.Width * 0.5 + 9.0, BankDistance)));
    }
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
    return bSoilLab || !WorldId.IsEmpty() ? OriginalColumn(Index(X, Y)).Surface() : Material::Soil;
}

double AMudTerrain::AggregateCover(int32 Cell) const
{
    double Cover = 0;
    if (const auto* Column = Changed.Find(Cell))
        for (auto Layer = Column->Layers.rbegin(); Layer != Column->Layers.rend() && IsAggregate(Layer->Type); ++Layer)
            Cover += Layer->Height();
    return Cover;
}

double AMudTerrain::Grip(FVector2D XY) const
{
    const Material Type = Surface(XY);
    const int32 Cell = Index(FMath::Clamp(FMath::RoundToInt(XY.X), 0, Samples - 1),
        FMath::Clamp(FMath::RoundToInt(XY.Y), 0, Samples - 1));
    const auto* Column = Changed.Find(Cell);
    const double Packing = Column && !Column->Layers.empty() ? Column->Layers.back().Packing : 0;
    return FMath::Clamp(SurfaceGrip(Type, Wetness(XY), WaterDepth(XY)) +
        Packing * 0.18 * (1.0 - Wetness(XY)), 0.12, 1.0);
}

double AMudTerrain::ContactSink(FVector2D XY, bool bTracked, double LoadFraction) const
{
    const double Wet = Wetness(XY);
    if (Wet <= 0.20)
        return 0;
    const double Limit = GetWorldSize();
    XY = FVector2D(FMath::Clamp(XY.X, 0.0, Limit), FMath::Clamp(XY.Y, 0.0, Limit));
    const FVector2D Cell(FMath::FloorToDouble(XY.X), FMath::FloorToDouble(XY.Y));
    const FVector2D Offset = XY - Cell;
    const auto YieldAt = [this](FVector2D Point)
    {
        const Material Ground = Surface(Point);
        const int32 CellIndex = Index(FMath::Min(FMath::RoundToInt(Point.X), Samples - 1),
            FMath::Min(FMath::RoundToInt(Point.Y), Samples - 1));
        if (!IsAggregate(Ground))
        {
            const auto* Column = Changed.Find(CellIndex);
            const double Packing = Column && !Column->Layers.empty() ? Column->Layers.back().Packing : 0;
            return (1.0 - Properties(Ground).Bearing) * (1.0 - Packing * 0.65);
        }
        const double Firmness = Changed.Contains(CellIndex) ? FMath::SmoothStep(0.0, 0.16, AggregateCover(CellIndex)) : 1.0;
        return FMath::Lerp(1.0, Ground == Material::Gravel ? 0.16 : 0.0, Firmness);
    };
    const double Softness = FMath::BiLerp(YieldAt(Cell), YieldAt(Cell + FVector2D(1, 0)),
        YieldAt(Cell + FVector2D(0, 1)), YieldAt(Cell + FVector2D(1, 1)), Offset.X, Offset.Y);
    const auto WearAt = [&](FVector2D Point)
    {
        return TrackWear.FindRef(Index(FMath::Min(FMath::RoundToInt(Point.X), Samples - 1),
            FMath::Min(FMath::RoundToInt(Point.Y), Samples - 1))) / 255.0;
    };
    const double Wear = FMath::BiLerp(WearAt(Cell), WearAt(Cell + FVector2D(1, 0)),
        WearAt(Cell + FVector2D(0, 1)), WearAt(Cell + FVector2D(1, 1)), Offset.X, Offset.Y);
    return RunningGear::ContactSink(Softness, Wet, LoadFraction, bTracked, Wear);
}

Earthwork::Column AMudTerrain::OriginalColumn(int32 Cell) const
{
    const FVector2D XY(Cell % Samples, Cell / Samples);
    if (bSoilLab && XY.X >= 128 && XY.X <= 320 && XY.Y >= 720 && XY.Y <= 900)
    {
        const Material Type = XY.X < 190 ? Material::Soil : XY.X < 230 ? Material::Sand :
            XY.X < 270 ? Material::Clay : XY.X < 300 ? Material::Gravel : Material::Rock;
        return {Original[Cell] - 5.0, {{Material::Rock, 2.0}, {Type, 3.0}}};
    }
    if(!WorldId.IsEmpty())
    {
        const int32 X=Cell%Samples,Y=Cell/Samples;
        const double Slope=FMath::Sqrt(FMath::Square((Original[Index(FMath::Min(X+1,Samples-1),Y)]-Original[Index(FMath::Max(X-1,0),Y)])*.5)+
            FMath::Square((Original[Index(X,FMath::Min(Y+1,Samples-1))]-Original[Index(X,FMath::Max(Y-1,0))])*.5));
        Material Type=Slope>.85?Material::Rock:Material::Soil;double Depth=Type==Material::Rock?0:2.2;
        for(const auto& R:Geology)if(FVector2D((XY.X-R.XY.X)/R.Radii.X,(XY.Y-R.XY.Y)/R.Radii.Y).SizeSquared()<1){Type=R.Type;Depth=R.Depth;}
        double Deposit=0;Material Cover=Material::Soil;
        for(const auto& P:Piles){const double D=P.Height*(1-FVector2D::Distance(P.Center,XY)/P.Radius);if(D>0){Deposit=D;Cover=P.Material;break;}}
        const double ColumnDepth=FMath::Max(32.0,Depth+8.0);
        Column Result{Original[Cell]-Deposit-ColumnDepth,{}};Result.Add(Material::Rock,ColumnDepth-Depth);Result.Add(Type,Depth);Result.Add(Cover,Deposit);return Result;
    }
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
    const double Balance = Changed[Cell].Mass() - OriginalColumn(Cell).Mass();
    MassDelta += Balance - AccountedMass.FindRef(Cell);
    AccountedMass.Add(Cell, Balance);
    Heights[Cell] = Changed[Cell].Height();
    if(Heights[Cell]<Original[Cell]-.35)UpdateTreeSupport(Cell);
    SoilFlow.WakeArea(Cell);
    if (FMath::Abs(WorkDelta) > Epsilon)
    {
        const double Blend = 1.0 - FMath::Exp(-FMath::Abs(WorkDelta) * 16.0);
        WorkFinish.Add(Cell, static_cast<float>(FMath::Lerp<double>(WorkFinish.FindRef(Cell), FMath::Sign(WorkDelta), Blend)));
    }
    DirtyCell(Cell);
}

void AMudTerrain::DirtyCell(int32 Cell)
{
    const int32 CX = Cell % Samples, CY = Cell / Samples, FineSize = Samples*2-1;
    for (int32 Y = FMath::Max(0,CY*2-4); Y <= FMath::Min(FineSize-1,CY*2+4); ++Y)
        for (int32 X = FMath::Max(0,CX*2-4); X <= FMath::Min(FineSize-1,CX*2+4); ++X)
            if (X%2 || Y%2) SurfaceHalfSamples.Remove(Y*FineSize+X);
    if (const int32* Plant = PlantByCell.Find(Cell))
        DirtyPlants.Add(*Plant);
    const int32 X = Cell % Samples, Y = Cell / Samples;
    if (!bRubbleDirty && !WorkFinish.IsEmpty() &&
        FVector2D::DistSquared(RubbleFocus, FVector2D(X, Y)) <= FMath::Square(RubbleRadius + 2.0))
        bRubbleDirty = true;
    for (const int32 DX : {-3, 3})
        for (const int32 DY : {-3, 3})
        {
            const int32 Id=TileIndex(X+DX,Y+DY);
            Dirty.Add(Id);
            const int32 X0=(Id%TilesPerSide)*TileSize,Y0=(Id/TilesPerSide)*TileSize;
            const FIntRect Area(FMath::Clamp(X-X0-3,0,TileSize),FMath::Clamp(Y-Y0-3,0,TileSize),
                FMath::Clamp(X-X0+4,0,TileSize+1),FMath::Clamp(Y-Y0+4,0,TileSize+1));
            if (auto* Previous=DirtyRegions.Find(Id))
            {
                Previous->Min.X=FMath::Min(Previous->Min.X,Area.Min.X); Previous->Min.Y=FMath::Min(Previous->Min.Y,Area.Min.Y);
                Previous->Max.X=FMath::Max(Previous->Max.X,Area.Max.X); Previous->Max.Y=FMath::Max(Previous->Max.Y,Area.Max.Y);
            }
            else DirtyRegions.Add(Id,Area);
        }
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

TArray<TPair<int32, double>> AMudTerrain::ToolFootprint(FVector2D XY, FVector2D Across, FVector2D Reach, bool bBladePass) const
{
    TArray<TPair<int32, double>> Result;
    const FVector2D Along(Across.Y, -Across.X);
    const double Radius = bBladePass ? Reach.Size()+.71 : FMath::Max(Reach.X, Reach.Y);
    for (int32 Cell : Cells(XY, Radius))
    {
        const FVector2D Offset = FVector2D(Cell % Samples, Cell / Samples) - XY;
        const double U = FMath::Square(FVector2D::DotProduct(Offset, Across) / Reach.X);
        const double V = FMath::Square(FVector2D::DotProduct(Offset, Along) / Reach.Y);
        double Weight=1.0-U-V;
        if(bBladePass)
        {
            Weight=0;
            // Coverage of the cell, not just its centre: contact with the reconstructed
            // surface can begin before the tool/chassis reaches the control node.
            for(int Y=0;Y<4;++Y)for(int X=0;X<4;++X)
            {
                const FVector2D D=Offset+FVector2D((X+.5)*.25-.5,(Y+.5)*.25-.5);
                const double A=FVector2D::DotProduct(D,Across)/Reach.X,B=FVector2D::DotProduct(D,Along)/Reach.Y;
                Weight+=FMath::Max(0.,1-A*A)*FMath::Max(0.,1-B*B)/16;
            }
        }
        if (Weight > Epsilon)
            Result.Emplace(Cell, Weight);
    }
    return Result;
}

Earthwork::Load AMudTerrain::CutPass(FVector2D XY, FVector2D Across, FVector2D Reach, double Amount, double ToolHeight,
    FVector2D Slopes, bool bBladePass)
{
    Load Result;
    if (Amount <= Epsilon)
        return Result;
    const auto Area = ToolFootprint(XY, Across, Reach, bBladePass);
    const FVector2D Along(Across.Y, -Across.X);
    const auto CutDepth = [&](int32 Cell)
    {
        const FVector2D Offset = FVector2D(Cell % Samples, Cell / Samples) - XY;
        const double CutPlane = ToolHeight + FVector2D::DotProduct(Offset, Across) * Slopes.X +
            FVector2D::DotProduct(Offset, Along) * Slopes.Y;
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
        const double Resistance = Properties(Surface(FVector2D(Cell % Samples, Cell / Samples))).DigResistance;
        const double Cut = Amount * Sample.Value / TotalWeight / FMath::Max(1.0, Resistance);
        if (Cut <= Epsilon)
            continue;
        const Load Removed = Edit(Cell).CutToHeight(Heights[Cell] - CutDepth(Cell), Cut);
        Result.Receive(Removed);
        if (Removed.Volume() > Epsilon)
            UpdateCell(Cell, -Removed.Volume());
    }
    return Result;
}

Earthwork::Load AMudTerrain::SweepCut(FVector From, FVector To, FVector2D Across, FVector2D Reach, double Amount, FVector2D Slopes)
{
    Load Result;
    if (FVector::Dist(From, To) > 5) From = To;
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector::Dist(From, To) / 0.25), 1, 24);
    for (int32 I = 1; I <= Steps; ++I)
    {
        const FVector Point = FMath::Lerp(From, To, double(I) / Steps);
        Result.Receive(CutPass(FVector2D(Point.X, Point.Y), Across, Reach, Amount / Steps, Point.Z, Slopes, true));
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
        const Load Removed = Edit(Cell).CutToHeight(Floor, Portion);
        Result.Receive(Removed);
        UpdateCell(Cell, -Removed.Volume());
    }
    return Result;
}

void AMudTerrain::Deposit(FVector2D XY, double Radius, const Earthwork::Load& Load)
{
    if (Load.Volume() <= 0)
        return;
    TArray<TPair<int32, double>> Area;
    const double Center = Height(XY);
    for (int32 Cell : Cells(XY, Radius))
    {
        const double Distance = FVector2D::Distance(XY, FVector2D(Cell % Samples, Cell / Samples));
        const double Downhill = FMath::Clamp(Center - Heights[Cell], -0.65, 1.40);
        Area.Emplace(Cell, FMath::Max(0.05, 1.0 - Distance / Radius) * (1.0 + 0.62 * Downhill));
    }
    if (Area.IsEmpty())
        Area.Emplace(Index(FMath::Clamp(FMath::RoundToInt(XY.X), 0, Samples - 1),
            FMath::Clamp(FMath::RoundToInt(XY.Y), 0, Samples - 1)), 1.0);
    ApplyFill(Area, Load);
}

void AMudTerrain::Pour(FVector Origin, FVector Direction, double Radius, const Earthwork::Load& Load)
{
    if (Load.Volume() <= Epsilon)
        return;
    const Material Kind = Load.DominantMaterial();
    const FVector Velocity = Direction.GetSafeNormal2D() * (Kind == Material::Soil ? 0.79 : 1.15) - FVector(0, 0, 0.32);
    Pours.Add({Origin, Velocity, Radius, Load});
    ShowSpill(Origin, Load.Volume(), Kind, Direction, -BIG_NUMBER, 0.88);
}

void AMudTerrain::LandPour(FVector2D Contact, FPour& Pour)
{
    const double Limit = GetWorldSize();
    const auto InWorld = [Limit](FVector2D XY)
    {
        return FVector2D(FMath::Clamp(XY.X, 0.0, Limit), FMath::Clamp(XY.Y, 0.0, Limit));
    };
    Contact = InWorld(Contact);
    // The common active-cell solver handles runout after impact for every material.
    Deposit(Contact, Pour.Radius * 0.87, Pour.Mass);
}

void AMudTerrain::RegisterMaterialReceiver(AMudMachine& Machine)
{
    MaterialReceivers.RemoveAll([](const TWeakObjectPtr<AMudMachine>& Value) { return !Value.IsValid(); });
    MaterialReceivers.AddUnique(&Machine);
}

void AMudTerrain::AdvancePours(double Dt)
{
    // Swept collision is stable even when a long render frame would tunnel through a bed.
    if (Dt > 1.0 / 60.0 + 0.000001)
    {
        const int32 Steps = FMath::CeilToInt(Dt * 60);
        for (int32 Step = 0; Step < Steps; ++Step) AdvancePours(Dt / Steps);
        return;
    }
    const FVector Gravity(0, 0, -9.81);
    for (int32 Index = Pours.Num() - 1; Index >= 0; --Index)
    {
        auto& Portion = Pours[Index];
        const FVector Previous = Portion.Position;
        FVector Next = Previous + Portion.Velocity * Dt + Gravity * (0.5 * Dt * Dt);
        Portion.Velocity += Gravity * Dt;
        bool bBedImpact = false;
        for (const auto& Receiver : MaterialReceivers)
            if (Receiver.IsValid() && Receiver->InterceptMaterial(Previous, Next, Portion.Velocity, Portion.Mass))
            { bBedImpact = true; break; }
        if (Portion.Mass.Volume() <= Epsilon)
        {
            Pours.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            continue;
        }
        if (bBedImpact)
        {
            Portion.Position = Next;
            continue;
        }
        const FVector2D From(Previous.X, Previous.Y), To(Next.X, Next.Y);
        const double StartGap = Previous.Z - Height(From);
        const double EndGap = Next.Z - Height(To);
        if (StartGap <= 0 || EndGap <= 0)
        {
            const double Impact = StartGap <= 0 ? 0.0 : StartGap / (StartGap - EndGap);
            LandPour(FMath::Lerp(From, To, Impact), Portion);
            Pours.RemoveAtSwap(Index, 1, EAllowShrinking::No);
        }
        else
        {
            Portion.Position = Next;
        }
    }
}

void AMudTerrain::SettlePours()
{
    // Used by automated earthwork scenarios before comparing the final terrain volume.
    for (int32 Step = 0; Step < 240 && !Pours.IsEmpty(); ++Step)
        AdvancePours(1.0 / 60.0);
    for (auto& Portion : Pours)
        LandPour(FVector2D(Portion.Position.X, Portion.Position.Y), Portion);
    Pours.Reset();
}

void AMudTerrain::AdvanceSoil(double Dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MudSoilFlow);
    if (!SoilFlow.ActiveCells()) { SoilStepMilliseconds = 0; return; }
    const double Started = FPlatformTime::Seconds();
    SoilFlow.Step(Dt, 768,
        [this](int32 Cell) -> Earthwork::Column& { return Edit(Cell); },
        [this](int32 Cell) { return Wetness(FVector2D(Cell % Samples, Cell / Samples)); },
        [this](int32 Cell) { UpdateCell(Cell); });
    SoilStepMilliseconds = (FPlatformTime::Seconds() - Started) * 1000;
}

double AMudTerrain::MaterialBalance() const
{
    double Result = MassDelta;
    for (const auto& Pour : Pours) Result += Pour.Mass.Mass();
    return Result;
}

double AMudTerrain::MaterialVolumeBalance() const
{
    double Total = 0;
    for (const auto& Entry : Changed)
    {
        for (const auto& Layer : Entry.Value.Layers) Total += Layer.Volume;
        for (const auto& Layer : OriginalColumn(Entry.Key).Layers) Total -= Layer.Volume;
    }
    return Total;
}

void AMudTerrain::InitializeSoilLab()
{
    bRoadContract = false;
    Start = FVector(150, 800, 18);
    Goal = FVector(305, 800, 18);
    Piles.Empty();
    for (int32 Y = 720; Y <= 900; ++Y)
        for (int32 X = 128; X <= 320; ++X)
        {
            const double Sand = FMath::Max(0.0, 3.5 - FVector2D::Distance(FVector2D(X,Y), FVector2D(210,800)) * 0.55);
            const double Gravel = FMath::Max(0.0, 3.0 - FVector2D::Distance(FVector2D(X,Y), FVector2D(285,800)) * 0.65);
            const double Slope = X > 175 && X < 230 && Y > 855 ? (Y - 855) * 0.25 : 0;
            Original[Index(X,Y)] = 18 + Sand + Gravel + Slope;
        }
    Heights = Original;
    Pits.Add({FVector2D(250,800), FVector2D(18,28)});
    Piles.Add({FVector2D(210,800), 6.36, 3.5, Material::Sand, 150});
    Piles.Add({FVector2D(285,800), 4.62, 3.0, Material::Gravel, 70});
    for (int32 I = 0; I < Trees.Num(); ++I)
        if (Trees[I].XY.X >= 125 && Trees[I].XY.X <= 323 && Trees[I].XY.Y >= 717 && Trees[I].XY.Y <= 903)
            HideTree(I);
}

void AMudTerrain::Spread(FVector2D XY, FVector2D Across, FVector2D Reach, const Earthwork::Load& Load)
{
    if (Load.Volume() <= 0)
        return;
    auto Area = ToolFootprint(XY, Across, Reach, true);
    if (Area.IsEmpty())
    {
        const int32 X = FMath::Clamp(FMath::RoundToInt(XY.X), 0, Samples - 1);
        const int32 Y = FMath::Clamp(FMath::RoundToInt(XY.Y), 0, Samples - 1);
        Area.Emplace(Index(X, Y), 1.0);
    }
    ApplyFill(Area, Load);
}

void AMudTerrain::ApplyFill(const TArray<TPair<int32, double>>& Area, const Earthwork::Load& Load)
{
    double Total = 0;
    for (const auto& Sample : Area)
        Total += Sample.Value;
    for (const auto& Sample : Area)
    {
        Earthwork::Load Share;
        for (size_t M = 0; M < MaterialCount; ++M)
            Share.Volumes[M] = Load.Volumes[M] * Sample.Value / Total;
        Edit(Sample.Key).Fill(Share);
        if (Share.Volume() > 0.075)
            TrackWear.Remove(Sample.Key);
        UpdateCell(Sample.Key, Share.Volume());
    }
}

double AMudTerrain::CompactBody(FVector2D Center,FVector2D Across,FVector2D Reach,double Plane,FVector2D Slopes,
    double PressureKPa,double Dt)
{
    if(Dt<=0||PressureKPa<=0)return 0;
    const auto Area=ToolFootprint(Center,Across,Reach,true);
    const FVector2D Forward(Across.Y,-Across.X);
    double Supported=0,ContactWeight=0;
    TArray<TPair<int32,double>> Contacts;
    for(const auto& Sample:Area)
    {
        const int Cell=Sample.Key;const FVector2D XY(Cell%Samples,Cell/Samples),D=XY-Center;
        const double Bottom=Plane+FVector2D::DotProduct(D,Across)*Slopes.X+FVector2D::DotProduct(D,Forward)*Slopes.Y;
        const double Depth=Heights[Cell]-Bottom;
        if(Depth<=.001||IsAggregate(Surface(XY)))continue;
        Supported+=Sample.Value*FMath::Clamp(Depth/.15,0.,.65);
        ContactWeight+=Sample.Value;Contacts.Add(Sample);
    }
    const double LoadShare=ContactWeight>Epsilon?Supported/ContactWeight:0;
    const double SupportedKN=PressureKPa*(4*Reach.X*Reach.Y)*LoadShare;
    for(const auto& Contact:Contacts)
    {
        const int Cell=Contact.Key;const FVector2D XY(Cell%Samples,Cell/Samples);
        // Packing reduces occupied height, never material mass. The existing column
        // packing limit prevents a chassis from tunnelling through a large soil bank.
        // Concentrate the supported load on the actual contact patch, rather than
        // diluting it over the empty underside when a narrow crest touches one edge.
        const double Packed=Edit(Cell).Compact(FMath::Clamp(SupportedKN/80.*Contact.Value/ContactWeight,0.,2.)*Dt*4,Wetness(XY));
        if(Packed>Epsilon)UpdateCell(Cell);
    }
    return LoadShare;
}

void AMudTerrain::PressTrack(FVector2D Center, FVector2D Forward, double HalfLength, double HalfWidth,
    double Outward, double Scrub, bool bTracked, double LoadFactor)
{
    Forward = Forward.GetSafeNormal();
    if (Forward.IsNearlyZero() || HalfLength <= 0 || HalfWidth <= 0 || LoadFactor <= 0)
        return;
    const FVector2D Right(-Forward.Y, Forward.X);
    const double EdgeWidth = HalfWidth + Scrub * 0.22;
    const double Radius = FMath::Sqrt(FMath::Square(HalfLength) + FMath::Square(EdgeWidth)) + (bTracked ? 0.0 : 0.71);
    Load Displaced;
    for (const int32 Cell : Cells(Center, Radius))
    {
        const FVector2D Point(Cell % Samples, Cell / Samples);
        double Contact = 0;
        // Integrate both track and tyre patches over the physical cell. Narrow
        // track shoes must not disappear between metre-spaced sample centres.
        {
            // Integrate the tyre patch within a terrain cell so narrow tyres leave continuous marks.
            for (int32 Y = 0; Y < 4; ++Y)
                for (int32 X = 0; X < 4; ++X)
                {
                    const FVector2D Offset = Point + FVector2D((X + 0.5) * 0.25 - 0.5, (Y + 0.5) * 0.25 - 0.5) - Center;
                    const double Along = FVector2D::DotProduct(Offset, Forward) / HalfLength;
                    const double Across = FMath::Abs(FVector2D::DotProduct(Offset, Right)) / EdgeWidth;
                    Contact += FMath::Max(0.0, 1.0 - Along * Along) *
                        (bTracked?FMath::Max(0.,1-Across*Across):1.0-FMath::SmoothStep(0.35,1.0,Across))/16.0;
                }
        }
        if (Contact <= Epsilon)
            continue;
        const Material Kind = Surface(Point);
        double Softness = 1.0 - Properties(Kind).Bearing;
        if (!bTracked && IsAggregate(Kind) && Changed.Contains(Cell))
        {
            const double Firmness = FMath::SmoothStep(0.0, 0.16, AggregateCover(Cell));
            Softness = FMath::Lerp(1.0, Softness, Firmness);
        }
        const int32 OldWear = TrackWear.FindRef(Cell);
        const double Compressed = Edit(Cell).Compact(Contact * LoadFactor * (1.0 - Scrub) * 0.10, Wetness(Point));
        if (Compressed > Epsilon) UpdateCell(Cell);
        if (!Changed[Cell].Layers.empty()) Softness *= 1.0 - Changed[Cell].Layers.back().Packing * 0.65;
        const auto Pressure = RunningGear::PressGround(OldWear, Contact, Softness, Wetness(Point), Scrub, bTracked, LoadFactor);
        if (Pressure.Wear == OldWear)
            continue;
        TrackWear.Add(Cell, static_cast<uint8>(Pressure.Wear));
        if (Pressure.Depth > Epsilon)
        {
            const Load Cut = Edit(Cell).CutToHeight(Heights[Cell] - Pressure.Depth, BIG_NUMBER);
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
    const double WorldEdge = GetWorldSize();
    const auto BankAt = [&](double Lateral)
    {
        const FVector2D Edge = Center + Right * Outward * Lateral;
        return FVector2D(FMath::Clamp(Edge.X, 0.0, WorldEdge), FMath::Clamp(Edge.Y, 0.0, WorldEdge));
    };
    const FVector2D Bank = BankAt(EdgeWidth + 0.92);
    if (!bTracked)
        Deposit(BankAt(-EdgeWidth - 0.92), 0.86, Displaced.Take(Displaced.Volume() * (0.5 - Scrub * 0.23)));
    Deposit(Bank, bTracked ? 1.22 : 0.95, Displaced);
    if (Scrub > 0.22)
        ShowSpill(FVector(Bank.X, Bank.Y, Height(Bank)), Displaced.Volume(), Displaced.DominantMaterial(),
            FVector(Right.X, Right.Y, 0) * Outward);
}

void AMudTerrain::ShowSpill(FVector Origin, double Volume, Material Type, FVector Direction, double FloorZ, double ThrowScale)
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
        Clump.Velocity *= ThrowScale;
        Clump.Life = 1.65;
        Clump.Radius = Type == Material::Gravel ? SpillRandom.FRandRange(0.018, 0.050) :
            Type == Material::Rock ? SpillRandom.FRandRange(0.06, 0.14) : SpillRandom.FRandRange(0.018, 0.055);
        Clump.Spin = SpillRandom.FRandRange(-180, 180);
        Clump.FloorZ = FloorZ;
        Clump.bBounced = false;
        Clump.bRolling = false;
    }
}

void AMudTerrain::UpdateSpills(double Dt)
{
    UpdatePourSurfaces();
    for (int32 Kind = 0; Kind < Spoil.Num(); ++Kind)
    {
        auto& Batch = Spills[Kind];
        const bool bSoil = !IsAggregate(static_cast<Material>(Kind));
        bool bChanged = false;
        for (int32 I = 0; I < Batch.Pieces.Num(); ++I)
        {
            FSpill& Clump = Batch.Pieces[I];
            if (Clump.Life <= 0)
                continue;
            bChanged = true;
            Clump.Life -= Dt;
            Clump.Spin += (Clump.bRolling ? Clump.Velocity.Size2D() * 140 : 220 + I * 9) * Dt;
            if (Clump.bRolling)
                Clump.Velocity *= FMath::Exp(-Dt * (bSoil ? 15.0 : 5.9));
            else
                Clump.Velocity.Z -= 9.81 * Dt;
            const FVector Previous = Clump.Position;
            Clump.Position += Clump.Velocity * Dt;
            for (const auto& Receiver : MaterialReceivers)
            {
                Load NoMass;
                if (Receiver.IsValid() && Receiver->InterceptMaterial(Previous, Clump.Position, Clump.Velocity, NoMass, false))
                {
                    Clump.Life = FMath::Min(Clump.Life, 0.16);
                    break;
                }
            }
            const double Ground = FMath::Max(Height(FVector2D(Clump.Position.X, Clump.Position.Y)), Clump.FloorZ);
            if (Clump.bRolling || Clump.Position.Z <= Ground + Clump.Radius * 0.30)
            {
                Clump.Position.Z = Ground + Clump.Radius * 0.30;
                if (!bSoil && !Clump.bBounced && Clump.Velocity.Z < -1.1)
                {
                    Clump.Velocity *= FVector(0.68, 0.68, -0.28);
                    Clump.bBounced = true;
                }
                else if (!Clump.bRolling)
                {
                    Clump.bRolling = true;
                    Clump.Velocity *= FVector(bSoil ? 0.36 : 0.70, bSoil ? 0.36 : 0.70, 0);
                    Clump.Life = FMath::Min(Clump.Life, bSoil ? 0.20 : 0.48);
                }
            }
            const double Fade = Clump.bRolling ? FMath::Clamp(Clump.Life / 0.12, 0.0, 1.0) : 1.0;
            Batch.Transforms[I] = Clump.Life > 0
                ? FTransform(FRotator(Clump.Spin, I * 61, Clump.Spin * 0.43), Clump.Position * 100,
                    FVector(1.35 + 0.08 * (I % 4), 1.06, 0.86) * (Fade * Clump.Radius))
                : FTransform(FRotator::ZeroRotator, FVector(0, 0, -100000), FVector(0.001));
        }
        if (bChanged)
            Spoil[Kind]->BatchUpdateInstancesTransforms(0, Batch.Transforms, false, true, true);
    }
}

double AMudTerrain::Grade(FVector2D XY, FVector2D Across, FVector2D Reach, double Rate, double ToolHeight, FVector2D Slopes,
    const TArray<FVector>* CuttingEdge)
{
    auto Area = ToolFootprint(XY, Across, Reach, true);
    if(CuttingEdge&&CuttingEdge->Num()>1)
    {
        TMap<int32,double> Weights;
        const FVector2D Forward(Across.Y,-Across.X);
        for(int I=1;I<CuttingEdge->Num();++I)
        {
            const FVector2D A((*CuttingEdge)[I-1].X,(*CuttingEdge)[I-1].Y),B((*CuttingEdge)[I].X,(*CuttingEdge)[I].Y);
            const auto Strip=ToolFootprint((A+B)*.5-Forward*.4,Across,{FVector2D::Distance(A,B)*.5+.025,Reach.Y},true);
            for(const auto& Cell:Strip)Weights.FindOrAdd(Cell.Key)=FMath::Min(1.,Weights.FindRef(Cell.Key)+Cell.Value);
        }
        Area.Reset();for(const auto& Cell:Weights)Area.Add({Cell.Key,Cell.Value});
        Area.Sort([](const auto& A,const auto& B){return A.Key<B.Key;});
    }
    if (Area.Num() < 2)
        return 0;
    const FVector2D Along(Across.Y, -Across.X);
    const FVector2D Gradient = Across * Slopes.X + Along * Slopes.Y;
    const auto RelativeHeight = [&](int32 Cell)
    {
        const FVector2D Offset = FVector2D(Cell % Samples, Cell / Samples) - XY;
        return Heights[Cell] - FVector2D::DotProduct(Offset, Gradient);
    };
    double Average = 0;
    double TotalWeight = 0;
    for (const auto& Sample : Area)
    {
        Average += RelativeHeight(Sample.Key) * Sample.Value;
        TotalWeight += Sample.Value;
    }
    Average /= TotalWeight;
    const double Fraction = FMath::Clamp(Rate, 0.0, 1.0);
    double Deficit = 0;
    TArray<TPair<int32, double>> HighCells, LowCells;
    for (const auto& Sample : Area)
    {
        const int32 Cell = Sample.Key;
        const double Height = RelativeHeight(Cell);
        const double Difference = (Height - Average) * Sample.Value;
        if (Difference > Epsilon)
        {
            const double Reachable = (Height - ToolHeight) * Sample.Value;
            if (Reachable > Epsilon)
                HighCells.Emplace(Cell, FMath::Min(Difference, Reachable));
        }
        else if (Difference < -Epsilon)
        {
            Deficit -= Difference;
            LowCells.Emplace(Cell, -Difference);
        }
    }
    if (Deficit <= Epsilon)
        return 0;
    Load Moved;
    for (const auto& Entry : HighCells)
    {
        const FVector2D Point(Entry.Key%Samples,Entry.Key/Samples);
        const double Plane=ToolHeight+FVector2D::DotProduct(Point-XY,Gradient);
        const Load Cut = Edit(Entry.Key).CutToHeight(FMath::Max(Plane,Heights[Entry.Key]-Entry.Value*Fraction),BIG_NUMBER);
        Moved.Receive(Cut);
        if (Cut.Volume() > 0)
            UpdateCell(Entry.Key, -Cut.Volume());
    }
    if (Moved.Volume() <= 0)
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

AMudTerrain::FMudRoadSurvey AMudTerrain::SurveyFordRoad(FVector2D Ford) const
{
    // Check a three-metre carriageway. The center can shift a few metres within the marked worksite.
    const int32 CenterY = FMath::RoundToInt(Ford.Y);
    FMudRoadSurvey Best;
    for (const int32 Lane : {0, -1, 1, -2, 2, -3, 3})
    {
        const int32 Y = CenterY + Lane;
        if (Y < 1 || Y >= Samples - 1)
            continue;
        const auto Channel = RiverAt(Y);
        const int32 CenterX = FMath::RoundToInt(Channel.XY.X);
        const int32 Reach = FMath::CeilToInt(Channel.Width * 0.5 + 12);
        if (CenterX - Reach < 1 || CenterX + Reach >= Samples - 1)
            continue;
        FMudRoadSurvey Survey;
        Survey.Start = FVector2D(CenterX - Reach, Y);
        Survey.Sections.Reserve(2 * Reach + 1);
        const double Levels[] = {RiverAt(Y - 1).Z, Channel.Z, RiverAt(Y + 1).Z};
        for (int32 X = CenterX - Reach; X <= CenterX + Reach; ++X)
        {
            auto& Section = Survey.Sections.AddDefaulted_GetRef();
            for (int32 WheelRow = Y - 1; WheelRow <= Y + 1; ++WheelRow)
            {
                const int32 Cell = Index(X, WheelRow);
                const FVector2D Point(X, WheelRow);
                const bool bNeedsCover = Original[Cell] < Levels[WheelRow - Y + 1] + 0.12 || Wetness(Point) > 0.87;
                const double DepthDeficit = FMath::Max(0.0, WaterDepth(Point) - 0.44);
                const double CoverDeficit = bNeedsCover ? FMath::Max(0.0, 0.12 - AggregateCover(Cell)) : 0.0;
                Section.bDeep |= DepthDeficit > 0;
                Section.bSoft |= CoverDeficit > 0;
                Section.bRough |= FMath::Abs(Heights[Cell] - Heights[Index(X - 1, WheelRow)]) > 0.34 ||
                    FMath::Abs(Heights[Cell] - Heights[Index(X + 1, WheelRow)]) > 0.34;
                Survey.Fill += FMath::Max(DepthDeficit, CoverDeficit);
            }
            Section.bRough |= FMath::Abs(Heights[Index(X, Y - 1)] - Heights[Index(X, Y + 1)]) > 0.47;
            Survey.Ready += Section.IsReady() ? 1 : 0;
            Survey.Deep += Section.bDeep ? 1 : 0;
            Survey.Soft += Section.bSoft ? 1 : 0;
            Survey.Rough += Section.bRough ? 1 : 0;
        }
        if (Survey.IsReady())
            return Survey;
        if (Best.Sections.IsEmpty() || Survey.Ready * Best.Sections.Num() > Best.Ready * Survey.Sections.Num())
            Best = MoveTemp(Survey);
    }
    return Best;
}

bool AMudTerrain::IsRoadReady() const
{
    return !bRoadContract || FordSurveys.ContainsByPredicate([](const FMudRoadSurvey& Survey) { return Survey.IsReady(); });
}

const AMudTerrain::FMudRoadSurvey* AMudTerrain::GetRoadSurvey(int32 FordIndex) const
{
    return bRoadContract && FordSurveys.IsValidIndex(FordIndex) ? &FordSurveys[FordIndex] : nullptr;
}

void AMudTerrain::BuildTile(int32 Id, int32 Step)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MudRebuildTerrainTile);
    const int32 X0 = (Id % TilesPerSide)*TileSize, Y0 = (Id / TilesPerSide)*TileSize;
    const double Spacing = Step == 0 ? 0.5 : Step;
    const int32 Cells = FMath::RoundToInt(TileSize/Spacing), Edge = Cells+1;
    TArray<FVector> Vertices, Normals;
    TArray<int32> Triangles;
    TArray<FVector2D> UV, FinishMasks, MineralMasks, RockMasks;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    const int32 Reserve = Edge*Edge + Cells*4*20;
    Vertices.Reserve(Reserve); Normals.Reserve(Reserve); UV.Reserve(Reserve);
    FinishMasks.Reserve(Reserve); MineralMasks.Reserve(Reserve); RockMasks.Reserve(Reserve);
    Colors.Reserve(Reserve); Tangents.Reserve(Reserve); Triangles.Reserve(Cells*Cells*6+Cells*96);
    struct FCoating { FLinearColor Color; FVector2D Finish, Minerals, Rock; };
    TMap<int32,FCoating> Coatings;
    const auto Read = [&](int32 X, int32 Y) -> FCoating
    {
        X = FMath::Clamp(X,0,Samples-1); Y = FMath::Clamp(Y,0,Samples-1);
        const int32 Cell = Index(X,Y);
        if (const auto* Cached = Coatings.Find(Cell)) return *Cached;
        const FVector2D XY(X,Y);
        const Material Kind = Surface(XY);
        const double Z = Heights[Cell], Finish = WorkFinish.FindRef(Cell), Wet = Wetness(XY);
        const FVector Up = Normal(XY);
        bool Grass = Kind == Material::Soil && FMath::Abs(Original[Cell]-Z)<0.035 && FMath::Abs(Finish)<0.16;
        for (const auto& Pile : Piles) Grass &= FVector2D::DistSquared(XY,Pile.Center)>=FMath::Square(Pile.Radius);
        auto Color = GroundMasks(Kind,FMath::Max(Wet,FMath::Clamp((Original[Cell]-Z)*0.70,0.0,0.38)),Up.Z,XY,Grass);
        const float Rut = FMath::SmoothStep(0.03f,0.54f,TrackWear.FindRef(Cell)/255.0f);
        Color.R *= 1-0.94f*Rut; Color.B *= 1-0.15f*Rut;
        Color.A = FMath::Max(Color.A,static_cast<float>(Wet*0.97*Rut));
        FCoating Value{Color, {FMath::Max(0.0,-Finish), FMath::Max(0.0,Finish)},
            {Kind==Material::Sand ? 1.0 : 0.0, Kind==Material::Clay ? 1.0 : 0.0},
            {Kind==Material::Rock ? 1.0 : 0.0,0}};
        Coatings.Add(Cell,Value); return Value;
    };
    const auto Add = [&](double X, double Y, int32 Destination=INDEX_NONE)
    {
        const FVector2D XY(X0+X,Y0+Y);
        const int32 GX = FMath::FloorToInt(XY.X), GY = FMath::FloorToInt(XY.Y);
        const double U = XY.X-GX, V = XY.Y-GY;
        const FCoating A=Read(GX,GY), B=Read(GX+1,GY), C=Read(GX,GY+1), D=Read(GX+1,GY+1);
        const auto Blend2 = [U,V](FVector2D A,FVector2D B,FVector2D C,FVector2D D)
        { return FMath::Lerp(FMath::Lerp(A,B,U),FMath::Lerp(C,D,U),V); };
        const FVector Up = Normal(XY);
        const int32 Index=Destination==INDEX_NONE ? Vertices.Num() : Destination;
        if (Destination==INDEX_NONE)
        {
            Vertices.AddUninitialized(); Normals.AddUninitialized(); UV.AddUninitialized();
            Colors.AddUninitialized(); FinishMasks.AddUninitialized(); MineralMasks.AddUninitialized();
            RockMasks.AddUninitialized(); Tangents.AddDefaulted();
        }
        Vertices[Index]=FVector(X*100,Y*100,Height(XY)*100);
        Normals[Index]=Up; UV[Index]=XY;
        Colors[Index]=FMath::Lerp(FMath::Lerp(A.Color,B.Color,U),FMath::Lerp(C.Color,D.Color,U),V);
        FinishMasks[Index]=Blend2(A.Finish,B.Finish,C.Finish,D.Finish);
        MineralMasks[Index]=Blend2(A.Minerals,B.Minerals,C.Minerals,D.Minerals);
        RockMasks[Index]=Blend2(A.Rock,B.Rock,C.Rock,D.Rock);
        Tangents[Index]=FProcMeshTangent(FVector::CrossProduct(FVector::RightVector,Up).GetSafeNormal(),false);
        return Index;
    };
    // Fixed near topology permits a local stencil update. Copy untouched vertex attributes
    // from the existing section; only the changed cells and normal halo are reconstructed.
    auto* Existing=TerrainMeshes[Id].Get();
    const FIntRect* Region=DirtyRegions.Find(Id);
    const auto* Section=Existing ? Existing->GetProcMeshSection(0) : nullptr;
    if (Step==0 && TileSteps[Id]==0 && Region && Section && Section->ProcVertexBuffer.Num()==Edge*Edge)
    {
        const int32 Count=Section->ProcVertexBuffer.Num();
        Vertices.SetNumUninitialized(Count); Normals.SetNumUninitialized(Count); UV.SetNumUninitialized(Count);
        FinishMasks.SetNumUninitialized(Count); MineralMasks.SetNumUninitialized(Count); RockMasks.SetNumUninitialized(Count);
        Colors.SetNumUninitialized(Count); Tangents.SetNumUninitialized(Count);
        for (int32 I=0; I<Count; ++I)
        {
            const auto& V=Section->ProcVertexBuffer[I];
            Vertices[I]=V.Position; Normals[I]=V.Normal; UV[I]=V.UV0;
            FinishMasks[I]=V.UV1; MineralMasks[I]=V.UV2; RockMasks[I]=V.UV3;
            Colors[I]=V.Color.ReinterpretAsLinear(); Tangents[I]=V.Tangent;
        }
        for (int32 Y=Region->Min.Y*2; Y<=FMath::Min(Edge-1,Region->Max.Y*2); ++Y)
            for (int32 X=Region->Min.X*2; X<=FMath::Min(Edge-1,Region->Max.X*2); ++X)
                Add(X*Spacing,Y*Spacing,Y*Edge+X);
        Existing->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,FinishMasks,MineralMasks,RockMasks,Colors,Tangents,false);
        DirtyRegions.Remove(Id); return;
    }
    for (int32 Y=0; Y<Edge; ++Y) for (int32 X=0; X<Edge; ++X) Add(X*Spacing,Y*Spacing);
    for (int32 Y=0; Y<Cells; ++Y)
        for (int32 X=0; X<Cells; ++X)
        {
            const int32 SW=Y*Edge+X, SE=SW+1, NW=SW+Edge, NE=NW+1;
            if (Step==0 || (X>0 && Y>0 && X<Cells-1 && Y<Cells-1))
            { Triangles.Append({SW,NW,SE,SE,NW,NE}); continue; }
            // Every LOD shares the exact same half-metre boundary. No skirts or T-junctions.
            TArray<int32,TInlineAllocator<40>> Ring;
            Ring.Add(SW);
            if (X==0) for (double T=0.5; T<Spacing; T+=0.5) Ring.Add(Add(X*Spacing,Y*Spacing+T));
            Ring.Add(NW);
            if (Y==Cells-1) for (double T=0.5; T<Spacing; T+=0.5) Ring.Add(Add(X*Spacing+T,(Y+1)*Spacing));
            Ring.Add(NE);
            if (X==Cells-1) for (double T=Spacing-0.5; T>0; T-=0.5) Ring.Add(Add((X+1)*Spacing,Y*Spacing+T));
            Ring.Add(SE);
            if (Y==0) for (double T=Spacing-0.5; T>0; T-=0.5) Ring.Add(Add(X*Spacing+T,Y*Spacing));
            const int32 Center=Add((X+0.5)*Spacing,(Y+0.5)*Spacing);
            for (int32 I=0; I<Ring.Num(); ++I) Triangles.Append({Center,Ring[I],Ring[(I+1)%Ring.Num()]});
        }
    auto* Mesh = TerrainMeshes[Id].Get();
    if (!Mesh)
    {
        Mesh=NewObject<UProceduralMeshComponent>(this); TerrainMeshes[Id]=Mesh;
        Mesh->SetupAttachment(RootComponent); Mesh->SetRelativeLocation(FVector(X0*100,Y0*100,0));
        Mesh->bUseAsyncCooking=true; Mesh->bUseComplexAsSimpleCollision=true;
        Mesh->SetMaterial(0,GroundMaterial); Mesh->RegisterComponent();
    }
    if (TileSteps[Id]==Step)
        Mesh->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,FinishMasks,MineralMasks,RockMasks,Colors,Tangents,false);
    else
    {
        Mesh->SetCollisionEnabled(Step==0 ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        Mesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UV,FinishMasks,MineralMasks,RockMasks,
            Colors,Tangents,Step==0,false);
    }
    TileSteps[Id]=Step;
    DirtyRegions.Remove(Id);
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
    constexpr int32 Spacing = 3;
    for (int32 Y = Spacing; Y < Samples - Spacing; Y += Spacing)
    {
        for (int32 X = Spacing; X < Samples - Spacing; X += Spacing)
        {
            const FVector2D Point(X + Scatter.FRandRange(-1.3, 1.3), Y + Scatter.FRandRange(-1.3, 1.3));
            if (FVector2D::DistSquared(Point, StartXY) < FMath::Square(12.0) ||
                FVector2D::DistSquared(Point, GoalXY) < FMath::Square(11.0) ||
                Surface(Point) != Earthwork::Material::Soil || WaterDepth(Point) > 0.015)
                continue;
            const FVector Up = Normal(Point);
            const double Wet = Wetness(Point);
            const double Cover = GroundMasks(Earthwork::Material::Soil, Wet, Up.Z, Point, true).R;
            const double Clusters = FMath::SmoothStep(-0.22f, 0.25f, FMath::PerlinNoise2D(Point * 0.29));
            const double Bank = FMath::SmoothStep(0.23, 0.48, Wet) * (1.0 - FMath::SmoothStep(0.58, 0.74, Wet));
            const double Density = Cover * FMath::Lerp(0.15, 0.84, Clusters) + Bank * Up.Z * 0.18;
            if (Scatter.FRand() > Density)
                continue;
            const int32 Cell = Index(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y));
            if (PlantByCell.Contains(Cell))
                continue;
            const double Width = Scatter.FRandRange(0.76, 1.54);
            const double HeightScale = Scatter.FRandRange(0.50, 1.24) + Bank * 0.47;
            const FQuat Lean = FQuat::FindBetweenNormals(FVector::UpVector, (Up + FVector::UpVector).GetSafeNormal());
            const FQuat Facing(FVector::UpVector, Scatter.FRandRange(0, UE_TWO_PI));
            const FTransform Transform(Lean * Facing, FVector(Point.X, Point.Y, Height(Point) - 0.025) * 100.0,
                FVector(Width, Width, HeightScale));
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

void AMudTerrain::UpdateGroundRubble(FVector2D Focus)
{
    if (!GroundRubble[0]->GetStaticMesh())
        return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextRubbleUpdate || (!bRubbleDirty && FVector2D::DistSquared(RubbleFocus, Focus) < FMath::Square(6.0)))
        return;

    RubbleFocus = Focus;
    NextRubbleUpdate = Now + 0.2;
    bRubbleDirty = false;

    TArray<const FMudPile*, TInlineAllocator<5>> NearbyPiles;
    for (const FMudPile& Pile : Piles)
        if (FVector2D::DistSquared(Focus, Pile.Center) <= FMath::Square(RubbleRadius + Pile.Radius))
            NearbyPiles.Add(&Pile);
    TArray<TPair<double, int32>> Nearby;
    if (!WorkFinish.IsEmpty() || !NearbyPiles.IsEmpty())
    {
        const int32 MinX = FMath::Max(1, FMath::FloorToInt(Focus.X) - RubbleRadius);
        const int32 MaxX = FMath::Min(Samples - 2, FMath::CeilToInt(Focus.X) + RubbleRadius);
        const int32 MinY = FMath::Max(1, FMath::FloorToInt(Focus.Y) - RubbleRadius);
        const int32 MaxY = FMath::Min(Samples - 2, FMath::CeilToInt(Focus.Y) + RubbleRadius);
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                const double Distance = FVector2D::DistSquared(Focus, FVector2D(X, Y));
                const int32 Cell = Index(X, Y);
                if (Distance > FMath::Square(RubbleRadius))
                    continue;
                const bool bDepotSurface = NearbyPiles.ContainsByPredicate([&](const FMudPile* Pile)
                {
                    return FVector2D::DistSquared(FVector2D(X, Y), Pile->Center) < FMath::Square(Pile->Radius);
                });
                if (bDepotSurface || FMath::Abs(WorkFinish.FindRef(Cell)) >= 0.30)
                    Nearby.Emplace(Distance, Cell);
            }
        }
        Nearby.Sort([](const auto& A, const auto& B) { return A.Key == B.Key ? A.Value < B.Value : A.Key < B.Key; });
    }

    TArray<FTransform> Transforms[MaterialCount];
    constexpr int32 MaxPiecesPerMaterial = 120;
    for (const auto& Entry : Nearby)
    {
        const int32 Cell = Entry.Value;
        const float Finish = WorkFinish.FindRef(Cell);
        const FVector2D Center(Cell % Samples, Cell / Samples);
        const Material Kind = Surface(Center);
        if (Kind == Material::Sand || Kind == Material::Clay) continue;
        auto& Pieces = Transforms[static_cast<uint8>(Kind)];
        if (Pieces.Num() >= MaxPiecesPerMaterial || TrackWear.FindRef(Cell) >= 105)
            continue;

        FRandomStream Scatter(static_cast<int32>(HashCombineFast(GetTypeHash(Seed), GetTypeHash(Cell))));
        const double Density = Kind == Material::Soil ? (Finish > 0 ? 0.18 : 0.08) : 0.50;
        const double Work = FMath::Abs(Finish);
        if (Scatter.FRand() > Density * (Work < 0.30 ? 0.49 : Work))
            continue;
        const FVector2D Point = Center + FVector2D(Scatter.FRandRange(-0.38, 0.38), Scatter.FRandRange(-0.38, 0.38));
        if (WaterDepth(Point) > 0.015)
            continue;

        FVector Up;
        const double Z = MeshHeight(Point, &Up);
        if (Up.Z < 0.68)
            continue;

        const double Scale = Kind == Material::Gravel ? Scatter.FRandRange(0.035, 0.085) :
            Scatter.FRandRange(0.045, Kind == Material::Rock ? 0.24 : 0.095);
        const FQuat Rotation = FQuat(Up, Scatter.FRandRange(0.0, 2.0 * PI)) *
            FQuat::FindBetweenNormals(FVector::UpVector, Up);
        const FVector Size = FVector(Scatter.FRandRange(0.8, 1.4), 1.0, Kind == Material::Soil ? 0.58 : 0.82) * Scale;
        Pieces.Emplace(Rotation, FVector(Point.X, Point.Y, Z) * 100.0 - Up * (3.0 * Scale), Size);
    }
    for (int32 Kind = 0; Kind < GroundRubble.Num(); ++Kind)
    {
        auto* Mesh = GroundRubble[Kind].Get();
        const int32 ExistingCount = Mesh->GetInstanceCount();
        const auto& Pieces = Transforms[Kind];
        if (ExistingCount == Pieces.Num())
        {
            if (!Pieces.IsEmpty())
                Mesh->BatchUpdateInstancesTransforms(0, Pieces, false, true, true);
            continue;
        }
        if (ExistingCount > 0)
            Mesh->ClearInstances();
        if (!Pieces.IsEmpty())
            Mesh->AddInstances(Pieces, false, false, false);
    }
}

void AMudTerrain::UpdateTiles(FVector2D Focus)
{
    if (bRoadContract && GetWorld()->GetTimeSeconds() >= NextFordSurvey)
    {
        NextFordSurvey = GetWorld()->GetTimeSeconds() + 0.45;
        for (int32 I = 0; I < Fords.Num(); ++I)
            FordSurveys[I] = SurveyFordRoad(Fords[I]);
    }
    UpdateGroundCover();
    TArray<TPair<double, int32>> Candidates;
    for (int32 Id = 0; Id < TerrainMeshes.Num(); ++Id)
    {
        const FVector2D Center((Id % TilesPerSide + 0.5) * TileSize, (Id / TilesPerSide + 0.5) * TileSize);
        const double Distance = FVector2D::DistSquared(Center, Focus);
        const double Near = TileSteps[Id]==0 ? 110.0 : 95.0;
        const int32 Target = Distance < FMath::Square(Near) ? 0 :
            (Distance < FMath::Square(TileSteps[Id]<=1 ? 180.0 : 160.0) ? 1 :
            (Distance < FMath::Square(TileSteps[Id]<=2 ? 300.0 : 275.0) ? 2 : 4));
        if (Dirty.Contains(Id) || TileSteps[Id] != Target)
            Candidates.Emplace(Distance, Id);
    }
    Candidates.Sort([](const auto& A, const auto& B) { return A.Key < B.Key; });
    for (int32 I = 0; I < FMath::Min(Candidates.Num(), 2); ++I)
    {
        const auto Entry = Candidates[I];
        const int32 Old = TileSteps[Entry.Value];
        BuildTile(Entry.Value, Entry.Key < FMath::Square(Old==0 ? 110.0 : 95.0) ? 0 :
            (Entry.Key < FMath::Square(Old<=1 ? 180.0 : 160.0) ? 1 :
            (Entry.Key < FMath::Square(Old<=2 ? 300.0 : 275.0) ? 2 : 4)));
        Dirty.Remove(Entry.Value);
    }
    UpdateGroundRubble(Focus);
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
    const auto AddWater = [&](const TArray<FRiverSample>& Channel, FVector2D Across, double Margin, bool bCreek)
    {
        for (int32 I = 0; I < Channel.Num(); ++I)
        {
            const FRiverSample& Sample = Channel[I];
            const int32 J = Points.Num();
            for (double Side : {-1.0, 1.0})
            {
                const FVector2D XY = Sample.XY + Across * (Side * (Sample.Width * 0.5 + Margin));
                Points.Emplace(XY.X * 100, XY.Y * 100, Sample.Z * 100);
                Normals.Add(FVector::UpVector);
                UV.Add(bCreek ? FVector2D(XY.Y, XY.X) : XY);
            }
            if (I > 0)
                Triangles.Append({ J - 2, J, J - 1, J - 1, J, J + 1 });
        }
    };
    AddWater(River, FVector2D(1, 0), 14.0, false);
    AddWater(Creek, FVector2D(0, -1), CreekMargin, true);
    Mesh->CreateMeshSection_LinearColor(0, Points, Triangles, Normals, UV, {}, {}, false, false);
    Mesh->RegisterComponent();
}

void AMudTerrain::BuildMarkers()
{
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto Add = [&](UMaterialInterface* Paint, FVector Position, FVector Size)
    {
        auto* Part = NewObject<UStaticMeshComponent>(this);
        Part->SetupAttachment(RootComponent);
        Part->SetStaticMesh(Cube);
        Part->SetMaterial(0, Paint);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetRelativeLocation(Position);
        Part->SetRelativeScale3D(Size);
        Part->RegisterComponent();
    };
    for (bool bStart : {true, false})
    {
        const FVector Position = (bStart ? Start : Goal) * 100;
        auto* Paint = GeneratedMaterial(bStart ? TEXT("GoldMarker") : TEXT("RedMarker"));
        for (double Side : {-1.0, 1.0})
        {
            if (bStart)
            {
                Add(Paint, Position + FVector(0, Side * 410, 55), FVector(0.22, 0.22, 1.1));
                Add(Paint, Position + FVector(0, Side * 410, 115), FVector(0.12, 0.44, 0.16));
            }
            else
                Add(Paint, Position + FVector(0, Side * 410, 245), FVector(0.24, 0.24, 4.9));
        }
        if (!bStart)
            Add(Paint, Position + FVector(0, 0, 500), FVector(0.26, 8.45, 0.27));
    }
    if (!bRoadContract)
        return;
    auto* Paint = GeneratedMaterial(TEXT("GoldMarker"));
    for (const FVector2D Ford : Fords)
    {
        const double Shoulder = RiverAt(Ford.Y).Width * 0.5 + 15.0;
        for (const double Bank : {-1.0, 1.0})
            for (const double Side : {-1.0, 1.0})
            {
                const FVector2D Point = Ford + FVector2D(Bank * Shoulder, Side * 3.3);
                const FVector Base = FVector(Point.X, Point.Y, Height(Point)) * 100;
                Add(Paint, Base + FVector(0, 0, 56), FVector(0.13, 0.13, 1.12));
                Add(Paint, Base + FVector(0, 0, 116), FVector(0.10, 0.37, 0.15));
            }
    }
}

bool AMudTerrain::HitsTree(FVector2D XY, double Radius, bool bClear)
{
    // Tree removal is handled by root failure and persistent rigid bodies.
    TSet<int32> RockTiles;
    for(double DX:{-Radius-2.1,Radius+2.1})for(double DY:{-Radius-2.1,Radius+2.1})RockTiles.Add(TileIndex(XY.X+DX,XY.Y+DY));
    for(int32 Tile:RockTiles)for(auto It=BoulderTiles.CreateConstKeyIterator(Tile);It;++It)
    {const auto& Rock=Boulders[It.Value()];if(FVector2D::Distance(XY,Rock.XY)<Radius+Rock.Radius)return true;}
    const FVector Probe(XY.X*100,XY.Y*100,(Height(XY)+.9)*100);
    for(AActor* Actor:FallenActors)
    {
        auto* Body=Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        const FVector Center=Body->GetComponentLocation(),Axis=Body->GetUpVector()*Body->GetComponentScale().Z*50;
        if(FMath::PointDistToSegment(Probe,Center-Axis,Center+Axis)<Radius*100+Body->GetComponentScale().X*50)return true;
    }
    bool bHit = false;
    TSet<int32> Tiles;
    for (const double DX : {-Radius, Radius})
        for (const double DY : {-Radius, Radius})
            Tiles.Add(TileIndex(XY.X + DX, XY.Y + DY));
    for (int32 Tile : Tiles)
    {
        for (auto It = TreeTiles.CreateConstKeyIterator(Tile); It; ++It)
        {
            const int32 Id = It.Value();
            if (Trees[Id].bRemoved || FVector2D::DistSquared(XY, Trees[Id].XY) > FMath::Square(Radius+Trees[Id].Root.Diameter*.5))
                continue;
            bHit = true;
        }
    }
    return bHit;
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
    Data->SetStringField(TEXT("worldId"),WorldId);
    SaveEnvironment(*Data);
    TArray<TSharedPtr<FJsonValue>> Edits, Removed, Tracks, Falling;
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
        TArray<TSharedPtr<FJsonValue>> Packing;
        for (const auto& Layer : Entry.Value.Layers) Packing.Add(MakeShared<FJsonValueNumber>(Layer.Packing));
        Row->SetArrayField(TEXT("packing"), Packing);
        if (const float* Finish = WorkFinish.Find(Entry.Key))
            Row->SetNumberField(TEXT("finish"), *Finish);
        Edits.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (int32 Id = 0; Id < Trees.Num(); ++Id)
        if (Trees[Id].bRemoved&&!Trees[Id].bFallen)
            Removed.Add(MakeShared<FJsonValueNumber>(Id));
    Data->SetArrayField(TEXT("edits"), Edits);
    Data->SetArrayField(TEXT("removedTrees"), Removed);
    for (const auto& Entry : TrackWear)
    {
        Tracks.Add(MakeShared<FJsonValueNumber>(Entry.Key));
        Tracks.Add(MakeShared<FJsonValueNumber>(Entry.Value));
    }
    Data->SetArrayField(TEXT("trackWear"), Tracks);
    for (const auto& Portion : Pours)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for (const double Value : {Portion.Position.X, Portion.Position.Y, Portion.Position.Z,
            Portion.Velocity.X, Portion.Velocity.Y, Portion.Velocity.Z, Portion.Radius})
            Values.Add(MakeShared<FJsonValueNumber>(Value));
        for (const double Volume : Portion.Mass.Volumes)
            Values.Add(MakeShared<FJsonValueNumber>(Volume));
        Falling.Add(MakeShared<FJsonValueArray>(MoveTemp(Values)));
    }
    Data->SetArrayField(TEXT("fallingLoads"), Falling);
    return Data;
}

bool AMudTerrain::RestoreTerrain(const TSharedPtr<FJsonObject>& Data)
{
    FString SavedWorld;
    if(!Data.IsValid())return false;
    Data->TryGetStringField(TEXT("worldId"),SavedWorld);
    if(SavedWorld!=WorldId||!RestoreEnvironment(*Data,true))return false;
    const TArray<TSharedPtr<FJsonValue>> *Edits, *Removed;
    if (!Data.IsValid() || !Data->TryGetArrayField(TEXT("edits"), Edits) ||
        !Data->TryGetArrayField(TEXT("removedTrees"), Removed))
        return false;
    TMap<int32, Earthwork::Column> Restored;
    TMap<int32, float> RestoredFinish;
    TMap<int32, uint8> RestoredTracks;
    TArray<FPour> RestoredPours;
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
    if (Data->HasField(TEXT("fallingLoads")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Falling;
        if (!Data->TryGetArrayField(TEXT("fallingLoads"), Falling) || Falling->Num() > 4096)
            return false;
        for (const auto& Row : *Falling)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values;
            if (!Row->TryGetArray(Values) || (Values->Num() != 10 && Values->Num() != 7 + static_cast<int32>(MaterialCount)))
                return false;
            double Number[7 + MaterialCount]{};
            for (int32 I = 0; I < Values->Num(); ++I)
                if (!(*Values)[I]->TryGetNumber(Number[I]) || !FMath::IsFinite(Number[I]))
                    return false;
            const FVector Position(Number[0], Number[1], Number[2]);
            const FVector Velocity(Number[3], Number[4], Number[5]);
            const double Radius = Number[6];
            if (Position.X < -16 || Position.Y < -16 || Position.X > GetWorldSize() + 16 ||
                Position.Y > GetWorldSize() + 16 || FMath::Abs(Position.Z) > 5000 || Velocity.SizeSquared() > 10000 ||
                Radius <= 0.5 || Radius > 8.0)
                return false;
            Load Mass;
            for (size_t M = 0; M < MaterialCount; ++M)
            {
                if (Number[7 + M] < 0 || Number[7 + M] > 10)
                    return false;
                Mass.Volumes[M] = Number[7 + M];
            }
            if (Mass.Volume() > Epsilon)
                RestoredPours.Add({Position, Velocity, Radius, Mass});
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
        const TArray<TSharedPtr<FJsonValue>>* Packing = nullptr;
        if ((*Row)->HasField(TEXT("packing")) && (!(*Row)->TryGetArrayField(TEXT("packing"), Packing) || Packing->Num() != JsonLayers->Num() / 2))
            return false;
        for (int32 I = 0; I < JsonLayers->Num(); I += 2)
        {
            double Type, Volume;
            if (!(*JsonLayers)[I]->TryGetNumber(Type) || !(*JsonLayers)[I + 1]->TryGetNumber(Volume) ||
                Type < 0 || Type >= static_cast<double>(Material::Count) || Type != FMath::FloorToDouble(Type) ||
                !FMath::IsFinite(Volume) || Volume < 0)
                return false;
            double Packed = 0;
            if (Packing && (!(*Packing)[I / 2]->TryGetNumber(Packed) || !FMath::IsFinite(Packed) || Packed < 0 || Packed > 1))
                return false;
            if (Volume > 0) Column.Layers.push_back({static_cast<Material>(static_cast<uint8>(Type)), Volume, Packed});
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
    Pours = MoveTemp(RestoredPours);
    for (const auto& Entry : TrackWear)
        DirtyCell(Entry.Key);
    Changed = MoveTemp(Restored);
    SoilFlow.Reset(Samples);
    AccountedMass.Empty();
    MassDelta = 0;
    WorkFinish = MoveTemp(RestoredFinish);
    bRubbleDirty = true;
    NextRubbleUpdate = 0;
    Heights = Original;
    SurfaceHalfSamples.Reset();
    bRestoringTerrain=true;
    for (const auto& Entry : Changed)
    {
        UpdateCell(Entry.Key);
    }
    bRestoringTerrain=false;
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
    return RestoreEnvironment(*Data,false);
}
