#include "MudGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "MudMachine.h"
#include "MudTerrain.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
using namespace Earthwork;
constexpr double DeliveryFraction = 0.75;
constexpr double FixedStep = 1.0 / 60.0;
constexpr int32 SaveVersion = 2;

FVector2D XY(const FVector& Point) { return FVector2D(Point.X, Point.Y); }
FString SavePath()
{
    return FString(FPlatformProcess::UserSettingsDir()) / TEXT("MudConstructionSimulator/Profiles/Default/Valley.json");
}

FString LoadPath()
{
    const FString Current = SavePath();
    return IFileManager::Get().FileExists(*Current) ? Current : FPaths::ProjectSavedDir() / TEXT("Valley.json");
}

const TCHAR* MaterialName(Material Type)
{
    switch (Type)
    {
    case Material::Gravel: return TEXT("Gravel");
    case Material::Rock: return TEXT("Crushed rock");
    default: return TEXT("Soil");
    }
}
}

AMudGameMode::AMudGameMode()
{
    DefaultPawnClass = AMudOperator::StaticClass();
    HUDClass = AMudHUD::StaticClass();
}

AMudOperator::AMudOperator()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("OperatorRoot"));
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetFieldOfView(80.f);
}

void AMudOperator::BeginPlay()
{
    Super::BeginPlay();
    const bool bSmoke = FParse::Param(FCommandLine::Get(), TEXT("MudSmokeTest"));
    Terrain = GetWorld()->SpawnActor<AMudTerrain>();
    if (!Terrain || !Terrain->Initialize())
    {
        bInitializeFailed = true;
        UE_LOG(LogTemp, Error, TEXT("Could not initialize the valley. Rebuild generated world assets with tools/build-windows.ps1."));
        if (bSmoke)
            FPlatformMisc::RequestExitWithStatus(false, 1);
        return;
    }
    OnFoot = XY(Terrain->Start);
    for (int32 Id = 0; Id < 4; ++Id)
    {
        auto* Machine = GetWorld()->SpawnActor<AMudMachine>();
        Machine->Initialize(static_cast<EMudMachine>(Id), OnFoot + FVector2D(0, 7.0 * Id - 10.5), *Terrain);
        Fleet.Add(Machine);
    }
    if (bSmoke)
    {
        const auto Before = Terrain->SaveTerrain();
        Earthwork::Load Gravel;
        Gravel.Add(Material::Gravel, 2.0);
        const FVector2D Start = XY(Terrain->Start);
        const double Initial = Terrain->Height(Start);
        Terrain->Deposit(Start, 1.2, Gravel);
        const bool bRaised = Terrain->Height(Start) > Initial && Terrain->Surface(Start) == Material::Gravel;
        const double Removed = Terrain->Excavate(Start, 1.2, 20, Initial).Volume();
        const bool bRestored = Terrain->RestoreTerrain(Before) && FMath::IsNearlyEqual(Terrain->Height(Start), Initial, 0.002);
        bool bPassed = Fleet.Num() == 4 && bRaised && bRestored && FMath::IsNearlyEqual(Removed, 2.0, 0.002);
        FString Report;
        if (FParse::Value(FCommandLine::Get(), TEXT("MudSmokeReport="), Report))
            bPassed = FFileHelper::SaveStringToFile(bPassed ? TEXT("passed") : TEXT("failed"), *Report) && bPassed;
        UE_LOG(LogTemp, Display, TEXT("Mud smoke test: %s"), bPassed ? TEXT("passed") : TEXT("failed"));
        FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
        return;
    }
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        FInputModeGameOnly Mode;
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = false;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("MudBenchmark")) && !Terrain->Fords.IsEmpty() && !Terrain->Piles.IsEmpty())
    {
        // Capture a representative workload at the southern crossing.
        Active = 2;
        auto* Dumper = GetMachine();
        const auto Ford = Terrain->Fords.Last();
        Dumper->Position = Ford + FVector2D(-9.5, 2);
        Dumper->Heading = 180;
        Dumper->UpdatePose(*Terrain);
        BenchmarkFrames.Reserve(9600);
        BenchmarkLast = BenchmarkStart = FPlatformTime::Seconds();
        bShowHelp = false;
    }
    Terrain->UpdateTiles(GetPosition());
    UpdateCamera();
}

AMudMachine* AMudOperator::GetMachine() const { return Fleet.IsValidIndex(Active) ? Fleet[Active] : nullptr; }

FVector2D AMudOperator::GetPosition() const
{
    if (const auto* Machine = GetMachine())
        return Machine->Position;
    return OnFoot;
}

