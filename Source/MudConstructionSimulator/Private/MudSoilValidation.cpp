#include "SoilValidation.h"
#include "MudTerrain.h"
#include "MudMachine.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

using namespace Earthwork;

bool RunSoilValidation(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    if (!Terrain.IsSoilLab() || Fleet.Num() != 5) return false;
    const auto Original = Terrain.SaveTerrain();
    const auto Reset = [&]()
    {
        const bool Restored = Terrain.RestoreTerrain(Original);
        for (int32 I = 0; I < Fleet.Num(); ++I)
        {
            Fleet[I]->StopWork(); Fleet[I]->Cargo = {};
            Fleet[I]->Position = FVector2D(140, 730 + I * 8);
            Fleet[I]->Heading = 0; Fleet[I]->BladeYaw = 0;
            Fleet[I]->RolledSide = 0;
            Fleet[I]->UpdatePose(Terrain);
        }
        return Restored;
    };
    const auto Balance = [&]()
    {
        double Mass = Terrain.MaterialBalance();
        for (auto* Machine : Fleet) Mass += Machine->Cargo.Mass();
        return Mass;
    };
    const auto Settle = [&]()
    {
        Terrain.SettlePours();
        int32 Steps = 0;
        while (Terrain.ActiveSoilCells() && Steps++ < 12000) Terrain.AdvanceSoil(1.0 / 60);
        return Terrain.ActiveSoilCells() == 0;
    };
    bool Passed = true;
    double MaximumError = 0;
    const double Started = FPlatformTime::Seconds();
    for (const double Angle : {-25.0, 0.0, 25.0})
    {
        Passed &= Reset();
        Load Sand; Sand.Add(Material::Sand, 24);
        Terrain.Deposit(FVector2D(210, 760), 2.0, Sand);
        Passed &= Settle();
        const double Expected = Sand.Mass();
        auto* Dozer = Fleet[0];
        Dozer->Position = FVector2D(200, 760);
        Dozer->BladeYaw = Angle;
        Dozer->bGrade = false;
        Dozer->UpdatePose(Terrain);
        Dozer->ToggleTool();
        double PeakLoad = 0;
        for (int32 I = 0; I < 420; ++I)
        {
            Dozer->Drive(1.0 / 60, 0.7, 0, false, Terrain, Fleet);
            Dozer->Work(1.0 / 60, 0, 0, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
            PeakLoad = FMath::Max(PeakLoad, Dozer->Cargo.Volume());
            MaximumError = FMath::Max(MaximumError, FMath::Abs(Balance() - Expected));
        }
        const bool Pushed = PeakLoad > 0.25 && Dozer->Position.X > 201;
        Dozer->Halt();
        for (int32 I = 0; I < 180; ++I)
        {
            Dozer->Work(1.0 / 60, 1, 0, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        const bool Dropped = Dozer->Cargo.Volume() < 0.001;
        Passed &= Settle() && Pushed && Dropped;

        double Highest = -BIG_NUMBER;
        FVector2D Pile;
        for (int32 Y = 754; Y <= 766; ++Y)
            for (int32 X = 200; X <= 225; ++X)
            {
                const double Z = Terrain.Height(FVector2D(X,Y));
                if (Z > Highest) { Highest = Z; Pile = FVector2D(X,Y); }
            }
        auto* Loader = Fleet[4];
        Dozer->Position = FVector2D(145,740); Dozer->UpdatePose(Terrain);
        Loader->Position = Pile - FVector2D(6,0);
        Loader->Boom = -22; Loader->Curl = 0;
        Loader->UpdatePose(Terrain);
        const FVector Lip = Loader->ToolPoint();
        Loader->Position += Pile - FVector2D(Lip.X, Lip.Y) - FVector2D(1.6, 0);
        Loader->UpdatePose(Terrain);
        for (int32 I = 0; I < 300 && Loader->Cargo.Volume() < 0.5; ++I)
        {
            Loader->Drive(1.0 / 60, 0.5, 0, false, Terrain, Fleet);
            Loader->Work(1.0 / 60, 0, 0, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        const double Scooped = Loader->Cargo.Volume();
        Loader->Halt();
        for (int32 I = 0; I < 150; ++I)
        {
            Loader->Work(1.0 / 60, 1, -0.5, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        const FVector2D From = Loader->Position;
        for (int32 I = 0; I < 180; ++I)
        {
            Loader->Drive(1.0 / 60, 0.5, 0, false, Terrain, Fleet);
            Loader->Work(1.0 / 60, 0, 0, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        Loader->Halt();
        const FVector TipPosition = Loader->ToolPoint();
        for (int32 I = 0; I < 240; ++I)
        {
            Loader->Work(1.0 / 60, 0, 1, Terrain, Fleet);
            Terrain.AdvancePours(1.0 / 60);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        Passed &= Settle();
        const bool Tipped = Loader->Cargo.Volume() < 0.001;
        const double CarriedDistance = FVector2D::Distance(From, Loader->Position);
        const FVector2D NewPile(TipPosition.X, TipPosition.Y);
        Loader->Position = FVector2D(145,748); Loader->UpdatePose(Terrain);
        auto* Truck = Fleet[3];
        Truck->Position = NewPile - FVector2D(8, 0); Truck->Heading = 0;
        Truck->UpdatePose(Terrain);
        const double BeforeDrive = Terrain.Height(NewPile);
        TArray<double> BeforeRuts;
        for (int32 X = -5; X <= 5; ++X)
            for (int32 Y = -2; Y <= 2; ++Y)
                BeforeRuts.Add(Terrain.Height(NewPile + FVector2D(X,Y)));
        const FVector2D TruckStart = Truck->Position;
        for (int32 I = 0; I < 360; ++I)
        {
            Truck->Drive(1.0 / 60, 0.5, 0, false, Terrain, Fleet);
            Terrain.AdvanceSoil(1.0 / 60);
        }
        Truck->Halt();
        Passed &= Settle();
        const bool Driven = FVector2D::Distance(TruckStart, Truck->Position) > 1;
        double RutChange = 0;
        int32 SampleIndex = 0;
        for (int32 X = -5; X <= 5; ++X)
            for (int32 Y = -2; Y <= 2; ++Y)
                RutChange = FMath::Max(RutChange, FMath::Abs(Terrain.Height(NewPile + FVector2D(X,Y)) - BeforeRuts[SampleIndex++]));
        UE_LOG(LogTemp, Display, TEXT("Soil acceptance truck: travel %.2f m, pile height change %.5f m"),
            FVector2D::Distance(TruckStart, Truck->Position), Terrain.Height(NewPile) - BeforeDrive);
        MaximumError = FMath::Max(MaximumError, FMath::Abs(Balance() - Expected));
        const auto Saved = Terrain.SaveTerrain();
        struct FSample { FVector2D Position; double Height; Material Type; };
        TArray<FSample> Samples;
        for (const auto& Value : Saved->GetArrayField(TEXT("edits")))
        {
            const int32 Cell = Value->AsObject()->GetIntegerField(TEXT("cell"));
            const int32 Side = Terrain.GetWorldSize() + 1;
            const FVector2D Position(Cell % Side, Cell / Side);
            Samples.Add({Position, Terrain.Height(Position), Terrain.Surface(Position)});
        }
        const double SavedHeight = Terrain.Height(Pile), SavedMass = Balance();
        FString Json, ReadBack;
        const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SoilValidation.json");
        const bool Written = FJsonSerializer::Serialize(Saved, TJsonWriterFactory<>::Create(&Json)) &&
            FFileHelper::SaveStringToFile(Json, *SavePath) && FFileHelper::LoadFileToString(ReadBack, *SavePath);
        TSharedPtr<FJsonObject> Reloaded;
        const bool Parsed = Written && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReadBack), Reloaded);
        Load Extra; Extra.Add(Material::Clay, 1);
        Terrain.Deposit(Pile, 1, Extra);
        const bool Restored = Parsed && Terrain.RestoreTerrain(Reloaded) &&
            FMath::Abs(Terrain.Height(Pile) - SavedHeight) < 0.00001 && FMath::Abs(Balance() - SavedMass) < 0.00001;
        bool CellsRestored = Restored;
        for (const auto& Sample : Samples)
            CellsRestored &= FMath::Abs(Terrain.Height(Sample.Position) - Sample.Height) < 0.00001 &&
                Terrain.Surface(Sample.Position) == Sample.Type;
        Passed &= Settle();
        MaximumError = FMath::Max(MaximumError, FMath::Abs(Balance() - Expected));
        Passed &= Pushed && Dropped && Scooped > 0.2 && CarriedDistance > 1 && Tipped && Driven &&
            RutChange > 0.00001 && CellsRestored;
        UE_LOG(LogTemp, Display, TEXT("Soil acceptance angle %.0f: push %.3f m3 (%d), drop %d, loader %.3f m3, transport %.2f m, tip %d, restore %d"),
            Angle, PeakLoad, Pushed, Dropped, Scooped, CarriedDistance, Tipped, Restored);
    }
    Passed &= MaximumError < 0.00001;
    UE_LOG(LogTemp, Display, TEXT("Soil acceptance: %s, maximum mass error %.9f t, elapsed %.2f s"),
        Passed ? TEXT("passed") : TEXT("failed"), MaximumError, FPlatformTime::Seconds() - Started);
    Reset();
    return Passed;
}
