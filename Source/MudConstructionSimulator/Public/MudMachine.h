#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Earthwork.h"
#include "MudMachine.generated.h"

class AMudTerrain;
class UStaticMeshComponent;
class UProceduralMeshComponent;

enum class EMudMachine : uint8 { Dozer, Excavator, Dumper, Truck };

UCLASS()
class AMudMachine : public AActor
{
    GENERATED_BODY()

public:
    AMudMachine();
    void Initialize(EMudMachine Kind, FVector2D Position, AMudTerrain& Terrain);
    void Drive(double Dt, double Throttle, double Steering, bool bBrake, AMudTerrain& Terrain,
        const TArray<AMudMachine*>& Fleet);
    void Work(double Dt, double AxisA, double AxisB, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
    void ToggleTool();
    void ChangeGroup();
    void ToggleUnload();
    void UpdatePose(const AMudTerrain& Terrain);
    FVector ToolPoint() const;
    FVector CameraSeat() const;
    FString ToolDescription() const;
    const TCHAR* Name() const;
    double Capacity() const;
    double Grip = 1;
    double Water = 0;
    double Speed = 0;
    double Heading = 0;
    double Boom = 15;
    double Stick = -28;
    double Curl = 0;
    double Swing = 0;
    double Blade = 0;
    FVector2D Position;
    Earthwork::Load Cargo;
    EMudMachine Kind = EMudMachine::Dozer;
    bool bTool = false;
    bool bGrade = false;
    bool bUnload = false;
    bool bOtherGroup = false;
    int32 RolledSide = 0;

private:
    UStaticMeshComponent* Part(const TCHAR* Shape, const TCHAR* Paint, USceneComponent* Parent,
        FVector PositionCm, FVector SizeM, FRotator Rotation = FRotator::ZeroRotator);
    UProceduralMeshComponent* BuildHeap(USceneComponent* Parent, FVector PositionCm, FVector SizeM);
    UPROPERTY() TObjectPtr<USceneComponent> Turret;
    UPROPERTY() TObjectPtr<USceneComponent> BoomPivot;
    UPROPERTY() TObjectPtr<USceneComponent> StickPivot;
    UPROPERTY() TObjectPtr<USceneComponent> Tool;
    UPROPERTY() TObjectPtr<USceneComponent> TipBed;
    UPROPERTY() TObjectPtr<USceneComponent> RearFrame;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> Payload;
    UPROPERTY() TArray<TObjectPtr<USceneComponent>> Wheels;
    Earthwork::Material CargoVisual = Earthwork::Material::Soil;
    double WheelAngle = 0;
    double Articulation = 0;
    double OffRoadRadius() const;
    void EmptyBucket(double Amount, AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet);
};