void AMudOperator::Tick(float Dt)
{
    Super::Tick(Dt);
    TRACE_CPUPROFILER_EVENT_SCOPE(MudOperatorTick);
    FrameSeconds = FMath::Lerp(FrameSeconds > 0 ? FrameSeconds : Dt, Dt, 0.09f);
    MessageTime = FMath::Max(0.0, MessageTime - Dt);
    if (bInitializeFailed || !Terrain)
        return;
    StepBank = FMath::Min(0.12, StepBank + Dt);
    const bool bStepped = StepBank >= FixedStep;
    while (StepBank >= FixedStep)
    {
        Step(FixedStep);
        StepBank -= FixedStep;
    }
    if (bStepped)
        MouseHydraulicA = MouseHydraulicB = 0;
    const FVector2D Location = GetPosition();
    Terrain->UpdateTiles(Location);
    Terrain->UpdateSpills(FMath::Min(Dt, 0.1f));
    const int32 Width = Terrain->GetWorldSize();
    if (Location.X < 0 || Location.Y < 0 || Location.X > Width || Location.Y > Width)
        return;
    UpdateCamera();
    if (BenchmarkStart > 0)
        UpdateBenchmark();
}

void AMudOperator::Step(double Dt)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MudSimulationStep);
    auto* Machine = GetMachine();
    if (Machine)
    {
        if (Throttle < -0.1)
            bCruise = false;
        const double Command = bCruise && FMath::Abs(Throttle) < 0.1 ? Cruise : Throttle;
        Machine->Drive(Dt, Command, Steering, bBrake, *Terrain, Fleet);
        Machine->Work(Dt, FMath::Clamp(HydraulicA + MouseHydraulicA, -1.0, 1.0),
            FMath::Clamp(HydraulicB + MouseHydraulicB, -1.0, 1.0), *Terrain, Fleet);
    }
    else
    {
        const FVector2D Forward = XY(FRotator(0, LookYaw, 0).Vector());
        const FVector2D Right(-Forward.Y, Forward.X);
        FVector2D Walk = Forward * Throttle + Right * Steering;
        if (Walk.SizeSquared() > 1.0)
            Walk.Normalize();
        OnFoot += Walk * (Dt * 5.0);
        const double Max = Terrain->GetWorldSize() - 3;
        OnFoot.X = FMath::Clamp(OnFoot.X, 3.0, Max);
        OnFoot.Y = FMath::Clamp(OnFoot.Y, 3.0, Max);
    }

    // Parked machines continue tipping, and their chassis follow terrain edits underneath them.
    for (auto* Other : Fleet)
    {
        if (Other == Machine)
            continue;
        if (Other->bUnload)
            Other->Work(Dt, 0, 0, *Terrain, Fleet);
    }
    if (Machine && bDepotLoad)
    {
        if (FMath::Abs(Machine->Speed) > 0.35 || Machine->bUnload || Machine->RolledSide != 0)
            bDepotLoad = false;
        else
        {
            const FMudPile* Nearest = nullptr;
            double Best = TNumericLimits<double>::Max();
            for (const FMudPile& Pile : Terrain->Piles)
            {
                const double D = FVector2D::DistSquared(Pile.Center, Machine->Position);
                if (D < Best && D <= FMath::Square(Pile.Radius + 14))
                {
                    Nearest = &Pile;
                    Best = D;
                }
            }
            if (Nearest)
            {
                const double Available = Machine->Capacity() - Machine->Cargo.Volume();
                auto Load = Terrain->DrawFromPile(*Nearest, Machine->Position, FMath::Min(Available, Dt * 6.4));
                Machine->Cargo.Receive(Load);
                if (Load.Volume() < Epsilon)
                {
                    bDepotLoad = false;
                    Notify(Available < 0.01 ? TEXT("Bed full") : TEXT("Face exhausted. Reposition or use the excavator."));
                }
            }
            else
            {
                bDepotLoad = false;
                Notify(TEXT("Park close to a material stockpile."));
            }
        }
    }

    if (!bFinished)
    {
        auto* Truck = Fleet.Last();
        if (Truck->Cargo.Volume() < Truck->Capacity() * DeliveryFraction)
            bDelivery = false;
        else if (FVector2D::DistSquared(Truck->Position, XY(Terrain->Start)) < FMath::Square(22.0))
            bDelivery = true;
        if (bDelivery && Truck->RolledSide == 0 &&
            FVector2D::DistSquared(Truck->Position, XY(Terrain->Goal)) < FMath::Square(12.0))
        {
            bFinished = true;
            Notify(TEXT("Delivery complete. The valley road is open."));
            Save();
        }
    }
}

