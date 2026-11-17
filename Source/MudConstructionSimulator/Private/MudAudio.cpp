#include "MudAudio.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MudMachine.h"
#include "MudTerrain.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

namespace
{
constexpr const TCHAR* LoopNames[] = {TEXT("EngineDozer"), TEXT("EngineExcavator"), TEXT("EngineDumper"),
    TEXT("EngineTruck"), TEXT("EngineLoader"), TEXT("EngineDozerLoad"), TEXT("EngineExcavatorLoad"),
    TEXT("EngineDumperLoad"), TEXT("EngineTruckLoad"), TEXT("EngineLoaderLoad"), TEXT("Tyres"),
    TEXT("Tracks"), TEXT("TrackScrub"), TEXT("Hydraulic"), TEXT("Soil"), TEXT("Gravel"),
    TEXT("GravelLift"), TEXT("WaterVehicle"), TEXT("Reverse"), TEXT("Wind"), TEXT("River")};
constexpr const TCHAR* CueNames[] = {TEXT("Door"), TEXT("Switch"), TEXT("Ignition"), TEXT("Impact"), TEXT("Tree"),
    TEXT("Goal"), TEXT("Notice"), TEXT("Brake"), TEXT("Jump"), TEXT("Land"), TEXT("BirdA"), TEXT("BirdB"), TEXT("StepSoilA"),
    TEXT("StepSoilB"), TEXT("StepMudA"), TEXT("StepMudB"), TEXT("StepStoneA"), TEXT("StepStoneB"),
    TEXT("StepWaterA"), TEXT("StepWaterB"), TEXT("TruckBrakeRelease"), TEXT("TruckAirPurge")};

FVector WorldPoint(FVector2D XY, double Height) { return FVector(XY.X, XY.Y, Height) * 100; }
}

void UMudAudio::Initialize()
{
    if (!GetWorld()->AllowAudioPlayback())
        return;
    WorldCues = NewObject<USoundAttenuation>(this);
    WorldCues->Attenuation.AttenuationShapeExtents = FVector(390);
    WorldCues->Attenuation.FalloffDistance = 3400;
    const auto Load = [this](const TCHAR* Name)
    {
        const FString Path = FString::Printf(TEXT("/Game/Generated/Audio/%s.%s"), Name, Name);
        if (auto* Wave = LoadObject<USoundWave>(nullptr, *Path))
            Library.Add(FName(Name), Wave);
    };
    for (const auto* Name : CueNames)
        Load(Name);
    static_assert(UE_ARRAY_COUNT(LoopNames) == LoopCount);
    Loops.SetNum(LoopCount);
    for (int32 Index = 0; Index < LoopCount; ++Index)
    {
        Load(LoopNames[Index]);
        auto* Wave = Sound(LoopNames[Index]);
        if (!Wave)
            continue;
        auto* Component = NewObject<UAudioComponent>(GetOwner());
        Component->bAutoActivate = false;
        Component->SetSound(Wave);
        Component->SetVolumeMultiplier(0);
        Component->SetLowPassFilterEnabled(true);
        if (Index != Wind)
        {
            FSoundAttenuationSettings Attenuation;
            Attenuation.AttenuationShapeExtents = FVector(Index < Tyres ? 650 : 110);
            Attenuation.FalloffDistance = Index == River ? 3000 : 4000;
            Component->SetAttenuationOverrides(Attenuation);
        }
        Component->RegisterComponentWithWorld(GetWorld());
        Loops[Index] = Component;
    }
    const int32 Expected = LoopCount + UE_ARRAY_COUNT(CueNames);
    if (Library.Num() == Expected)
    {
        UE_LOG(LogTemp, Display, TEXT("Valley audio ready: %d sound effects"), Expected);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Valley audio missing assets: %d of %d sound effects loaded"), Library.Num(), Expected);
    }
}

USoundWave* UMudAudio::Sound(FName Name) const
{
    const auto* Wave = Library.Find(Name);
    return Wave ? Wave->Get() : nullptr;
}

