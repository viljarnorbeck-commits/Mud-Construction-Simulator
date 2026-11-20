#include "LoaderValidation.h"
#include "LoaderRig.h"
#include "MudTerrain.h"
#include "MudMachine.h"
#include "Dom/JsonObject.h"

// Runs the production Work/Drive/pour solver in the ordinary world, before any user save is loaded.
bool RunLoaderValidation(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    using namespace Earthwork;
    if (Fleet.Num() != 5) return false;
    constexpr double Dt = 1.0 / 60;
    const auto Original = Terrain.SaveTerrain();
    const auto XY = [](FVector P) { return FVector2D(P.X, P.Y); };
    const auto Balance = [&]()
    {
        double Mass = Terrain.MaterialBalance();
        for (auto* Machine : Fleet) Mass += Machine->Cargo.Mass();
        return Mass;
    };
    auto* Loader = Fleet[4];
    const FVector2D Start = XY(Terrain.Start);
    FVector2D Center = Start;
    double Best = BIG_NUMBER;
    // Find a dry, unobstructed patch without replacing the world's terrain or physics.
    for (int32 X = -40; X <= 40; X += 8)
        for (int32 Y = -40; Y <= 40; Y += 8)
        {
            const FVector2D Candidate = Start + FVector2D(X,Y);
            if (Candidate.X < 30 || Candidate.Y < 30 || Candidate.X > Terrain.GetWorldSize()-30 ||
                Candidate.Y > Terrain.GetWorldSize()-30) continue;
            double Low = BIG_NUMBER, High = -BIG_NUMBER;
            bool Clear = true;
            for (int32 I = -4; I <= 18; I += 2)
                for (int32 J = -4; J <= 4; J += 2)
                {
                    const auto P = Candidate + FVector2D(I,J);
                    Low = FMath::Min(Low, Terrain.Height(P)); High = FMath::Max(High, Terrain.Height(P));
                    Clear &= Terrain.WaterDepth(P) < 0.03 && !Terrain.HitsTree(P, 1.5, false);
                }
            if (Clear && High - Low < Best) { Best = High - Low; Center = Candidate; }
        }
    bool Passed = Best < 1.0;
    double MaximumError = 0;
    for (const int32 ReceiverIndex : {2,3})
    {
        Passed &= Terrain.RestoreTerrain(Original);
        for (int32 I = 0; I < Fleet.Num(); ++I)
        {
            Fleet[I]->StopWork(); Fleet[I]->Cargo = {};
            Fleet[I]->Position = Center + FVector2D(-25, 12 + I * 8);
            Fleet[I]->Heading = 0; Fleet[I]->RestoreArticulation(0); Fleet[I]->RolledSide = 0;
            Fleet[I]->UpdatePose(Terrain);
        }
        Loader->Position = Center; Loader->Boom = LoaderRig::MinBoom; Loader->Curl = 0;
        Loader->UpdatePose(Terrain);
        Load Pile; Pile.Add(Material::Gravel, 5); Pile.Add(Material::Soil, 2);
        const FVector2D PilePosition = XY(Loader->ToolPoint()) + FVector2D(1.7,0);
        Terrain.Deposit(PilePosition, 1.3, Pile);
        const double Expected = Balance();
        const auto Step = [&](double Throttle, double Lift, double Tilt)
        {
            Loader->Drive(Dt, Throttle, 0, Throttle == 0, Terrain, Fleet);
            Loader->Work(Dt, Lift, Tilt, Terrain, Fleet);
            Terrain.AdvancePours(Dt); Terrain.AdvanceSoil(Dt);
            MaximumError = FMath::Max(MaximumError, FMath::Abs(Balance() - Expected));
        };
        for (int32 I = 0; I < 600 && Loader->Cargo.Volume() < 1.2; ++I) Step(0.42, 0, 0);
        Loader->Halt();
        const double Scooped = Loader->Cargo.Volume();
        // Roll back first, then reverse with the load and raise both hydraulic circuits together.
        for (int32 I = 0; I < 90; ++I) Step(0, 0, -1);
        const FVector2D BeforeReverse = Loader->Position;
        for (int32 I = 0; I < 120; ++I) Step(-0.35, 0.7, 0);
        Loader->Halt();
        const double Reversed = FVector2D::Distance(BeforeReverse, Loader->Position);
        for (int32 I = 0; I < 430; ++I) Step(0, 1, -0.3);
        const double Retained = Loader->Cargo.Volume();
        auto* Receiver = Fleet[ReceiverIndex]; Receiver->Heading = 90;
        Receiver->Position = Loader->Position + FVector2D(7.4,0); Receiver->UpdatePose(Terrain);
        Receiver->Position += Loader->Position + FVector2D(7.4,0) - XY(Receiver->BedLoadPoint());
        Receiver->UpdatePose(Terrain);
        const double TargetX = Receiver->BedLoadPoint().X - 3.9;
        const FVector2D BeforeHaul = Loader->Position;
        for (int32 I = 0; I < 1200 && Loader->Position.X < TargetX; ++I) Step(0.22,0,0);
        Loader->Halt();
        const double Travel = FVector2D::Distance(BeforeHaul, Loader->Position);
        const double BodyGap = Receiver->BedLoadPoint().X - Loader->Position.X;
        for (int32 I = 0; I < 300; ++I) Step(0,0,1);
        Terrain.SettlePours();
        const double Received = Receiver->Cargo.Volume();
        const bool Loaded = Scooped > 0.5 && FMath::Abs(Retained - Scooped) < 0.00001 &&
            Received > Retained * 0.98 && Loader->Cargo.Volume() < Epsilon &&
            Reversed > 0.5 && Travel > 2 && BodyGap > 3.65 && BodyGap < 4.15;
        const Load BeforeDrive = Receiver->Cargo;
        for (int32 I = 0; I < 120; ++I) Receiver->Drive(Dt, 0.4, 0, false, Terrain, Fleet);
        Receiver->Halt();
        bool KeptLoad = true;
        for (int32 M = 0; M < MaterialCount; ++M)
            KeptLoad &= FMath::Abs(BeforeDrive.Volumes[M] - Receiver->Cargo.Volumes[M]) < 0.000001;
        // Save/restore in-flight material uses the production terrain schema.
        Load Air; Air.Add(Material::Sand, 0.2);
        const FVector Bed = Receiver->BedLoadPoint();
        Terrain.Pour(Bed + FVector(0,0,3), FVector::ZeroVector, 0.6, Air);
        const double WithAir = Balance();
        const auto Saved = Terrain.SaveTerrain();
        Terrain.AdvancePours(0.1);
        const bool Restored = Terrain.RestoreTerrain(Saved) && FMath::Abs(Balance() - WithAir) < 0.00001;
        Terrain.SettlePours();
        const bool AirCaught = FMath::Abs(Receiver->Cargo.Volume() - Received - 0.2) < 0.00001;
        // A full bed keeps its load and routes excess back to the shared terrain solver.
        Receiver->Cargo.Add(Material::Gravel, Receiver->Capacity() - Receiver->Cargo.Volume());
        const double FullMass = Receiver->Cargo.Mass();
        Terrain.Pour(Receiver->BedLoadPoint() + FVector(0,0,3), FVector::ZeroVector, 0.6, Air);
        const double OverflowExpected = Balance();
        Terrain.SettlePours();
        const bool Overflow = FMath::Abs(Receiver->Cargo.Mass() - FullMass) < 0.00001 &&
            FMath::Abs(Balance() - OverflowExpected) < 0.00001;
        Passed &= Loaded && KeptLoad && Restored && AirCaught && Overflow;
        UE_LOG(LogTemp, Display, TEXT("Loader %s: scoop %.3f, retain %.3f, reverse %.2f, approach %.2f, gap %.2f, receive %.3f, cycle %d, retain %d, air-save %d/%d, overflow %d"),
            Receiver->Name(), Scooped, Retained, Reversed, Travel, BodyGap, Received, Loaded, KeptLoad, Restored, AirCaught, Overflow);
    }
    Passed &= MaximumError < 0.00001;
    UE_LOG(LogTemp, Display, TEXT("Loader acceptance: %s, maximum mass error %.9f t, patch variation %.3f m"),
        Passed ? TEXT("passed") : TEXT("failed"), MaximumError, Best);
    return Passed;
}