void AMudOperator::UpdateCamera()
{
    const auto* Machine = GetMachine();
    const FVector2D Position = GetPosition();
    FVector Focus(Position.X * 100, Position.Y * 100, (Terrain->Height(Position) + 2.1) * 100);
    const double VehicleYaw = Machine ? Machine->Heading : 0;
    const double CabYaw = Machine && Machine->Kind == EMudMachine::Excavator ? Machine->Swing : 0;
    const double Yaw = VehicleYaw + LookYaw + (bCab ? CabYaw : 0.0);
    const FRotator Rotation(LookPitch, Yaw, 0);
    FVector View = Focus;
    if (Machine && bCab)
        View = Machine->CameraSeat();
    else if (Machine)
    {
        View -= Rotation.Vector() * Distance * 100;
        for (int32 I = 1; I < 10; ++I)
        {
            const FVector Sample = FMath::Lerp(Focus, View, I / 10.0);
            if (Sample.Z < (Terrain->Height(XY(Sample) / 100.0) + 0.40) * 100)
            {
                View = FMath::Lerp(Focus, View, (I - 1) / 10.0);
                break;
            }
        }
    }
    const double MinimumZ = (Terrain->Height(XY(View) / 100.0) + 0.65) * 100;
    View.Z = FMath::Max(View.Z, MinimumZ);
    Camera->SetWorldLocationAndRotation(View, Rotation);
}

void AMudOperator::UpdateBenchmark()
{
    const double Current = FPlatformTime::Seconds();
    const double Elapsed = Current - BenchmarkStart;
    if (Elapsed > 3.0)
        BenchmarkFrames.Add((Current - BenchmarkLast) * 1000.0);
    BenchmarkLast = Current;
    auto* Dumper = Fleet[2];
    if (Dumper->Cargo.Volume() < 0.05)
    {
        const FVector Position = Dumper->ToolPoint();
        const FMudPile& Pile = Terrain->Piles[0];
        auto Cargo = Terrain->Excavate(XY(Position), 2.35, Dumper->Capacity(), Position.Z - 2.0);
        Cargo.Receive(Terrain->DrawFromPile(Pile, Pile.Center, Dumper->Capacity() - Cargo.Volume()));
        Dumper->Cargo = Cargo;
        Dumper->bUnload = true;
    }
    if (Elapsed < 33.0)
        return;
    BenchmarkStart = 0;
    if (BenchmarkFrames.IsEmpty())
        return;
    FString Csv = TEXT("frame,ms\n");
    double Sum = 0;
    for (int32 I = 0; I < BenchmarkFrames.Num(); ++I)
    {
        Sum += BenchmarkFrames[I];
        Csv += FString::Printf(TEXT("%d,%.4f\n"), I + 1, BenchmarkFrames[I]);
    }
    BenchmarkFrames.Sort();
    const double P99 = BenchmarkFrames[FMath::CeilToInt(BenchmarkFrames.Num() * 0.99) - 1];
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Benchmark.csv");
    const FString Summary = FString::Printf(TEXT("Benchmark: %.1f FPS average | %.2f ms p99"),
        1000 * BenchmarkFrames.Num() / FMath::Max(1.0, Sum), P99);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const bool bSaved = FFileHelper::SaveStringToFile(Csv, *Path);
    UE_LOG(LogTemp, Display, TEXT("%s | %s"), *Summary, bSaved ? *Path : TEXT("CSV write failed"));
    Notify(Summary);
}

void AMudOperator::MoveMouseX(float Value)
{
    if (bMouseHydraulics)
        MouseHydraulicB = FMath::Clamp(MouseHydraulicB + Value * 0.20, -1.0, 1.0);
    else
        LookYaw = FRotator::NormalizeAxis(LookYaw + Value * 0.23);
}

void AMudOperator::MoveMouseY(float Value)
{
    if (bMouseHydraulics)
        MouseHydraulicA = FMath::Clamp(MouseHydraulicA + Value * 0.20, -1.0, 1.0);
    else
        LookPitch = FMath::Clamp(LookPitch + Value * 0.23, -76.0, 34.0);
}

void AMudOperator::MovePadX(float Value)
{
    if (bPadHydraulics)
        MouseHydraulicB = Value;
    else if (GetWorld())
        LookYaw = FRotator::NormalizeAxis(LookYaw + Value * GetWorld()->GetDeltaSeconds() * 96);
}

void AMudOperator::MovePadY(float Value)
{
    if (bPadHydraulics)
        MouseHydraulicA = Value;
    else if (GetWorld())
        LookPitch = FMath::Clamp(LookPitch + Value * GetWorld()->GetDeltaSeconds() * 72, -76.0, 34.0);
}

