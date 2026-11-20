#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Earthwork.h"
#include "TreePhysics.h"
#include "MudTerrain.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UTexture2D;
class UMaterialInterface;
class UStaticMeshComponent;
class FJsonObject;
class AMudMachine;

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
    struct FMudRoadSection
    {
        bool bDeep = false;
        bool bSoft = false;
        bool bRough = false;

        bool IsReady() const { return !bDeep && !bSoft && !bRough; }
    };

    struct FMudRoadSurvey
    {
        FVector2D Start = FVector2D::ZeroVector;
        TArray<FMudRoadSection> Sections;
        int32 Ready = 0;
        int32 Deep = 0;
        int32 Soft = 0;
        int32 Rough = 0;
        double Fill = 0;

        bool IsReady() const { return !Sections.IsEmpty() && Ready == Sections.Num(); }
        int32 Progress() const { return Sections.IsEmpty() ? 0 : Ready * 100 / Sections.Num(); }
    };

    AMudTerrain();
    bool Initialize();
    double Height(FVector2D XY) const;
    double MeshHeight(FVector2D XY, FVector* SurfaceNormal = nullptr) const;
    FVector Normal(FVector2D XY) const;
    double Wetness(FVector2D XY) const;
    double WaterDepth(FVector2D XY) const;
    FVector WaterSoundPoint(FVector2D XY) const;
    double Grip(FVector2D XY) const;
    double ContactSink(FVector2D XY, bool bTracked, double LoadFraction) const;
    Earthwork::Material Surface(FVector2D XY) const;
    Earthwork::Load Excavate(FVector2D XY, double Radius, double Amount, double ToolHeight);
    Earthwork::Load CutPass(FVector2D XY, FVector2D Across, FVector2D Reach, double Amount, double ToolHeight,
        FVector2D Slopes = FVector2D::ZeroVector, bool bBladePass = false);
    Earthwork::Load DrawFromPile(const FMudPile& Pile, FVector2D Approach, double Amount);
    Earthwork::Load SweepCut(FVector From, FVector To, FVector2D Across, FVector2D Reach, double Amount,
        FVector2D Slopes = FVector2D::ZeroVector);
    void Deposit(FVector2D XY, double Radius, const Earthwork::Load& Load);
    void Pour(FVector Origin, FVector Direction, double Radius, const Earthwork::Load& Load);
    void AdvancePours(double Dt);
    void RegisterMaterialReceiver(AMudMachine& Machine);
    void AdvanceSoil(double Dt);
    int32 ActiveSoilCells() const { return static_cast<int32>(SoilFlow.ActiveCells()); }
    double SoilStepMilliseconds = 0;
    double MaterialBalance() const;
    double MaterialVolumeBalance() const;
    bool ValidateSurfaceRendering(FVector2D Focus);
    bool IsSoilLab() const { return bSoilLab; }
    void SettlePours();
    void Spread(FVector2D XY, FVector2D Across, FVector2D Reach, const Earthwork::Load& Load);
    void PressTrack(FVector2D Center, FVector2D Forward, double HalfLength, double HalfWidth,
        double Outward, double Scrub, bool bTracked, double LoadFactor = 1);
    double CompactBody(FVector2D Center,FVector2D Across,FVector2D Reach,double Plane,FVector2D Slopes,
        double PressureKPa,double Dt);
    void ShowSpill(FVector Origin, double Volume, Earthwork::Material Type,
        FVector Direction = FVector::ZeroVector, double FloorZ = -BIG_NUMBER, double ThrowScale = 1.0);
    void UpdateSpills(double Dt);
    double Grade(FVector2D XY, FVector2D Across, FVector2D Reach, double Rate, double ToolHeight, FVector2D Slopes,
        const TArray<FVector>* CuttingEdge = nullptr);
    void UpdateTiles(FVector2D Focus);
    bool HitsTree(FVector2D XY, double Radius, bool bClear);
    void PushTrees(FVector2D XY, double Radius, FVector2D Direction, double ForceN, double ContactHeight, double Dt);
    void UpdateEnvironment(FVector2D Focus, double Dt);
    FString GetWorldId() const { return WorldId; }
    double Remaining(const FMudPile& Pile) const;
    double EstimateFordFill(FVector2D Ford, double Clearance) const;
    FMudRoadSurvey SurveyFordRoad(FVector2D Ford) const;
    bool HasRoadContract() const { return bRoadContract; }
    bool IsRoadReady() const;
    const FMudRoadSurvey* GetRoadSurvey(int32 FordIndex) const;
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
    friend class AMudWorldValidation;
    static constexpr int32 TileSize = 64;
    static constexpr int32 CrownTiers = 3;
    static constexpr int32 RubbleRadius = 36;
    static constexpr double CreekMargin = 5.0;
    struct FTree
    {
        FVector2D XY;
        FTransform Trunk;
        FTransform Crown[CrownTiers];
        bool bRemoved = false;
        bool bFallen = false;
        TreePhysics::Root Root;
        FVector2D BendDirection = FVector2D(1,0);
        UStaticMeshComponent* Body = nullptr;
        double LastPush = -100;
    };
    struct FRiverSample { FVector2D XY; double Z; double Width; };
    struct FGroundPlant
    {
        FTransform Transform;
        int32 Cell;
        bool bHidden = false;
    };
    struct FSpill
    {
        FVector Position = FVector::ZeroVector;
        FVector Velocity = FVector::ZeroVector;
        double Life = 0;
        double Radius = 0;
        double Spin = 0;
        double FloorZ = -BIG_NUMBER;
        bool bBounced = false;
        bool bRolling = false;
    };
    struct FSpillBatch
    {
        TArray<FSpill> Pieces;
        TArray<FTransform> Transforms;
        int32 Next = 0;
        double Credit = 0;
    };
    struct FPour
    {
        FVector Position;
        FVector Velocity;
        double Radius;
        Earthwork::Load Mass;
    };

    int32 Samples = 0;
    int32 TilesPerSide = 0;
    int32 Seed = 0;
    FString WorldId;
    struct FGeology { FVector2D XY, Radii; Earthwork::Material Type; double Depth; };
    TArray<FGeology> Geology;
    struct FBoulder { FVector2D XY; double Radius; };
    TArray<FBoulder> Boulders;
    UPROPERTY() TArray<TObjectPtr<AActor>> FallenActors;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BoulderMesh;
    TSet<int32> BentTrees;
    TMap<int32,UStaticMeshComponent*> TreeContacts;
    TMultiMap<int32,int32> BoulderTiles;
    bool bRestoringTerrain=false;
    void UpdateTreeSupport(int32 Cell);
    int32 CrownInstancesPerTree = CrownTiers;
    TArray<float> Original;
    TArray<float> Heights;
    mutable TMap<int32, float> SurfaceHalfSamples;
    double SurfaceSample(int32 X, int32 Y) const;
    TArray<FRiverSample> River;
    TArray<FRiverSample> Creek;
    TArray<FMudRoadSurvey> FordSurveys;
    double NextFordSurvey = 0;
    bool bRoadContract = false;
    TArray<FMudPit> Pits;
    TMap<int32, Earthwork::Column> Changed;
    Earthwork::ActiveSoil SoilFlow;
    TMap<int32, double> AccountedMass;
    double MassDelta = 0;
    bool bSoilLab = false;
    void InitializeSoilLab();
    TMap<int32, float> WorkFinish;
    TMap<int32, uint8> TrackWear;
    TArray<FTree> Trees;
    TMultiMap<int32, int32> TreeTiles;
    TSet<int32> Dirty;
    TArray<FGroundPlant> Plants;
    TMap<int32, int32> PlantByCell;
    TSet<int32> DirtyPlants;
    TArray<int32> TileSteps;
    TMap<int32,FIntRect> DirtyRegions;
    FSpillBatch Spills[Earthwork::MaterialCount];
    TArray<FPour> Pours;
    TArray<TWeakObjectPtr<AMudMachine>> MaterialReceivers;
    FRandomStream SpillRandom{66};
    FVector2D RubbleFocus{-10000, -10000};
    double NextRubbleUpdate = 0;
    bool bRubbleDirty = true;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> TerrainMeshes;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> PourSurfaces;
    bool bPourSurfacesVisible = false;
    void UpdatePourSurfaces();
    UPROPERTY() TObjectPtr<UMaterialInterface> GroundMaterial;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Trunks;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Crowns;
    UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Meadow;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Spoil;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> GroundRubble;
    UPROPERTY() TObjectPtr<UTexture2D> Overview;

    int32 Index(int32 X, int32 Y) const { return Y * Samples + X; }
    int32 TileIndex(int32 X, int32 Y) const;
    Earthwork::Column OriginalColumn(int32 Cell) const;
    Earthwork::Column& Edit(int32 Cell);
    void UpdateCell(int32 Cell, double WorkDelta = 0);
    void DirtyCell(int32 Cell);
    TArray<int32> Cells(FVector2D XY, double Radius) const;
    TArray<TPair<int32, double>> ToolFootprint(FVector2D XY, FVector2D Across, FVector2D Reach, bool bBladePass) const;
    void ApplyFill(const TArray<TPair<int32, double>>& Area, const Earthwork::Load& Load);
    void LandPour(FVector2D Contact, FPour& Pour);
    FRiverSample RiverAt(double Y) const;
    FRiverSample CreekAt(double X) const;
    double AggregateCover(int32 Cell) const;
    static FRiverSample SampleChannel(const TArray<FRiverSample>& Channel, double Row);
    void BuildTile(int32 Id, int32 Step);
    void BuildGroundCover();
    void UpdateGroundCover();
    void UpdateGroundRubble(FVector2D Focus);
    void BuildWater();
    void BuildMarkers();
    void HideTree(int32 Id);
    void ActivateTree(int32 Id);
    void BendTree(int32 Id);
    void SaveEnvironment(FJsonObject& Data) const;
    bool RestoreEnvironment(const FJsonObject& Data, bool ValidateOnly);
};
