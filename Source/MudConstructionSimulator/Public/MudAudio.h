#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Earthwork.h"
#include "MudAudio.generated.h"

class AMudMachine;
class AMudTerrain;
class UAudioComponent;
class USoundAttenuation;
class USoundWave;

UCLASS()
class UMudAudio : public UActorComponent
{
    GENERATED_BODY()

public:
    void Initialize();
    void Mix(float Dt, const AMudTerrain& Terrain, FVector2D Focus, AMudMachine* Machine,
        const TArray<AMudMachine*>& Fleet, bool bCab, bool bBrake);
    void Footstep(const AMudTerrain& Terrain, FVector Location, float Strength, bool bLanding = false);
    void Play(const TCHAR* Cue, FVector Location, float Volume = 1.0f, float Pitch = 1.0f);
    void Interface(const TCHAR* Cue);

private:
    enum : int32 { EngineCount = 5, LoadedEngines = EngineCount, Tyres = 2 * EngineCount,
        Tracks, TrackScrub, Hydraulic, Soil, Gravel, GravelLift, Water,
        Reverse, Wind, River, LoopCount };

    void Loop(int32 Index, FVector Location, float Volume, float Pitch, float Dt, float LowPass = 20000);
    void TruckAir(float Dt, const AMudMachine& Machine, bool bBrake, bool bCab, bool bEntered);
    USoundWave* Sound(FName Name) const;

    UPROPERTY(Transient) TMap<FName, TObjectPtr<USoundWave>> Library;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Loops;
    UPROPERTY(Transient) TObjectPtr<USoundAttenuation> WorldCues;
    float Levels[LoopCount]{};
    FVector EngineLocations[EngineCount]{};
    float EnginePitch[EngineCount]{0.83f, 0.83f, 0.83f, 0.83f, 0.83f};
    float EngineLoad = 0;
    double PreviousBucketHeight = 0;
    double PreviousCurl = 0;
    double PreviousTilt = 0;
    float AirPressure = 0.77f;
    float AirValveCooldown = 0;
    bool bAirCharging = true;
    bool bTruckBraking = false;
    FRandomStream Details{48923};
    float BirdTime = 5;
    uint32 LastImpact = 0;
    int32 LastEngine = INDEX_NONE;
};