void AMudOperator::Zoom(float Value) { Distance = FMath::Clamp(Distance - Value * 1.4, 5.0, 33.0); }

void AMudOperator::EnterExit()
{
    if (!Terrain || bInitializeFailed)
        return;
    if (auto* Machine = GetMachine())
    {
        Machine->Speed = 0;
        OnFoot = Machine->Position + FVector2D(0, -2.7);
        LookYaw = FRotator::NormalizeAxis(LookYaw + Machine->Heading);
        LookPitch = -5;
        Active = INDEX_NONE;
        bDepotLoad = bCruise = false;
        return;
    }
    double Best = FMath::Square(6.0);
    for (int32 Id = 0; Id < Fleet.Num(); ++Id)
    {
        const double Dist = FVector2D::DistSquared(OnFoot, Fleet[Id]->Position);
        if (Dist < Best)
        {
            Active = Id;
            Best = Dist;
        }
    }
    if (auto* Machine = GetMachine())
    {
        LookYaw = FRotator::NormalizeAxis(LookYaw - Machine->Heading);
        LookPitch = -21;
    }
}

void AMudOperator::SwapVehicle()
{
    if (Fleet.IsEmpty())
        return;
    if (auto* Old = GetMachine())
        Old->Speed = 0;
    Active = (Active + 1) % Fleet.Num();
    LookYaw = -35;
    LookPitch = -21;
    bDepotLoad = bCruise = false;
}

void AMudOperator::WorkTool() { if (auto* Machine = GetMachine()) Machine->ToggleTool(); }
void AMudOperator::ChangeGroup() { if (auto* Machine = GetMachine()) Machine->ChangeGroup(); }
void AMudOperator::Unload()
{
    if (auto* Machine = GetMachine())
    {
        Machine->ToggleUnload();
        bDepotLoad = false;
    }
}

void AMudOperator::Recover()
{
    if (auto* Machine = GetMachine())
    {
        Machine->Position = XY(Terrain->Start) + FVector2D(0, 7.0 * Active - 10.5);
        Machine->Speed = 0;
        Machine->Heading = 0;
        Machine->RolledSide = 0;
        Machine->bUnload = false;
        bDepotLoad = bCruise = false;
        Machine->UpdatePose(*Terrain);
        Notify(TEXT("Recovered to the start yard."));
    }
}

void AMudOperator::Service()
{
    const auto* Machine = GetMachine();
    if (!Machine || (Machine->Kind != EMudMachine::Truck && Machine->Kind != EMudMachine::Dumper))
        return;
    bDepotLoad = !bDepotLoad;
    Notify(bDepotLoad ? TEXT("Stockpile loading active. Keep the vehicle parked.") : TEXT("Stockpile loading stopped."));
}

void AMudOperator::Notify(const FString& Text)
{
    Message = Text;
    MessageTime = 4;
}

FString AMudOperator::MissionText() const
{
    if (bInitializeFailed)
        return TEXT("The valley could not be loaded. Rebuild the project assets.");
    if (bFinished)
        return TEXT("Delivery complete. Keep working the valley.");
    if (bDelivery)
        return TEXT("Deliver the loaded truck to the red marker.");
    return TEXT("Prepare a crossing. Bring the truck to the yellow yard with at least 18 m3.");
}

FString AMudOperator::SurveyText() const
{
    if (!Terrain || bInitializeFailed)
        return {};
    const FVector2D Position = GetPosition();
    const double GradeX = (Terrain->Height(Position + FVector2D(2, 0)) - Terrain->Height(Position - FVector2D(2, 0))) / 4;
    const double GradeY = (Terrain->Height(Position + FVector2D(0, 2)) - Terrain->Height(Position - FVector2D(0, 2))) / 4;
    FString Info = FString::Printf(TEXT("%s   |   Slope %.0f%%   |   Water %.2f m   |   Goal %.0f m"),
        MaterialName(Terrain->Surface(Position)), FVector2D(GradeX, GradeY).Size() * 100,
        Terrain->WaterDepth(Position), FVector2D::Distance(Position, XY(Terrain->Goal)));
    if (bShowMap)
    {
        for (const FVector2D Ford : Terrain->Fords)
        {
            const double Fill = Terrain->EstimateFordFill(Ford, 0.48);
            Info += FString::Printf(TEXT("   |   %.0f m: ~%.0f m3 fill"), FVector2D::Distance(Position, Ford), Fill);
        }
    }
    return Info;
}

FString AMudOperator::StatusText() const { return MessageTime > 0 ? Message : FString(); }