void UMudAudio::Loop(int32 Index, FVector Location, float Volume, float Pitch, float Dt, float LowPass)
{
    auto* Component = Loops[Index].Get();
    if (!Component)
        return;
    float& Level = Levels[Index];
    Level = FMath::Lerp(Level, Volume, 1.0f - FMath::Exp(-Dt * (Volume > Level ? 10.0f : 5.5f)));
    if (Level < 0.002f && Volume < 0.002f)
    {
        if (Component->IsPlaying())
            Component->Stop();
        return;
    }
    Component->SetWorldLocation(Location);
    Component->SetVolumeMultiplier(Level);
    Component->SetPitchMultiplier(Pitch);
    Component->SetLowPassFilterFrequency(LowPass);
    if (!Component->IsPlaying())
        Component->Play();
}

void UMudAudio::Mix(float Dt, const AMudTerrain& Terrain, FVector2D Focus, AMudMachine* Machine,
    const TArray<AMudMachine*>& Fleet, bool bCab, bool bBrake)
{
    if (Loops.IsEmpty())
        return;
    const int32 Engine = Machine ? static_cast<int32>(Machine->Kind) : INDEX_NONE;
    const FMudMachineSound Motion = Machine ? Machine->GetSoundState() : FMudMachineSound{};
    const FVector Body = Machine ? Machine->GetActorLocation() + FVector(0, 0, 105) : FVector::ZeroVector;
    const bool bTracked = Machine && (Machine->Kind == EMudMachine::Excavator || Machine->Kind == EMudMachine::Dozer);
    const float Filter = bCab ? 2300 : 20000;
    const bool bChangedMachine = Engine != LastEngine;
    if (bChangedMachine)
    {
        if (Machine)
            Play(TEXT("Ignition"), Body, 0.45f);
        LastEngine = Engine;
        LastImpact = Motion.Impact;
        EngineLoad = 0;
    }
    if (Machine && LastImpact != Motion.Impact)
    {
        Play(Motion.bTreeImpact ? TEXT("Tree") : TEXT("Impact"), Body, Motion.ImpactLevel, Details.FRandRange(0.80f, 1.12f));
        LastImpact = Motion.Impact;
    }
    const float GroundSpeed = FMath::Min(1.0f, Motion.Travel * 0.16f);
    const float Wet = Machine ? Terrain.Wetness(Machine->Position) : 0.0f;
    const float WaterDepth = Machine ? Machine->Water : 0.0f;
    const float Load = FMath::Clamp(Motion.Drive * (bBrake ? 0.32f : 0.85f) + Motion.Hydraulic * 0.53f +
        Motion.EarthFlow * 0.09f, 0.0f, 1.0f);
    EngineLoad = FMath::Lerp(EngineLoad, Load, 1.0f - FMath::Exp(-Dt * (Load > EngineLoad ? 4.8f : 2.4f)));
    if (Machine)
    {
        EngineLocations[Engine] = Body;
        const float Revs = 0.83f + 0.32f * EngineLoad + 0.13f * GroundSpeed;
        EnginePitch[Engine] = FMath::Lerp(EnginePitch[Engine], Revs, 1.0f - FMath::Exp(-Dt * 5.0f));
    }
    const float CabGain = bCab ? 0.78f : 1.0f;
    for (int32 Index = 0; Index < EngineCount; ++Index)
    {
        const float Blend = Index == Engine ? EngineLoad : 0;
        Loop(Index, EngineLocations[Index], Index == Engine ? (0.45f - 0.31f * Blend) * CabGain : 0,
            EnginePitch[Index], Dt, Filter);
        Loop(LoadedEngines + Index, EngineLocations[Index], 0.64f * Blend * CabGain,
            EnginePitch[Index], Dt, Filter);
    }

    Loop(Tyres, Body, bTracked ? 0 : GroundSpeed * 0.47f, 0.78f + GroundSpeed * 0.58f, Dt, bCab ? Filter : 3900 - Wet * 2600);
    const FVector Undercarriage = Body - FVector(0, 0, 95);
    const float BeltSpeed = bTracked ? FMath::Min(Motion.Travel * 0.32f, 1.0f) : 0;
    // Travel includes driven slip and belt motion from turning on the spot.
    const float Scrub = bTracked ? FMath::Clamp((Motion.Travel - FMath::Abs(Machine->Speed)) * 0.52, 0.0, 1.0) : 0;
    const float TrackPitch = Engine == static_cast<int32>(EMudMachine::Dozer) ? 0.92f : 1.09f;
    Loop(Tracks, Undercarriage, FMath::Sqrt(BeltSpeed) * 0.53f, TrackPitch * (0.61f + BeltSpeed * 0.79f), Dt, Filter);
    Loop(TrackScrub, Undercarriage, Scrub * (0.56f - Wet * 0.16f), 0.78f + Scrub * 0.33f, Dt, Filter);
    Loop(Water, Body, GroundSpeed * FMath::Clamp(WaterDepth * 2.9f + Wet * 0.12f, 0.0f, 0.84f), 0.86f + GroundSpeed * 0.32f, Dt, Filter);
    Loop(Reverse, Body, Machine && Machine->Speed < -0.24 ? 0.37f : 0, 1, Dt, bCab ? 2400 : 8500);
    if (Machine && Machine->Kind == EMudMachine::Truck)
        TruckAir(Dt, *Machine, bBrake, bCab, bChangedMachine);

    auto* Working = Machine;
    FMudMachineSound Work = Motion;
    for (auto* Other : Fleet)
    {
        if (Other == Machine || !Other->IsTipping() || FVector2D::DistSquared(Focus, Other->Position) > FMath::Square(28.0))
            continue;
        const auto Candidate = Other->GetSoundState();
        if (Candidate.EarthFlow + Candidate.Hydraulic > Work.EarthFlow + Work.Hydraulic)
        {
            Working = Other;
            Work = Candidate;
        }
    }
    const FVector Edge = Working ? Working->ToolPoint() * 100 : Body;
    const float Cut = FMath::Sqrt(FMath::Clamp(Work.EarthFlow * 0.17f, 0.0f, 1.0f)) * 0.73f;
    Loop(Hydraulic, Working ? Working->GetActorLocation() + FVector(0, 0, 160) : Body, Work.Hydraulic * 0.48f,
        0.79f + Work.Hydraulic * 0.46f, Dt, Filter);
    Loop(Soil, Edge, Work.Material == Earthwork::Material::Soil ? Cut : 0, 0.87f + Cut * 0.29f, Dt, Filter);
    Loop(Gravel, Edge, Work.Material == Earthwork::Material::Soil ? 0 : Cut, Work.Material == Earthwork::Material::Rock ? 0.82f : 1.0f, Dt, Filter);

    FVector Bucket = Body;
    float GravelRattle = 0;
    float GravelPitch = 1;
    if (Machine && (Machine->Kind == EMudMachine::Excavator || Machine->Kind == EMudMachine::Loader))
    {
        Bucket = Working == Machine ? Edge : Machine->ToolPoint() * 100;
        const double Height = Machine->GetActorTransform().InverseTransformPositionNoScale(Bucket).Z / 100;
        if (!bChangedMachine && Dt > UE_SMALL_NUMBER && Motion.Hydraulic > 0.015f)
        {
            const auto& Cargo = Machine->Cargo;
            const double Stones = Cargo.Volumes[static_cast<size_t>(Earthwork::Material::Rock)];
            const double GravelAmount = Cargo.Volumes[static_cast<size_t>(Earthwork::Material::Gravel)];
            const double Lift = FMath::Abs(Height - PreviousBucketHeight) / Dt;
            const double Tilt = (FMath::Abs(Machine->Curl - PreviousCurl) +
                FMath::Abs(Machine->BucketTilt - PreviousTilt)) / Dt;
            const double Fill = (GravelAmount + Stones) / Machine->Capacity();
            GravelRattle = FMath::Clamp((Lift * 0.59 + Tilt * 0.012 - 0.07) * FMath::Sqrt(Fill), 0.0, 0.58) *
                (1.0f - FMath::Min(Motion.EarthFlow * 0.22f, 0.85f));
            GravelPitch = Stones > GravelAmount ? 0.84f : 1.07f;
        }
        PreviousBucketHeight = Height;
        PreviousCurl = Machine->Curl;
        PreviousTilt = Machine->BucketTilt;
    }
    Loop(GravelLift, Bucket, GravelRattle, GravelPitch, Dt, Filter);

    const FVector RiverSource = Terrain.RiverSoundPoint(Focus.Y);
    Loop(River, RiverSource, FVector2D::DistSquared(Focus, FVector2D(RiverSource.X, RiverSource.Y) / 100) < FMath::Square(38.0) ? 0.45f : 0,
        1, Dt, bCab ? 900 : 13000);
    Loop(Wind, FVector::ZeroVector, bCab ? 0.105f : 0.29f, 1, Dt, bCab ? 750 : 8500);
    BirdTime -= Dt;
    if (BirdTime <= 0)
    {
        BirdTime = Details.FRandRange(9.0f, 24.0f);
        const double Angle = Details.FRandRange(0.0f, UE_TWO_PI);
        const FVector2D Place = Focus + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Details.FRandRange(7.0f, 19.0f);
        Play(Details.RandRange(0, 1) ? TEXT("BirdA") : TEXT("BirdB"), WorldPoint(Place, Terrain.Height(Place) + 4),
            bCab ? 0.19f : 0.57f, Details.FRandRange(0.93f, 1.06f));
    }
}

