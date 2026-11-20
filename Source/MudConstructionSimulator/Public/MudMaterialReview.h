#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MudMaterialReview.generated.h"
class AMudTerrain;
class AMudMachine;
class UCameraComponent;

UCLASS()
class AMudMaterialReview : public AActor
{
    GENERATED_BODY()
public:
    AMudMaterialReview();
    void Initialize(AMudTerrain& Ground,const TArray<AMudMachine*>& Machines);
    void Tick(float Dt) override;
private:
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<AMudTerrain> Terrain;
    UPROPERTY() TArray<AMudMachine*> Fleet;
    FVector2D Center;
    FString Folder;
    int32 Frame=0;
    bool bDozerReview=false;
    TArray<double> Timings;
    TArray<double> WorkTimings;
    void View(FVector Location,FVector Target);
};