void AMudOperator::Save()
{
    if (!Terrain || bInitializeFailed)
        return;
    const auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), SaveVersion);
    Root->SetNumberField(TEXT("seed"), Terrain->GetSeed());
    Root->SetNumberField(TEXT("active"), Active);
    Root->SetNumberField(TEXT("footX"), OnFoot.X);
    Root->SetNumberField(TEXT("footY"), OnFoot.Y);
    Root->SetBoolField(TEXT("delivery"), bDelivery);
    Root->SetBoolField(TEXT("complete"), bFinished);
    Root->SetObjectField(TEXT("terrain"), Terrain->SaveTerrain());
    TArray<TSharedPtr<FJsonValue>> Vehicles;
    for (const auto* Machine : Fleet)
    {
        const auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("x"), Machine->Position.X);
        Row->SetNumberField(TEXT("y"), Machine->Position.Y);
        Row->SetNumberField(TEXT("heading"), Machine->Heading);
        Row->SetNumberField(TEXT("boom"), Machine->Boom);
        Row->SetNumberField(TEXT("stick"), Machine->Stick);
        Row->SetNumberField(TEXT("curl"), Machine->Curl);
        Row->SetNumberField(TEXT("swing"), Machine->Swing);
        Row->SetNumberField(TEXT("blade"), Machine->Blade);
        Row->SetNumberField(TEXT("rolled"), Machine->RolledSide);
        TArray<TSharedPtr<FJsonValue>> Cargo;
        for (const double Volume : Machine->Cargo.Volumes)
            Cargo.Add(MakeShared<FJsonValueNumber>(Volume));
        Row->SetArrayField(TEXT("cargo"), Cargo);
        Vehicles.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("vehicles"), Vehicles);
    FString Json;
    const FString Path = SavePath(), Temporary = Path + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const auto Writer = TJsonWriterFactory<>::Create(&Json);
    if (FJsonSerializer::Serialize(Root, Writer, true) &&
        FFileHelper::SaveStringToFile(Json, *Temporary) &&
        IFileManager::Get().Move(*Path, *Temporary, true, false, false, true))
        Notify(bFinished ? TEXT("Delivery complete. Progress saved.") : TEXT("Progress saved."));
    else
        Notify(TEXT("Could not save progress."));
}