void UMudAudio::TruckAir(float Dt, const AMudMachine& Machine, bool bBrake, bool bCab, bool bEntered)
{
    const FVector Valve = Machine.GetActorLocation() - Machine.GetActorForwardVector() * 145.0 + FVector(0, 0, 58);
    const float Gain = bCab ? 0.64f : 1.0f;
    AirValveCooldown = FMath::Max(0.0f, AirValveCooldown - Dt);
    if (bEntered)
        bTruckBraking = false;
    if (bBrake && !bTruckBraking)
        AirPressure = FMath::Max(0.55f, AirPressure - 0.084f);
    if (!bBrake && bTruckBraking && AirValveCooldown <= 0)
    {
        Play(TEXT("TruckBrakeRelease"), Valve, 0.66f * Gain, Details.FRandRange(0.92f, 1.07f));
        AirValveCooldown = 0.62f;
    }
    bTruckBraking = bBrake;
    // The governor starts charging below cut-in and purges once at cut-out.
    bAirCharging |= AirPressure <= 0.82f;
    if (!bAirCharging)
        return;
    AirPressure = FMath::Min(1.0f, AirPressure + Dt * (0.012f + EngineLoad * 0.009f));
    if (AirPressure >= 1.0f && AirValveCooldown <= 0)
    {
        Play(TEXT("TruckAirPurge"), Valve, 0.76f * Gain, Details.FRandRange(0.94f, 1.04f));
        AirValveCooldown = 1.0f;
        bAirCharging = false;
    }
}

