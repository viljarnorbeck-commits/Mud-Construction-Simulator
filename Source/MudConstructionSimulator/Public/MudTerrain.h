#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Earthwork.h"
#include "MudTerrain.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UTexture2D;
class UMaterialInterface;
class FJsonObject;

struct FMudPile
{
    FVector2D Center;
    double Radius;
    double Height;
    Earthwork::Material Material;
    double InitialVolume;
};

struct FMudPit
{
    FVector2D Center;
    FVector2D Radii;
};

UCLASS()
class AMudTerrain : public AActor
{
    GENERATED_BODY()

public:
    AMudTerrain();
    bool Initialize();
    double Height(FVector2D XY) const;
    FVector Normal(FVector2D XY) const;
    double Wetness(FVector2D XY) const;
    double WaterDepth(FVector2D XY) const;
    double Grip(FVector2D XY) const;
    Earthwork::Material Surface(FVector2D XY) const;
    Earthwork::Load Excavate(FVector2D XY, double Radius, double Amount, double ToolHeight);
    Earthwork::Load DrawFromPile(const FMudPile& Pile, FVector2D Approach, double Amount);
    void Deposit(FVector2D XY, double Radius, const Earthwork::Load& Load);
    void ShowSpill(FVector Origin, double Volume);
    void UpdateSpills(double Dt);
    double Grade(FVector2D XY, double Radius, double Rate);
    void UpdateTiles(FVector2D Focus);
    bool HitsTree(FVector2D XY, double Radius, bool bClear);
    double Remaining(const FMudPile& Pile) const;
    double EstimateFordFill(FVector2D Ford, double Clearance) const;
    TSharedRef<FJsonObject> SaveTerrain() const;
    bool RestoreTerrain(const TSharedPtr<FJsonObject>& Data);
    int32 GetSeed() const { return Seed; }
    int32 GetWorldSize() const { return Samples - 1; }
    FVector Start = FVector::ZeroVector;
    FVector Goal = FVector::ZeroVector;
    TArray<FMudPile> Piles;
    TArray<FVector2D> Fords;
    UTexture2D* GetOverview() const { return Overview; }

private:
    static constexpr int32 TileSize = 64;
    static constexpr int32 CrownTiers = 3;
    struct FTree
    {
        FVector2D XY;
        FTransform Trunk;
        FTransform Crown[CrownTiers];
        bool bRemoved = false;
    };
    struct FRiverSample { FVector2D XY; double Z; double Width; };
    struct FSpill
    {
        FVector Position = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        double Life = 0;
        double Radius = 0;
    };

    int32 Samples = 0;
    int32 TilesPerSide = 0;
    int32 Seed = 0;
    TArray<float> Original;
    TArray<float> Heights;
    TArray<FRiverSample> River;
    TArray<FMudPit> Pits;
    TMap<int32, Earthwork::Column> Changed;
    TArray<FTree> Trees;
    TMultiMap<int32, int32> TreeTiles;
    TSet<int32> Dirty;
    TArray<int32> TileSteps;
    TArray<FSpill> Spills;
    TArray<FTransform> SpillTransforms;
    int32 NextSpill = 0;
    double SpillCredit = 0;
    FRandomStream SpillRandom{66};
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> TerrainMeshes;
    UPROPERTY() TObjectPtr<UMaterialInterface> GroundMaterial;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Trunks;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Crowns;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Spoil;
    UPROPERTY() TObjectPtr<UTexture2D> Overview;

    int32 Index(int32 X, int32 Y) const { return Y * Samples + X; }
    int32 TileIndex(int32 X, int32 Y) const;
    Earthwork::Column OriginalColumn(int32 Cell) const;
    Earthwork::Column& Edit(int32 Cell);
    void UpdateCell(int32 Cell);
    TArray<int32> Cells(FVector2D XY, double Radius) const;
    FRiverSample RiverAt(double Y) const;
    void BuildTile(int32 Id, int32 Step);
    void BuildWater();
    void BuildMarkers();
    void HideTree(int32 Id);
};