void AMudOperator::Load()
{
    if (!Terrain || bInitializeFailed)
        return;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    int32 Version = 0, Seed = 0, Chosen = INDEX_NONE;
    const TArray<TSharedPtr<FJsonValue>>* Vehicles = nullptr;
    const TSharedPtr<FJsonObject>* TerrainData = nullptr;
    if (!FFileHelper::LoadFileToString(Json, *LoadPath()) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() ||
        !Root->TryGetNumberField(TEXT("version"), Version) || Version != SaveVersion ||
        !Root->TryGetNumberField(TEXT("seed"), Seed) || Seed != Terrain->GetSeed() ||
        !Root->TryGetNumberField(TEXT("active"), Chosen) || (Chosen != INDEX_NONE && !Fleet.IsValidIndex(Chosen)) ||
        !Root->TryGetArrayField(TEXT("vehicles"), Vehicles) || Vehicles->Num() != Fleet.Num() ||
        !Root->TryGetObjectField(TEXT("terrain"), TerrainData))
    {
        Notify(TEXT("No compatible save found."));
        return;
    }
    struct FRestore
    {
        FVector2D XY;
        double Heading, Boom, Stick, Curl, Swing, Blade;
        int32 RolledSide;
        Earthwork::Load Cargo;
    };
    TArray<FRestore> Machines;
    const auto ValidPosition = [this](FVector2D Point)
    {
        return Point.X >= 0 && Point.Y >= 0 && Point.X <= Terrain->GetWorldSize() && Point.Y <= Terrain->GetWorldSize();
    };
    for (int32 Id = 0; Id < Fleet.Num(); ++Id)
    {
        const TSharedPtr<FJsonObject>* Row;
        const TArray<TSharedPtr<FJsonValue>>* Cargo;
        FRestore Entry{};
        if (!(*Vehicles)[Id]->TryGetObject(Row) ||
            !(*Row)->TryGetNumberField(TEXT("x"), Entry.XY.X) || !(*Row)->TryGetNumberField(TEXT("y"), Entry.XY.Y) ||
            !ValidPosition(Entry.XY) || !(*Row)->TryGetNumberField(TEXT("heading"), Entry.Heading) ||
            !(*Row)->TryGetNumberField(TEXT("boom"), Entry.Boom) || !(*Row)->TryGetNumberField(TEXT("stick"), Entry.Stick) ||
            !(*Row)->TryGetNumberField(TEXT("curl"), Entry.Curl) || !(*Row)->TryGetNumberField(TEXT("swing"), Entry.Swing) ||
            !(*Row)->TryGetNumberField(TEXT("blade"), Entry.Blade) ||
            !(*Row)->TryGetNumberField(TEXT("rolled"), Entry.RolledSide) || FMath::Abs(Entry.RolledSide) > 1 ||
            !(*Row)->TryGetArrayField(TEXT("cargo"), Cargo) || Cargo->Num() != MaterialCount)
        {
            Notify(TEXT("Vehicle data in the save is invalid."));
            return;
        }
        for (size_t M = 0; M < MaterialCount; ++M)
        {
            if (!(*Cargo)[M]->TryGetNumber(Entry.Cargo.Volumes[M]) ||
                !FMath::IsFinite(Entry.Cargo.Volumes[M]) || Entry.Cargo.Volumes[M] < 0 ||
                Entry.Cargo.Volumes[M] > Fleet[Id]->Capacity())
            {
                Notify(TEXT("Invalid cargo in the save."));
                return;
            }
        }
        if (Entry.Cargo.Volume() > Fleet[Id]->Capacity() + Epsilon)
        {
            Notify(TEXT("Cargo exceeds vehicle capacity."));
            return;
        }
        Machines.Add(Entry);
    }
    FVector2D Foot;
    bool bSavedDelivery, bSavedComplete;
    if (!Root->TryGetNumberField(TEXT("footX"), Foot.X) || !Root->TryGetNumberField(TEXT("footY"), Foot.Y) ||
        !ValidPosition(Foot) || !Root->TryGetBoolField(TEXT("delivery"), bSavedDelivery) ||
        !Root->TryGetBoolField(TEXT("complete"), bSavedComplete) || !Terrain->RestoreTerrain(*TerrainData))
    {
        Notify(TEXT("Terrain data in the save is invalid."));
        return;
    }
    for (int32 Id = 0; Id < Machines.Num(); ++Id)
    {
        const FRestore& Entry = Machines[Id];
        auto* Machine = Fleet[Id];
        Machine->Position = Entry.XY;
        Machine->Heading = Entry.Heading;
        Machine->Boom = Entry.Boom;
        Machine->Stick = Entry.Stick;
        Machine->Curl = Entry.Curl;
        Machine->Swing = Entry.Swing;
        Machine->Blade = Entry.Blade;
        Machine->RolledSide = Entry.RolledSide;
        Machine->Cargo = Entry.Cargo;
        Machine->Speed = 0;
        Machine->bTool = Machine->bUnload = Machine->bOtherGroup = Machine->bGrade = false;
        Machine->UpdatePose(*Terrain);
    }
    Active = Chosen;
    OnFoot = Foot;
    bDelivery = bSavedDelivery;
    bFinished = bSavedComplete;
    bDepotLoad = bCruise = false;
    Notify(TEXT("Progress restored."));
}

void AMudOperator::PerformanceMode()
{
    auto* Settings = GEngine->GetGameUserSettings();
    Settings->SetOverallScalabilityLevel(1);
    Settings->SetResolutionScaleValueEx(66.67f);
    Settings->SetFrameRateLimit(120);
    Settings->SetVSyncEnabled(false);
    Settings->ApplySettings(false);
    Notify(TEXT("Performance: TSR at 67% render scale, 120 fps limit."));
}

void AMudOperator::QualityMode()
{
    auto* Settings = GEngine->GetGameUserSettings();
    Settings->SetOverallScalabilityLevel(2);
    Settings->SetResolutionScaleValueEx(100);
    Settings->SetFrameRateLimit(120);
    Settings->SetVSyncEnabled(false);
    Settings->ApplySettings(false);
    Notify(TEXT("Quality: 100% render scale, 120 fps limit."));
}

void AMudOperator::ToggleFullscreen()
{
    auto* Settings = GEngine->GetGameUserSettings();
    const bool bFullscreen = Settings->GetFullscreenMode() != EWindowMode::Windowed;
    Settings->SetFullscreenMode(bFullscreen ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
    Settings->ApplySettings(false);
    Notify(bFullscreen ? TEXT("Windowed") : TEXT("Fullscreen at desktop resolution"));
}

void AMudOperator::Quit() { UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false); }

