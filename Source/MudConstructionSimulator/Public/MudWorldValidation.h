#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MudWorldValidation.generated.h"
class AMudTerrain;
UCLASS()
class AMudWorldValidation:public AActor
{
    GENERATED_BODY()
public:
    AMudWorldValidation();
    void Initialize(AMudTerrain* Ground);
    void Tick(float Dt) override;
private:
    UPROPERTY() TObjectPtr<AMudTerrain> Terrain;
    int32 Small=-1,Large=-1;
    double Elapsed=0;
    bool Complete=false;
    void Finish(bool Passed);
    bool TestMobility();
};