void UMudAudio::Play(const TCHAR* Cue, FVector Location, float Volume, float Pitch)
{
    if (auto* Wave = Sound(Cue))
        UGameplayStatics::PlaySoundAtLocation(this, Wave, Location, Volume, Pitch, 0, WorldCues);
}

void UMudAudio::Interface(const TCHAR* Cue)
{
    if (auto* Wave = Sound(Cue))
        UGameplayStatics::PlaySound2D(this, Wave, 0.47f);
}

void UMudAudio::Footstep(const AMudTerrain& Terrain, FVector Location, float Strength, bool bLanding)
{
    const FVector2D Cell(Location.X / 100, Location.Y / 100);
    const bool B = Details.RandRange(0, 1) != 0;
    const TCHAR* Name;
    if (Terrain.WaterDepth(Cell) > 0.035)
        Name = B ? TEXT("StepWaterB") : TEXT("StepWaterA");
    else if (Terrain.Surface(Cell) != Earthwork::Material::Soil)
        Name = B ? TEXT("StepStoneB") : TEXT("StepStoneA");
    else if (Terrain.Wetness(Cell) > 0.53)
        Name = B ? TEXT("StepMudB") : TEXT("StepMudA");
    else
        Name = B ? TEXT("StepSoilB") : TEXT("StepSoilA");
    Play(Name, Location, Strength, Details.FRandRange(0.88f, 1.12f));
    if (bLanding)
        Play(TEXT("Land"), Location, Strength * 0.65f, Details.FRandRange(0.91f, 1.07f));
}