void AMudOperator::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("Throttle"), this, &AMudOperator::SetThrottle);
    Input->BindAxis(TEXT("Steering"), this, &AMudOperator::SetSteering);
    Input->BindAxis(TEXT("LookX"), this, &AMudOperator::MoveMouseX);
    Input->BindAxis(TEXT("LookY"), this, &AMudOperator::MoveMouseY);
    Input->BindAxis(TEXT("PadX"), this, &AMudOperator::MovePadX);
    Input->BindAxis(TEXT("PadY"), this, &AMudOperator::MovePadY);
    Input->BindAxis(TEXT("HydraulicA"), this, &AMudOperator::SetHydraulicA);
    Input->BindAxis(TEXT("HydraulicB"), this, &AMudOperator::SetHydraulicB);
    Input->BindAxis(TEXT("Zoom"), this, &AMudOperator::Zoom);
    Input->BindAction(TEXT("EnterExit"), IE_Pressed, this, &AMudOperator::EnterExit);
    Input->BindAction(TEXT("Swap"), IE_Pressed, this, &AMudOperator::SwapVehicle);
    Input->BindAction(TEXT("Camera"), IE_Pressed, this, &AMudOperator::SwitchCamera);
    Input->BindAction(TEXT("Map"), IE_Pressed, this, &AMudOperator::SwitchMap);
    Input->BindAction(TEXT("Help"), IE_Pressed, this, &AMudOperator::SwitchHelp);
    Input->BindAction(TEXT("MouseHydraulics"), IE_Pressed, this, &AMudOperator::MouseHydraulicsOn);
    Input->BindAction(TEXT("MouseHydraulics"), IE_Released, this, &AMudOperator::MouseHydraulicsOff);
    Input->BindAction(TEXT("PadHydraulics"), IE_Pressed, this, &AMudOperator::PadHydraulicsOn);
    Input->BindAction(TEXT("PadHydraulics"), IE_Released, this, &AMudOperator::PadHydraulicsOff);
    Input->BindAction(TEXT("Brake"), IE_Pressed, this, &AMudOperator::BrakeOn);
    Input->BindAction(TEXT("Brake"), IE_Released, this, &AMudOperator::BrakeOff);
    Input->BindAction(TEXT("Cruise"), IE_Pressed, this, &AMudOperator::CruiseToggle);
    Input->BindAction(TEXT("CruiseUp"), IE_Pressed, this, &AMudOperator::CruiseUp);
    Input->BindAction(TEXT("CruiseDown"), IE_Pressed, this, &AMudOperator::CruiseDown);
    Input->BindAction(TEXT("Tool"), IE_Pressed, this, &AMudOperator::WorkTool);
    Input->BindAction(TEXT("Group"), IE_Pressed, this, &AMudOperator::ChangeGroup);
    Input->BindAction(TEXT("Unload"), IE_Pressed, this, &AMudOperator::Unload);
    Input->BindAction(TEXT("Recover"), IE_Pressed, this, &AMudOperator::Recover);
    Input->BindAction(TEXT("Service"), IE_Pressed, this, &AMudOperator::Service);
    Input->BindAction(TEXT("Save"), IE_Pressed, this, &AMudOperator::Save);
    Input->BindAction(TEXT("Load"), IE_Pressed, this, &AMudOperator::Load);
    Input->BindAction(TEXT("Performance"), IE_Pressed, this, &AMudOperator::PerformanceMode);
    Input->BindAction(TEXT("Quality"), IE_Pressed, this, &AMudOperator::QualityMode);
    Input->BindAction(TEXT("Fullscreen"), IE_Pressed, this, &AMudOperator::ToggleFullscreen);
    Input->BindAction(TEXT("Quit"), IE_Pressed, this, &AMudOperator::Quit);
}

void AMudHUD::DrawHUD()
{
    Super::DrawHUD();
    const auto* Operator = Cast<AMudOperator>(GetOwningPawn());
    if (!Operator || !Canvas)
        return;
    const auto* Terrain = Operator->GetTerrain();
    const auto* Machine = Operator->GetMachine();
    const float Scale = FMath::Clamp(Canvas->SizeY / 870.f, 0.75f, 2.4f);
    const float W = Canvas->SizeX / Scale, H = Canvas->SizeY / Scale;
    const FLinearColor Panel(0.026f, 0.038f, 0.037f, 0.93f);
    const FLinearColor Text(0.91f, 0.94f, 0.89f);
    const FLinearColor Muted(0.65f, 0.74f, 0.69f);
    const FLinearColor Gold(1.0f, 0.74f, 0.30f);
    const FLinearColor Red(0.96f, 0.38f, 0.27f);
    const auto Rect = [this, Scale](const FLinearColor& Color, float X, float Y, float Width, float Height)
    {
        DrawRect(Color, X * Scale, Y * Scale, Width * Scale, Height * Scale);
    };
    const auto Label = [this, Scale](const FString& Value, const FLinearColor& Color, float X, float Y, float Size = 1.0f)
    {
        DrawText(Value, Color, X * Scale, Y * Scale, GEngine->GetSmallFont(), Scale * Size, false);
    };

    Rect(Panel, 22, 22, 600, 80);
    Rect(Gold, 22, 22, 3, 80);
    Label(TEXT("VALLEY ROAD  /  DELIVERY CONTRACT"), Gold, 38, 33, 1.15);
    Label(Operator->MissionText(), Text, 38, 61);
    Label(Operator->SurveyText(), Muted, 38, 81, 0.90);
    if (!Machine || !Terrain)
        return;

    Rect(Panel, 22, H - 135, 305, 113);
    Label(Machine->Name(), Text, 38, H - 124, 1.24);
    Label(Machine->ToolDescription(), Muted, 38, H - 99);
    Label(FString::Printf(TEXT("%02.0f km/h   |   %02.0f%% grip   |   %4.1f t payload"),
        FMath::Abs(Machine->Speed) * 3.6, Machine->Grip * 100, Machine->Cargo.Mass()), Text, 38, H - 76);
    Rect(FLinearColor(0.13, 0.19, 0.17), 38, H - 48, 138, 6);
    Rect(Gold, 38, H - 48, 138 * Machine->Cargo.Volume() / Machine->Capacity(), 6);
    Label(FString::Printf(TEXT("%.1f / %.0f m3"), Machine->Cargo.Volume(), Machine->Capacity()), Text, 190, H - 55);
    const auto Status = Operator->StatusText();
    if (!Status.IsEmpty())
    {
        Rect(Panel, 22, H - 181, 495, 32);
        Label(Status, Gold, 36, H - 173);
    }

    const float FrameMs = Operator->FrameSeconds * 1000;
    Rect(Panel, W - 188, 22, 166, 30);
    Label(FString::Printf(TEXT("%.0f FPS   /   %.2f ms"), 1000 / FMath::Max(FrameMs, 0.01f), FrameMs), Muted, W - 174, 29);
    if (Operator->bShowHelp)
    {
        Rect(Panel, W - 368, H - 156, 346, 134);
        Label(TEXT("WASD  Drive     E  Enter / exit     Tab  Vehicle"), Text, W - 353, H - 143);
        Label(TEXT("J/K/N/M  Hydraulics     G  Function group"), Text, W - 353, H - 122);
        Label(TEXT("V  Work tool     I  Tip     R  Stockpile service"), Text, W - 353, H - 101);
        Label(TEXT("C  Camera     9  Survey     Backspace  Recover"), Text, W - 353, H - 80);
        Label(TEXT("F5 / F9  Save / load    F7 / F8  Graphics"), Text, W - 353, H - 59);
        Label(TEXT("F1  Controls     F6  Fullscreen     Esc  Quit"), Muted, W - 353, H - 39);
    }

    const float MapSize = Operator->bShowMap ? FMath::Min(500.f, H - 230) : 180.f;
    const float MapX = W - MapSize - 22, MapY = 67;
    Rect(Panel, MapX - 2, MapY - 2, MapSize + 4, MapSize + 4);
    if (auto* Overview = Terrain->GetOverview())
        DrawTextureSimple(Overview, MapX * Scale, MapY * Scale, MapSize * Scale / Overview->GetSizeX(), false);
    const auto MapPoint = [&](FVector2D Position, const FLinearColor& Color, float Size)
    {
        Position = Position * (MapSize / Terrain->GetWorldSize()) + FVector2D(MapX, MapY);
        Rect(Color, Position.X - Size * 0.5, Position.Y - Size * 0.5, Size, Size);
        return Position;
    };
    MapPoint(XY(Terrain->Start), Gold, 6);
    MapPoint(XY(Terrain->Goal), Red, 6);
    for (const auto Ford : Terrain->Fords)
        MapPoint(Ford, FLinearColor(0.61, 0.85, 1), 4);
    for (const auto* Vehicle : Operator->GetFleet())
        MapPoint(Vehicle->Position, Vehicle == Machine ? FLinearColor::White : Gold, Vehicle == Machine ? 6 : 3);
    if (Operator->bShowMap)
    {
        for (const auto& Pile : Terrain->Piles)
        {
            const FVector2D Point = MapPoint(Pile.Center, Text, 5);
            Label(FString::Printf(TEXT("%s %.0f m3"), MaterialName(Pile.Material), Terrain->Remaining(Pile)),
                Text, Point.X + 5, Point.Y - 8);
        }
    }
}
