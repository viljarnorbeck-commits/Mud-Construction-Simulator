#include "MudGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "MudAudio.h"
#include "MudMachine.h"
#include "MudTerrain.h"
#include "SoilValidation.h"
#include "MudWorldValidation.h"
#include "LoaderValidation.h"
#include "LoaderRig.h"
#include "MudMaterialReview.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UnrealClient.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
using namespace Earthwork;
constexpr double DeliveryFraction = 0.75;
constexpr double FixedStep = 1.0 / 60.0;
const FVector2D ParkingOffsets[] = {{-5, -10.5}, {-4, -3.5}, {7, 10.5}, {8, 18}, {-10, 12}};
constexpr int32 LegacyVehicleCount = static_cast<int32>(EMudMachine::Loader);
constexpr int32 SaveVersion = 3;
FString ProfileWorldId=TEXT("Default");
constexpr double AutosaveInterval = 5 * 60;

const TCHAR* ArrowHelp(const AMudMachine& Machine)
{
    switch (Machine.Kind)
    {
    case EMudMachine::Loader: return TEXT("Up/Down Lift    Left/Right Roll back / tip");
    case EMudMachine::Dozer: return TEXT("Up/Down Blade    Both mouse buttons + sideways: Tilt    V Grade hold    G Mode");
    case EMudMachine::Excavator: return Machine.bOtherGroup ? TEXT("Arrows: bucket / swing   G: change group") :
        TEXT("Arrows: boom / stick   G: change group");
    default: return TEXT("Up/Down Raise / lower bed    I Auto tip");
    }
}

FVector2D XY(const FVector& Point) { return FVector2D(Point.X, Point.Y); }
FString WorldProfile()
{
    return ProfileWorldId;
}
FString SavePath()
{
    // An isolated Windows preview can use a host-mapped profile directory.
    FString ProfileDirectory;
    if (FParse::Value(FCommandLine::Get(), TEXT("MudProfileDir="), ProfileDirectory) && !ProfileDirectory.IsEmpty())
        return FPaths::ConvertRelativePathToFull(ProfileDirectory) / TEXT("Valley.json");
    FString Profile=WorldProfile();
    if(FParse::Param(FCommandLine::Get(),TEXT("MudSoilLab")))Profile=Profile==TEXT("Default")?TEXT("SoilLab"):Profile+TEXT("-SoilLab");
    return FString(FPlatformProcess::UserSettingsDir())/TEXT("MudConstructionSimulator/Profiles")/Profile/TEXT("Valley.json");
}

FString LoadPath()
{
    const FString Current = SavePath();
    return WorldProfile()!=TEXT("Default")||IFileManager::Get().FileExists(*Current) ? Current : FPaths::ProjectSavedDir() / TEXT("Valley.json");
}

const TCHAR* MaterialName(Material Type)
{
    switch (Type)
    {
    case Material::Gravel: return TEXT("Gravel");
    case Material::Rock: return TEXT("Crushed rock");
    case Material::Sand: return TEXT("Sand");
    case Material::Clay: return TEXT("Clay");
    default: return TEXT("Soil");
    }
}

bool TestVehicleSwitch(AMudOperator& Operator, APlayerController& Controller)
{
    const auto& Fleet = Operator.GetFleet();
    auto* PlayerInput = NewObject<UPlayerInput>(&Controller);
    auto* Bindings = NewObject<UInputComponent>(&Operator);
    Operator.SetupPlayerInputComponent(Bindings);
    const TArray<UInputComponent*> Stack{Bindings};
    const auto SendTab = [&](EInputEvent Event)
    {
        PlayerInput->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::Tab, Event, FPlatformTime::Cycles64()));
        PlayerInput->ProcessInputStack(Stack, FixedStep, false);
    };
    int32 Expected = Fleet.IndexOfByKey(Operator.GetMachine());
    for (int32 Step = 0; Step < Fleet.Num() + 1; ++Step)
    {
        Expected = (Expected + 1) % Fleet.Num();
        SendTab(IE_Pressed);
        if (Operator.GetMachine() != Fleet[Expected])
            return false;
        SendTab(IE_Repeat);
        SendTab(IE_Released);
        if (Operator.GetMachine() != Fleet[Expected])
            return false;
    }
    return true;
}

bool TestTrackMarks(AMudTerrain& Terrain)
{
    const FVector2D Start = XY(Terrain.Start) + FVector2D(14, -19);
    const FVector2D Center(FMath::RoundToInt(Start.X), FMath::RoundToInt(Start.Y));
    const auto InitialTerrain = Terrain.SaveTerrain();
    // Compaction lowers occupied volume without removing mass.
    const auto GroundMass = [&]() { return Terrain.MaterialBalance(); };
    const double Before = GroundMass();
    const double StartHeight = Terrain.Height(Center);
    const auto Stamp = [&](double Scrub)
    {
        for (int32 I = 0; I < 6; ++I)
            Terrain.PressTrack(Center, {1, 0}, 1.70, 0.79, 1, Scrub, true);
        return StartHeight - Terrain.Height(Center);
    };
    const double StraightDepth = Stamp(0);
    const bool bStraight = StraightDepth > 0.005 && FMath::IsNearlyEqual(Before, GroundMass(), 0.00001);
    const bool bReset = Terrain.RestoreTerrain(InitialTerrain);
    const double TurnDepth = Stamp(1);
    const bool bTurn = TurnDepth > StraightDepth + 0.025 && FMath::IsNearlyEqual(Before, GroundMass(), 0.00001);
    const auto WithTracks = Terrain.SaveTerrain();
    const double TrackCount = WithTracks->GetArrayField(TEXT("trackWear")).Num();
    const bool bRoundTrip = Terrain.RestoreTerrain(InitialTerrain) && Terrain.RestoreTerrain(WithTracks) &&
        FMath::IsNearlyEqual(StartHeight - Terrain.Height(Center), TurnDepth, 0.001) &&
        TrackCount > 0 && TrackCount == Terrain.SaveTerrain()->GetArrayField(TEXT("trackWear")).Num();
    InitialTerrain->RemoveField(TEXT("trackWear"));
    const bool bLegacy = Terrain.RestoreTerrain(InitialTerrain) &&
        FMath::IsNearlyEqual(Terrain.Height(Center), StartHeight, 0.001);
    UE_LOG(LogTemp, Display, TEXT("Track marks: straight %.3f m (%d), pivot %.3f m (%d), save %d, old save %d"),
        StraightDepth, bStraight, TurnDepth, bTurn, bRoundTrip, bLegacy);
    return bStraight && bTurn && bReset && bRoundTrip && bLegacy;
}

bool TestFallingLoad(AMudTerrain& Terrain)
{
    const auto Baseline = Terrain.SaveTerrain();
    const FVector2D Start = XY(Terrain.Start);
    const FVector2D Center(FMath::RoundToInt(Start.X), FMath::RoundToInt(Start.Y));
    TArray<double> Heights;
    for (int32 Y = -6; Y <= 6; ++Y)
        for (int32 X = -6; X <= 6; ++X)
            Heights.Add(Terrain.Height(Center + FVector2D(X, Y)));
    const auto Fill = [&]()
    {
        int32 Index = 0;
        FVector2D Sum(0, 0);
        for (int32 Y = -6; Y <= 6; ++Y)
            for (int32 X = -6; X <= 6; ++X)
            {
                const double Added = Terrain.Height(Center + FVector2D(X, Y)) - Heights[Index++];
                Sum.X += Added;
                Sum.Y += Added * X;
            }
        return Sum;
    };
    Load Gravel;
    Gravel.Add(Material::Gravel, 2.0);
    Terrain.Pour(FVector(Center.X, Center.Y, Terrain.Height(Center) + 2.0), FVector::ForwardVector, 1.67, Gravel);
    Terrain.AdvancePours(0.15);
    const bool bAirborne = Fill().IsNearlyZero();
    const auto InFlight = Terrain.SaveTerrain();
    const bool bRoundTrip = Terrain.RestoreTerrain(Baseline) && Terrain.RestoreTerrain(InFlight);
    for (int32 Step = 0; Step < 60; ++Step)
        Terrain.AdvancePours(FixedStep);
    const auto Placed = Fill();
    const bool bLanded = FMath::IsNearlyEqual(Placed.X, 2.0, 0.002) && Placed.Y / 2.0 > 0.25;
    UE_LOG(LogTemp, Display, TEXT("Falling gravel: airborne %d, save %d, landed %.3f m3, advanced %.2f m"),
        bAirborne, bRoundTrip, Placed.X, Placed.Y / 2.0);
    return Terrain.RestoreTerrain(Baseline) && bAirborne && bRoundTrip && bLanded;
}

bool TestGravelRoad(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    if (!Terrain.HasRoadContract())
        return true;
    const auto Original = Terrain.SaveTerrain();
    auto* Truck = Fleet[static_cast<int32>(EMudMachine::Truck)];
    const FVector2D SavedPosition = Truck->Position;
    const double SavedHeading = Truck->Heading;
    const Earthwork::Load SavedCargo = Truck->Cargo;
    const FVector2D Ford = Terrain.Fords.Last();
    const int32 X0 = FMath::RoundToInt(Ford.X), Y0 = FMath::RoundToInt(Ford.Y);
    constexpr int32 Reach = 27;
    constexpr double Cap = 0.19;
    const double Water = Terrain.WaterDepth(Ford) + Terrain.Height(Ford);
    const auto Raw = Terrain.SurveyFordRoad(Ford);
    const bool bRawBlocked = !Raw.IsReady() && Raw.Deep > 0 && Raw.Soft > 0 && Raw.Fill > 0 &&
        Terrain.WaterDepth(Ford) > FordingLimit(1.05, DeliveryFraction);

    double Profile[2 * Reach + 1];
    for (int32 I = 0; I <= 2 * Reach; ++I)
    {
        double Z = Water + 0.04;
        for (int32 Y = Y0 - 2; Y <= Y0 + 2; ++Y)
            Z = FMath::Max(Z, Terrain.Height(FVector2D(X0 + I - Reach, Y)));
        Profile[I] = Z;
    }
    for (int32 I = 1; I <= 2 * Reach; ++I)
        Profile[I] = FMath::Max(Profile[I], Profile[I - 1] - 0.23);
    for (int32 I = 2 * Reach - 1; I >= 0; --I)
        Profile[I] = FMath::Max(Profile[I], Profile[I + 1] - 0.23);
    for (int32 I = 0; I <= 2 * Reach; ++I)
        for (int32 Y = Y0 - 2; Y <= Y0 + 2; ++Y)
        {
            const FVector2D Point(X0 + I - Reach, Y);
            Earthwork::Load Soil;
            Soil.Add(Material::Soil, FMath::Max(0.0, Profile[I] - Terrain.Height(Point)));
            Terrain.Deposit(Point, 0.49, Soil);
        }
    const auto SoilSurvey = Terrain.SurveyFordRoad(Ford);
    const bool bSoilFails = !SoilSurvey.IsReady() && SoilSurvey.Soft > 0 && SoilSurvey.Deep == 0 && SoilSurvey.Rough == 0;
    Earthwork::Load Gravel;
    Gravel.Add(Material::Gravel, Cap);
    for (int32 X = X0 - Reach; X <= X0 + Reach; ++X)
        for (int32 Y = Y0 - 2; Y <= Y0 + 2; ++Y)
            Terrain.Deposit(FVector2D(X, Y), 0.49, Gravel);
    const auto Built = Terrain.SurveyFordRoad(Ford);
    const auto Graded = Terrain.SaveTerrain();
    Earthwork::Load Ridge;
    Ridge.Add(Material::Gravel, 0.70);
    for (int32 Y = Y0 - 4; Y <= Y0 + 4; ++Y)
        Terrain.Deposit(FVector2D(X0, Y), 0.49, Ridge);
    const auto RidgeSurvey = Terrain.SurveyFordRoad(Ford);
    const bool bRidgeFails = !RidgeSurvey.IsReady() && RidgeSurvey.Rough > 0 && RidgeSurvey.Deep == 0 && RidgeSurvey.Soft == 0;
    const bool bGradeRestored = Terrain.RestoreTerrain(Graded);

    Truck->StopWork();
    Truck->Position = FVector2D(X0 - Reach + 4, Y0);
    Truck->Heading = 0;
    Truck->Cargo = {};
    Truck->Cargo.Add(Material::Gravel, Truck->Capacity() * 0.9);
    Truck->UpdatePose(Terrain);
    for (int32 I = 0; I < 960 && Truck->Position.X < X0 + Reach - 3; ++I)
        Truck->Drive(FixedStep, 1, 0, false, Terrain, Fleet);
    UE_LOG(LogTemp, Display, TEXT("Crossing contact: position %.2f/%.2f, water %.3f, grip %.3f, roll %d, speed %.3f"),
        Truck->Position.X-X0,Truck->Position.Y-Y0,Terrain.WaterDepth(Truck->Position),Truck->Grip,Truck->RolledSide,Truck->Speed);
    const bool bTruckPassed = Truck->Position.X >= X0 + Reach - 3 && Truck->RolledSide == 0;
    const double Travel = Truck->Position.X - (X0 - Reach + 4);
    const bool bRestored = Terrain.RestoreTerrain(Original);
    Truck->StopWork();
    Truck->Position = SavedPosition;
    Truck->Heading = SavedHeading;
    Truck->RolledSide = 0;
    Truck->Cargo = SavedCargo;
    Truck->UpdatePose(Terrain);
    UE_LOG(LogTemp, Display, TEXT("Gravel crossing: raw blocked %d, soil fails %d, ridge fails %d, finished %.0f%%, truck %.1f m (%d)"),
        bRawBlocked, bSoilFails, bRidgeFails, double(Built.Progress()), Travel, bTruckPassed);
    return bRawBlocked && bSoilFails && bRidgeFails && bGradeRestored && Built.IsReady() && Built.Fill == 0 && bTruckPassed && bRestored;
}

bool TestWheelSteering(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const auto TerrainBefore = Terrain.SaveTerrain();
    bool bPassed = true;
    for (const EMudMachine Kind : {EMudMachine::Dumper, EMudMachine::Loader, EMudMachine::Truck})
    {
        auto* Machine = Fleet[static_cast<int32>(Kind)];
        const FVector2D Start = Machine->Position;
        const auto Reset = [&]()
        {
            Machine->StopWork();
            Machine->Position = Start;
            Machine->Heading = 0;
            Machine->RolledSide = 0;
            Machine->UpdatePose(Terrain);
        };
        const bool bArticulated = Machine->ArticulationLimit() > 0;
        const auto Drive = [&](double Throttle, double Steering, bool bBrake, int32 Steps)
        {
            for (int32 I = 0; I < Steps; ++I)
                Machine->Drive(FixedStep, Throttle, Steering, bBrake, Terrain, Fleet);
        };
        Drive(0, 1, true, 42);
        const double ParkedYaw = Machine->Heading;
        const double ParkedBend = Machine->GetArticulation();
        const bool bParked = bArticulated ? ParkedYaw > 5 && ParkedBend < -10 && Machine->Position.Y < Start.Y - 0.1 :
            FMath::IsNearlyZero(ParkedYaw) && FVector2D::DistSquared(Machine->Position, Start) < 0.0001;
        Drive(0, 0, true, 20);
        const bool bHolds = FMath::IsNearlyEqual(Machine->Heading, ParkedYaw, 0.001) &&
            FMath::IsNearlyEqual(Machine->GetArticulation(), ParkedBend, 0.001);

        Reset();
        Drive(0, 1, true, 65);
        const FVector2D BeforeForward = Machine->Position;
        const double BeforeForwardYaw = Machine->Heading;
        Drive(0.5, 1, false, 65);
        const bool bForward = Machine->Position.X > BeforeForward.X + 0.25 && Machine->Heading > BeforeForwardYaw + 2;

        Reset();
        Drive(0, 1, true, 65);
        const FVector2D BeforeReverse = Machine->Position;
        const double BeforeReverseYaw = Machine->Heading;
        Drive(-0.5, 1, false, 65);
        const bool bReverse = Machine->Position.X < BeforeReverse.X - 0.25 && Machine->Heading < BeforeReverseYaw - 2;
        Reset();

        UE_LOG(LogTemp, Display, TEXT("Wheel steering %s: parked %d (yaw %.1f, bend %.1f), held %d, forward %d, reverse %d"),
            Machine->Name(), bParked, ParkedYaw, ParkedBend, bHolds, bForward, bReverse);
        bPassed &= bParked && bHolds && bForward && bReverse;
    }
    return Terrain.RestoreTerrain(TerrainBefore) && bPassed;
}

bool TestMachineWork(AMudTerrain& Terrain, const TArray<AMudMachine*>& Fleet)
{
    const FVector2D Center = XY(Terrain.Start);
    const auto GroundVolume = [&]()
    {
        return Terrain.MaterialVolumeBalance();
    };
    const double Before = GroundVolume();
    auto* Excavator = Fleet[1];
    Excavator->ToggleTool();
    FVector2D FirstCut = FVector2D::ZeroVector;
    double Sweep = 0;
    for (int32 I = 0; I < 330; ++I)
    {
        const double BeforeStep = Excavator->Cargo.Volume();
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet);
        if (Excavator->Cargo.Volume() > BeforeStep + Epsilon)
        {
            const FVector2D Tip = XY(Excavator->ToolPoint());
            if (BeforeStep <= Epsilon)
                FirstCut = Tip;
            Sweep = FMath::Max(Sweep, FVector2D::Distance(Tip, FirstCut));
        }
    }
    const double Dig = Excavator->Cargo.Volume();
    const bool bDug = Dig > 0.25 && Dig <= Excavator->Capacity() && Sweep > 0.85 &&
        FMath::IsNearlyEqual(Before - GroundVolume(), Dig, 0.005);
    Excavator->ToggleUnload();
    for (int32 I = 0; I < 225; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet);
    Terrain.SettlePours();
    const bool bTipped = Excavator->Cargo.Volume() < Epsilon && FMath::IsNearlyEqual(Before, GroundVolume(), 0.005);

    auto* Dozer = Fleet[0];
    Dozer->ToggleTool();
    for (int32 I = 0; I < 45; ++I)
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    const bool bStationaryBlade = Dozer->Cargo.Volume() <= Epsilon &&
        FMath::IsNearlyEqual(Before, GroundVolume(), 0.005);
    for (int32 I = 0; I < 95; ++I)
    {
        Dozer->Drive(FixedStep, 1, 0, false, Terrain, Fleet);
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    }
    const double Cut = Dozer->Cargo.Volume();
    const bool bCut = Cut > 0.25 && FMath::IsNearlyEqual(Before - GroundVolume(), Cut, 0.005);
    Dozer->Halt();
    Dozer->ToggleUnload();
    for (int32 I = 0; I < 110; ++I)
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    const bool bSpread = Dozer->Cargo.Volume() < Epsilon && FMath::IsNearlyEqual(Before, GroundVolume(), 0.005);
    Dozer->ChangeGroup();
    Dozer->ToggleTool();
    Dozer->UpdatePose(Terrain);
    const FVector2D Point = XY(Dozer->ToolPoint());
    const double Heap = Terrain.Height(Point);
    Dozer->Position.X -= 1.2;
    Dozer->UpdatePose(Terrain);
    for (int32 I = 0; I < 105; ++I)
    {
        Dozer->Drive(FixedStep, 0.55, 0, false, Terrain, Fleet);
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    }
    const double GradeDrop = Heap - Terrain.Height(Point);
    const bool bGraded = GradeDrop > 0.025 && FMath::IsNearlyEqual(Before, GroundVolume(), 0.005);

    Excavator->Position = Center + FVector2D(-8, 3);
    Excavator->Boom = 15;
    Excavator->Stick = -28;
    Excavator->Curl = 0;
    Excavator->UpdatePose(Terrain);
    Excavator->ToggleTool();
    for (int32 I = 0; I < 330; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet);
    const double SecondBucket = Excavator->Cargo.Volume();
    Excavator->StopWork();
    Excavator->Boom = 48;
    Excavator->Stick = -32;
    Excavator->Curl = -93;
    Excavator->UpdatePose(Terrain);
    auto* Dumper = Fleet[2];
    Dumper->Position = XY(Excavator->ToolPoint()) + FVector2D(1.0, 0);
    Dumper->UpdatePose(Terrain);
    Excavator->Curl = 30;
    Excavator->UpdatePose(Terrain);
    Excavator->ToggleUnload();
    for (int32 I = 0; I < 225; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet);
    Terrain.SettlePours();
    const double BedLoad = Dumper->Cargo.Volume();
    const bool bLoadedBed = SecondBucket > 0.25 && FMath::IsNearlyEqual(SecondBucket, BedLoad, 0.002) &&
        Excavator->Cargo.Volume() < Epsilon && FMath::IsNearlyEqual(Before - GroundVolume(), BedLoad, 0.005);
    Dumper->Position = Center + FVector2D(12, 7);
    Dumper->UpdatePose(Terrain);
    Dumper->ToggleUnload();
    for (int32 I = 0; I < 160; ++I)
        Dumper->Work(FixedStep, 0, 0, Terrain, Fleet);
    Terrain.SettlePours();
    const bool bDumpedBed = Dumper->Cargo.Volume() < Epsilon && !Dumper->IsTipping() &&
        FMath::IsNearlyEqual(Before, GroundVolume(), 0.005);
    Excavator->Position = Center + FVector2D(-10, -10);
    Excavator->Boom = -9;
    Excavator->Stick = -14;
    Excavator->Curl = -73;
    Excavator->UpdatePose(Terrain);
    Excavator->ChangeGroup();
    for (int32 I = 0; I < 46; ++I)
        Excavator->Work(FixedStep, 1, 0, Terrain, Fleet);
    const double Manual = Excavator->Cargo.Volume();
    const bool bManualDig = Manual > 0.09 && FMath::IsNearlyEqual(Before - GroundVolume(), Manual, 0.005);
    Excavator->Halt();
    const double StartBoom = Excavator->Boom, StartStick = Excavator->Stick;
    const double StartCurl = Excavator->Curl, StartSwing = Excavator->Swing;
    MachineMotion::HydraulicInput LeftDrag;
    MachineMotion::AccumulateMouseDrag(LeftDrag, true, false, MachineMotion::MouseTool::Excavator, 0.6, 0.6);
    for (int32 I = 0; I < 18; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet, LeftDrag);
    const bool bMouseArm = Excavator->Boom > StartBoom + 1 && Excavator->Curl > StartCurl + 1 &&
        FMath::IsNearlyEqual(StartSwing, Excavator->Swing) && FMath::IsNearlyEqual(StartStick, Excavator->Stick);

    Excavator->ChangeGroup();
    Excavator->Halt();
    const double RaisedBoom = Excavator->Boom, TurnedSwing = Excavator->Swing;
    const double CurledBucket = Excavator->Curl;
    MachineMotion::HydraulicInput RightDrag;
    MachineMotion::AccumulateMouseDrag(RightDrag, false, true, MachineMotion::MouseTool::Excavator, 0.6, 0.6);
    for (int32 I = 0; I < 18; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet, RightDrag);
    const bool bMouseBucket = Excavator->Swing > TurnedSwing + 1 && Excavator->Stick > StartStick + 1 &&
        FMath::IsNearlyEqual(RaisedBoom, Excavator->Boom) && FMath::IsNearlyEqual(CurledBucket, Excavator->Curl);
    Excavator->Halt();
    const FVector FlatEdge = Excavator->ToolPoint();
    MachineMotion::HydraulicInput TiltDrag;
    MachineMotion::AccumulateMouseDrag(TiltDrag, true, true, MachineMotion::MouseTool::Excavator, 0.7, 0);
    for (int32 I = 0; I < 18; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, Fleet, TiltDrag);
    const bool bMouseTilt = Excavator->BucketTilt > 2 && FVector::DistSquared(FlatEdge, Excavator->ToolPoint()) > 0.0001;
    Terrain.SettlePours();

    Dozer->Halt();
    const double StartBlade = Dozer->Blade;
    MachineMotion::HydraulicInput BladeDrag;
    MachineMotion::AccumulateMouseDrag(BladeDrag, true, false, MachineMotion::MouseTool::Blade, 0.6, -0.6);
    for (int32 I = 0; I < 18; ++I)
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet, BladeDrag);
    const bool bMouseBlade = Dozer->Blade < StartBlade - 0.02;
    Dozer->Halt();
    const double LowBlade = Dozer->ToolPoint().Z;
    for (int32 I = 0; I < 18; ++I)
        Dozer->Work(FixedStep, 1, 0, Terrain, Fleet);
    const double HighBlade = Dozer->ToolPoint().Z;
    Dozer->Halt();
    for (int32 I = 0; I < 18; ++I)
        Dozer->Work(FixedStep, -1, 0, Terrain, Fleet);
    const bool bBladeControls = HighBlade > LowBlade + 0.08 && Dozer->ToolPoint().Z < HighBlade - 0.08;
    auto* Loader = Fleet[LegacyVehicleCount];
    const auto MoveLoader = [&](double Lift, double Tilt)
    {
        Loader->Halt();
        for (int32 I = 0; I < 24; ++I)
            Loader->Work(FixedStep, Lift, Tilt, Terrain, Fleet);
        return Loader->ToolPoint().Z;
    };
    const double InitialEdge = Loader->ToolPoint().Z;
    const double RaisedEdge = MoveLoader(1, 0);
    const double LoweredEdge = MoveLoader(-1, 0);
    const double CurledEdge = MoveLoader(0, -1);
    const double TippedEdge = MoveLoader(0, 1);
    const bool bLoaderControls = RaisedEdge > InitialEdge + 0.15 && LoweredEdge < RaisedEdge - 0.15 &&
        CurledEdge > LoweredEdge + 0.15 && TippedEdge < CurledEdge - 0.15;
    Loader->Boom = LoaderRig::MinBoom;
    Loader->Curl = 0;
    Loader->Halt();
    Loader->UpdatePose(Terrain);
    Load TestPile;
    TestPile.Add(Material::Gravel, 2.4);
    Terrain.Deposit(XY(Loader->ToolPoint()), 1.1, TestPile);
    const double PileGround = GroundVolume();
    Loader->Speed = 1.1;
    for (int32 I = 0; I < 55; ++I)
    {
        Loader->Drive(FixedStep, 0.4, 0, false, Terrain, Fleet);
        Loader->Work(FixedStep, 0, 0, Terrain, Fleet);
    }
    const double Loaded = Loader->Cargo.Volume();
    const bool bLoaderScoop = Loaded > 0.25 && FMath::IsNearlyEqual(PileGround - GroundVolume(), Loaded, 0.005);
    Loader->Halt();
    for (int32 I = 0; I < 117; ++I)
        Loader->Work(FixedStep, 1, 0, Terrain, Fleet);
    for (int32 I = 0; I < 117; ++I)
        Loader->Work(FixedStep, 0, 1, Terrain, Fleet);
    Terrain.SettlePours();
    const bool bLoaderDump = Loader->Cargo.Volume() < Epsilon && FMath::IsNearlyEqual(PileGround, GroundVolume(), 0.005);
    UE_LOG(LogTemp, Display, TEXT("Wheel loader: controls %d, scoop %.2f m3 (%d), tip %d"), bLoaderControls,
        Loaded, bLoaderScoop, bLoaderDump);

    Dumper->Cargo.Add(Material::Gravel, 1.0);
    for (int32 I = 0; I < 60; ++I)
        Dumper->Work(FixedStep, 1, 0, Terrain, Fleet);
    for (int32 I = 0; I < 60; ++I)
        Dumper->Work(FixedStep, 0, 0, Terrain, Fleet);
    const bool bHeldBed = Dumper->IsTipping() && Dumper->Cargo.Volume() < Epsilon;
    for (int32 I = 0; I < 120; ++I)
        Dumper->Work(FixedStep, -1, 0, Terrain, Fleet);
    const bool bLoweredBed = !Dumper->IsTipping();
    Terrain.SettlePours();
    const double ManualDump = GroundVolume() - PileGround;
    const bool bManualBed = bHeldBed && bLoweredBed && FMath::IsNearlyEqual(ManualDump, 1.0, 0.005);
    UE_LOG(LogTemp, Display, TEXT("Manual dump bed: held %d, lowered %d, dumped %.2f m3"), bHeldBed, bLoweredBed, ManualDump);

    // Isolate manual cutting from the angles left by the mouse-control checks.
    Dozer->StopWork();
    Dozer->Blade = 0;
    Dozer->BladeYaw=Dozer->BucketTilt=0;
    Dozer->bGrade = false;
    Dozer->Heading = 0;
    Dozer->Position = Center + FVector2D(-14, -13);
    Dozer->UpdatePose(Terrain);
    Load PushPile;
    // A small, pushable stockpile; the overload/stall case is covered by world validation.
    PushPile.Add(Material::Gravel, 1.6);
    Terrain.Deposit(XY(Dozer->ToolPoint()), 1.4, PushPile);
    const double PushGround = GroundVolume();
    for (int32 I = 0; I < 60; ++I)
        Dozer->Work(FixedStep, -1, 0, Terrain, Fleet);
    const FVector2D ManualStart=Dozer->Position;
    for (int32 I = 0; I < 90; ++I)
    {
        Dozer->Drive(FixedStep, 1, 0, false, Terrain, Fleet);
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    }
    const double ManualPush = Dozer->Cargo.Volume();
    const bool bManualPush = !Dozer->bTool && Dozer->Blade < -0.30 && ManualPush > 0.20 &&
        FMath::IsNearlyEqual(PushGround - GroundVolume(), ManualPush, 0.005);
    UE_LOG(LogTemp,Display,TEXT("Manual dozer: travel %.3f m, blade %.3f, cargo %.3f m3, %s"),
        FVector2D::Distance(ManualStart,Dozer->Position),Dozer->Blade,ManualPush,*Dozer->ToolDescription());

    Dozer->Cargo.Add(Material::Soil, Dozer->Capacity() - ManualPush);
    const FVector2D BladeEdge = XY(Dozer->ToolPoint());
    // A full blade feeds a shallow windrow, rather than ramming a multi-metre face.
    Load Windrow;Windrow.Add(Material::Gravel,.4);
    Terrain.Deposit(BladeEdge, 1.8, Windrow);
    const double BeforeOverflow = GroundVolume();
    const double FullBladeVolume=Dozer->Cargo.Volume();
    const FVector2D RightBank = BladeEdge + FVector2D(-0.65, 2.55);
    const FVector2D LeftBank = BladeEdge + FVector2D(-0.65, -2.55);
    const double RightBefore = Terrain.Height(RightBank), LeftBefore = Terrain.Height(LeftBank);
    for (int32 I = 0; I < 90; ++I)
    {
        Dozer->Drive(FixedStep, 0.5, 0, false, Terrain, Fleet);
        Dozer->Work(FixedStep, 0, 0, Terrain, Fleet);
    }
    const double RightGain = Terrain.Height(RightBank) - RightBefore, LeftGain = Terrain.Height(LeftBank) - LeftBefore;
    const bool bFullPush = RightGain > 0.005 && LeftGain > 0.005 && Dozer->Cargo.Volume()>Dozer->Capacity()*.5 &&
        Dozer->Cargo.Volume()<=Dozer->Capacity()+Epsilon&&
        FMath::IsNearlyEqual(BeforeOverflow+FullBladeVolume,GroundVolume()+Dozer->Cargo.Volume(),0.00001);

    Excavator->StopWork();
    Excavator->Position = Center + FVector2D(-11, -9);
    Excavator->Boom = 48;
    Excavator->Stick = -32;
    Excavator->Curl = -93;
    Excavator->Swing = Excavator->BucketTilt = 0;
    Excavator->Cargo = {};
    Excavator->Cargo.Add(Material::Soil, 0.45);
    Excavator->UpdatePose(Terrain);
    const double ManualTipGround = GroundVolume();
    for (int32 I = 0; I < 12; ++I)
        Excavator->Work(FixedStep, 0, 0, Terrain, {});
    const bool bManualTipAirborne = Excavator->Cargo.Volume() < Epsilon && FMath::IsNearlyEqual(ManualTipGround, GroundVolume(), 0.002);
    Terrain.SettlePours();
    const bool bManualTip = bManualTipAirborne && FMath::IsNearlyEqual(GroundVolume() - ManualTipGround, 0.45, 0.002);
    UE_LOG(LogTemp, Display, TEXT("Manual bucket tip: airborne %d, mass settled %d"), bManualTipAirborne, bManualTip);
    UE_LOG(LogTemp, Display, TEXT("Dozer material transport: manual %.2f m3 (%d), windrows left %.2f m, right %.2f m (%d)"),
        ManualPush, bManualPush, LeftGain, RightGain, bFullPush);
    UE_LOG(LogTemp, Display, TEXT("Manual blade directions: %d"), bBladeControls);
    UE_LOG(LogTemp, Display, TEXT("Mouse hydraulics: boom/swing %d, stick/curl %d, side tilt %d, blade %d"),
        bMouseArm, bMouseBucket, bMouseTilt, bMouseBlade);
    UE_LOG(LogTemp, Display, TEXT("Machine earthwork: bucket %.2f m3 over %.2f m, dug %d, tipped %d, manual %.2f m3 (%d); blade %.2f m3, still %d, cut %d, spread %d, graded %d (%.2f m); dumper %.2f m3, loaded %d, tipped %d"),
        Dig, Sweep, bDug, bTipped, Manual, bManualDig, Cut, bStationaryBlade, bCut, bSpread, bGraded, GradeDrop, BedLoad, bLoadedBed, bDumpedBed);
    return bDug && bTipped && bManualDig && bStationaryBlade && bCut && bSpread && bGraded &&
        bLoadedBed && bDumpedBed && bMouseArm && bMouseBucket && bMouseTilt && bMouseBlade &&
        bBladeControls && bLoaderControls && bLoaderScoop && bLoaderDump && bManualBed && bManualPush && bFullPush && bManualTip;
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
    FootAvatar = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FootAvatar"));
    FootAvatar->SetupAttachment(RootComponent);
    FootAvatar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FootAvatar->SetVisibility(false);
    FootAvatar->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    FootAvatar->AddTickPrerequisiteActor(this);
    DriverAvatar = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("DriverAvatar"));
    DriverAvatar->SetupAttachment(RootComponent);
    DriverAvatar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DriverAvatar->SetVisibility(false);
    Audio = CreateDefaultSubobject<UMudAudio>(TEXT("ValleyAudio"));
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
    ProfileWorldId=Terrain->GetWorldId();
    if(FParse::Param(FCommandLine::Get(),TEXT("MudSoilLab")))ProfileWorldId.RemoveFromEnd(TEXT("-soil-lab"));
    if(ProfileWorldId.IsEmpty())ProfileWorldId=TEXT("Default");
    OnFoot = XY(Terrain->Start);
    for (int32 Id = 0; Id < UE_ARRAY_COUNT(ParkingOffsets); ++Id)
    {
        auto* Machine = GetWorld()->SpawnActor<AMudMachine>();
        Machine->Initialize(static_cast<EMudMachine>(Id), OnFoot + ParkingOffsets[Id], *Terrain);
        Fleet.Add(Machine);
    }
    FString MaterialReviewFolder;
    if (FParse::Value(FCommandLine::Get(),TEXT("MudMaterialReview="),MaterialReviewFolder))
    {
        bInitializeFailed=true; // Isolated rendering fixture; user progress is never loaded/saved.
        GetWorld()->SpawnActor<AMudMaterialReview>()->Initialize(*Terrain,Fleet);
        return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("MudTerrainAudit")))
    {
        bInitializeFailed=true; // No profile load or autosave during terrain diagnostics.
        bool Passed=true;
        for(const FVector2D Point : {XY(Terrain->Start),FVector2D(236,1640),FVector2D(337,1665),FVector2D(535,610),FVector2D(650,1320)})
            Passed=Terrain->ValidateSurfaceRendering(Point)&&Passed;
        FString Report;
        if(FParse::Value(FCommandLine::Get(),TEXT("MudSmokeReport="),Report))
            Passed=FFileHelper::SaveStringToFile(Passed?TEXT("passed"):TEXT("failed"),*Report)&&Passed;
        UE_LOG(LogTemp,Display,TEXT("Natural terrain mesh/collision-height/LOD audit: %d"),Passed);
        FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);return;
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("MudWorldValidation")))
    {
        bInitializeFailed=true; // The diagnostic actor owns the simulation; no player autosave is armed.
        auto* Validation=GetWorld()->SpawnActor<AMudWorldValidation>();Validation->Initialize(Terrain);
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("MudLoaderValidation")))
    {
        const bool Passed = RunLoaderValidation(*Terrain, Fleet);
        FString Report;
        if (FParse::Value(FCommandLine::Get(), TEXT("MudSmokeReport="), Report))
            FFileHelper::SaveStringToFile(Passed ? TEXT("passed") : TEXT("failed"), *Report);
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("MudSoilValidation")))
    {
        const bool Passed = RunSoilValidation(*Terrain, Fleet);
        FString Report;
        if (FParse::Value(FCommandLine::Get(), TEXT("MudSmokeReport="), Report))
            FFileHelper::SaveStringToFile(Passed ? TEXT("passed") : TEXT("failed"), *Report);
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return;
    }
    if (bSmoke)
    {
        const auto Before = Terrain->SaveTerrain();
        const auto HasFinish = [](const FJsonObject& Data, double Sign)
        {
            for (const auto& Value : Data.GetArrayField(TEXT("edits")))
            {
                double Finish;
                if (Value->AsObject()->TryGetNumberField(TEXT("finish"), Finish) && Finish * Sign > 0.20)
                    return true;
            }
            return false;
        };
        Earthwork::Load Gravel;
        Gravel.Add(Material::Gravel, 2.0);
        const FVector2D Start = XY(Terrain->Start);
        const double Initial = Terrain->Height(Start);
        Terrain->Deposit(Start, 1.2, Gravel);
        const bool bRaised = Terrain->Height(Start) > Initial && Terrain->Surface(Start) == Material::Gravel;
        const auto Filled = Terrain->SaveTerrain();
        const bool bFillFinish = HasFinish(*Filled, 1);
        const double Removed = Terrain->Excavate(Start, 1.2, 20, Initial).Volume();
        const bool bCutFinish = HasFinish(*Terrain->SaveTerrain(), -1);
        const bool bSavedFinish = Terrain->RestoreTerrain(Filled) && HasFinish(*Terrain->SaveTerrain(), 1);
        const bool bRestored = Terrain->RestoreTerrain(Before) && FMath::IsNearlyEqual(Terrain->Height(Start), Initial, 0.002);
        const bool bTrackMarks = TestTrackMarks(*Terrain);
        const bool bFallingLoad = TestFallingLoad(*Terrain);
        const bool bSteering = Fleet.Num() == UE_ARRAY_COUNT(ParkingOffsets) && TestWheelSteering(*Terrain, Fleet);
        const bool bRoad = Fleet.Num() == UE_ARRAY_COUNT(ParkingOffsets) && TestGravelRoad(*Terrain, Fleet);
        bool bPassed = Fleet.Num() == UE_ARRAY_COUNT(ParkingOffsets) && bRaised && bRestored && FMath::IsNearlyEqual(Removed, 2.0, 0.002)
            && TestMachineWork(*Terrain, Fleet) && bSteering && bRoad && bTrackMarks && bFallingLoad && bFillFinish && bCutFinish && bSavedFinish;
        Active = INDEX_NONE;
        auto* PC = GetWorld()->GetFirstPlayerController();
        const bool bTabSwitch = PC && !Fleet.IsEmpty() && TestVehicleSwitch(*this, *PC);
        bPassed &= bTabSwitch;
        bPassed &= Terrain->ValidateSurfaceRendering(XY(Terrain->Start));
        UE_LOG(LogTemp, Display, TEXT("Tab vehicle switching: %s"), bTabSwitch ? TEXT("passed") : TEXT("failed"));
        UE_LOG(LogTemp, Display, TEXT("Earthwork surface finish: fill %d, cut %d, save %d"), bFillFinish, bCutFinish, bSavedFinish);
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
        Mode.SetConsumeCaptureMouseDown(false);
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = false;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("MudHighResolution")))
        ApplyQualityMode(true);
    const bool bBenchmark = FParse::Param(FCommandLine::Get(), TEXT("MudBenchmark"));
    if (bBenchmark && !Terrain->Fords.IsEmpty() && !Terrain->Piles.IsEmpty())
    {
        // Capture a representative workload at the southern crossing.
        Active = 2;
        auto* Dumper = GetMachine();
        const auto Ford = Terrain->Fords.Last();
        Dumper->Position = Terrain->IsSoilLab() ? FVector2D(210, 800) : Ford + FVector2D(-9.5, 2);
        Dumper->Heading = 180;
        Dumper->UpdatePose(*Terrain);
        BenchmarkFrames.Reserve(9600);
        BenchmarkLast = BenchmarkStart = FPlatformTime::Seconds();
        bShowHelp = false;
    }
    InitializeFootAvatar();
    ResetFootMotion(LookYaw);
    Audio->Initialize();
    if (!bBenchmark && GetWorld()->WorldType == EWorldType::Game)
    {
        if (IFileManager::Get().FileExists(*LoadPath()))
            bProtectSave = !RestoreProgress();
        NextAutosave = FPlatformTime::Seconds() + AutosaveInterval;
    }
    Terrain->UpdateTiles(GetPosition());
    UpdateCamera();
}

void AMudOperator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClosePauseMenu();
    if (EndPlayReason == EEndPlayReason::Quit && NextAutosave > 0 && !bProtectSave)
        WriteProgress();
    Super::EndPlay(EndPlayReason);
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
    const double FrameStep = FMath::Min(0.12, static_cast<double>(Dt));
    if (FrameStep > 0)
    {
        // Keep large frame times within the terrain solver's step size and advance on every rendered frame.
        const int32 Substeps = FMath::CeilToInt(FrameStep / (FixedStep + 0.0001));
        const double MouseScale = MachineMotion::MouseValveScale(FrameStep);
        FVector2D Hydraulics(VerticalTap.Resolve(HydraulicA, FrameStep), HorizontalTap.Resolve(HydraulicB, FrameStep));
        MachineMotion::HydraulicInput Direct{
            MouseHydraulics.Boom * MouseScale, MouseHydraulics.Stick * MouseScale,
            MouseHydraulics.Curl * MouseScale, MouseHydraulics.Swing * MouseScale, MouseHydraulics.Tilt * MouseScale
        };
        const auto* Machine = GetMachine();
        if (Machine && Machine->Kind == EMudMachine::Excavator)
        {
            if (const auto* PC = Cast<APlayerController>(GetController()))
            {
                const auto Held = [PC](FKey Key)
                {
                    return PC->IsInputKeyDown(Key) || PC->WasInputKeyJustPressed(Key);
                };
                const bool Shift = Held(EKeys::LeftShift) || Held(EKeys::RightShift);
                MachineMotion::AddExcavatorKeys(Direct, Shift, double(Held(EKeys::K)) - double(Held(EKeys::M)),
                    double(Held(EKeys::J)) - double(Held(EKeys::N)));
            }
            MachineMotion::AccumulateExcavatorGamepad(Direct, bPadHydraulics, bPadSecondHydraulics,
                PadHydraulicB, PadHydraulicA);
        }
        else
        {
            Hydraulics += FVector2D(HydraulicKeyA, HydraulicKeyB);
            if (bPadHydraulics)
                Hydraulics += FVector2D(PadHydraulicA, PadHydraulicB);
        }
        for (int32 Index = 0; Index < Substeps; ++Index)
            Step(FrameStep / Substeps, Hydraulics, Direct);
        MouseHydraulics = {};
    }
    const FVector2D Location = GetPosition();
    Terrain->UpdateTiles(Location);
    Terrain->UpdateSpills(FMath::Min(Dt, 0.1f));
    Terrain->UpdateEnvironment(Location,FMath::Min(Dt,0.1f));
    if (NextAutosave > 0 && !bProtectSave && FPlatformTime::Seconds() >= NextAutosave)
    {
        NextAutosave = FPlatformTime::Seconds() + AutosaveInterval;
        Notify(WriteProgress() ? TEXT("Progress autosaved.") : TEXT("Could not autosave progress."));
    }
    const int32 Width = Terrain->GetWorldSize();
    if (Location.X < 0 || Location.Y < 0 || Location.X > Width || Location.Y > Width)
        return;
    UpdateCamera(Dt);
    Audio->Mix(Dt, *Terrain, Location, GetMachine(), Fleet, bCab, bBrake);
    if (BenchmarkStart > 0)
        UpdateBenchmark();
}

void AMudOperator::Step(double Dt, const FVector2D& Hydraulics, const MachineMotion::HydraulicInput& Direct)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(MudSimulationStep);
    Terrain->AdvancePours(Dt);
    Terrain->AdvanceSoil(Dt);
    auto* Machine = GetMachine();
    if (Machine)
    {
        if (Throttle < -0.1)
            bCruise = false;
        const double Command = bCruise && FMath::Abs(Throttle) < 0.1 ? Cruise : Throttle;
        Machine->Drive(Dt, Command, Steering, bBrake, *Terrain, Fleet);
        Machine->Work(Dt, Hydraulics.X, Hydraulics.Y, *Terrain, Fleet, Direct);
    }
    else
        StepOnFoot(Dt);

    // Parked machines continue tipping, and their chassis follow terrain edits underneath them.
    for (auto* Other : Fleet)
    {
        if (Other == Machine)
            continue;
        if (Other->IsTipping())
            Other->Work(Dt, 0, 0, *Terrain, Fleet);
    }
    if (Machine && bDepotLoad)
    {
        if (FMath::Abs(Machine->Speed) > 0.35 || Machine->IsTipping() || Machine->RolledSide != 0)
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
                if (Load.Volume() > Epsilon)
                    Machine->RecordEarthSound(Load.Volume() / Dt, Load.DominantMaterial());
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
        auto* Truck = Fleet[static_cast<int32>(EMudMachine::Truck)];
        if (Truck->Cargo.Volume() < Truck->Capacity() * DeliveryFraction)
            bDelivery = false;
        else if (FVector2D::DistSquared(Truck->Position, XY(Terrain->Start)) < FMath::Square(22.0))
            bDelivery = true;
        if (bDelivery && Terrain->IsRoadReady() && Truck->RolledSide == 0 &&
            FVector2D::DistSquared(Truck->Position, XY(Terrain->Goal)) < FMath::Square(12.0))
        {
            bFinished = true;
            Notify(TEXT("Delivery complete. The valley road is open."));
            if (NextAutosave > 0 && !bProtectSave)
                WriteProgress();
            Audio->Interface(TEXT("Goal"));
        }
    }
}

void AMudOperator::UpdateCamera(float Dt)
{
    auto* Machine = GetMachine();
    FootAvatar->SetVisibility(!Machine);
    UpdateDriverAvatar(Machine);
    if (!Machine)
    {
        UpdateFootView(Dt);
        return;
    }
    if (bCab)
    {
        const FTransform Seat = Machine->CabView();
        Camera->SetWorldLocationAndRotation(Seat.GetLocation(),
            Seat.GetRotation() * FRotator(CabPitch, CabYaw, 0).Quaternion());
        Machine->UpdateCabInstruments();
        return;
    }
    const FVector2D Position = GetPosition();
    FVector Focus(Position.X * 100, Position.Y * 100, (Terrain->Height(Position) + 2.1) * 100);
    const double VehicleYaw = Machine ? Machine->Heading : 0;
    const double UpperStructureYaw = Machine && Machine->Kind == EMudMachine::Excavator ? Machine->Swing : 0;
    if (Machine && Machine->Kind == EMudMachine::Excavator)
        Focus += FRotator(0, VehicleYaw + UpperStructureYaw, 0).Vector() * 180;
    const FRotator Rotation(LookPitch, VehicleYaw + LookYaw, 0);
    FVector View = Focus;
    if (Machine)
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
    {
        BenchmarkFrames.Add((Current - BenchmarkLast) * 1000.0);
        BenchmarkSoilTimes.Add(Terrain->SoilStepMilliseconds);
        BenchmarkActiveCells.Add(Terrain->ActiveSoilCells());
    }
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
    FString Csv = TEXT("frame,ms,soil_step_ms,active_cells\n");
    double Sum = 0;
    for (int32 I = 0; I < BenchmarkFrames.Num(); ++I)
    {
        Sum += BenchmarkFrames[I];
        Csv += FString::Printf(TEXT("%d,%.4f,%.5f,%d\n"), I + 1, BenchmarkFrames[I], BenchmarkSoilTimes[I], BenchmarkActiveCells[I]);
    }
    BenchmarkFrames.Sort();
    const double P99 = BenchmarkFrames[FMath::CeilToInt(BenchmarkFrames.Num() * 0.99) - 1];
    const FString Path = FPaths::ProjectSavedDir() / TEXT("Benchmark.csv");
    const FString Summary = FString::Printf(TEXT("Benchmark: %.1f FPS average | %.2f ms p99"),
        1000 * BenchmarkFrames.Num() / FMath::Max(1.0, Sum), P99);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const bool bSaved = FFileHelper::SaveStringToFile(Csv, *Path);
    UE_LOG(LogTemp, Display, TEXT("%s | %s"), *Summary, bSaved ? *Path : TEXT("CSV write failed"));
    const auto Memory=FPlatformMemory::GetStats();
    const auto Size=GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    const auto* RenderScale=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    UE_LOG(LogTemp,Display,TEXT("Benchmark environment: %s, %dx%d, %.1f%% screen percentage, peak process physical %.1f MiB"),
        *Terrain->GetWorldId(),Size.X,Size.Y,RenderScale?RenderScale->GetFloat():0,Memory.PeakUsedPhysical/1048576.0);
    Notify(Summary);
    if (FParse::Param(FCommandLine::Get(), TEXT("MudBenchmarkExit")))
        FPlatformMisc::RequestExit(false);
}

void AMudOperator::MoveMouseX(float Value)
{
    if (Value != 0 && !ApplyMouseDrag(Value, 0))
        PanView(Value * MachineMotion::MouseLookDegreesPerCount, 0);
}

void AMudOperator::MoveMouseY(float Value)
{
    const double Vertical = -static_cast<double>(Value);
    if (Value != 0 && !ApplyMouseDrag(0, Vertical))
        PanView(0, Vertical * MachineMotion::MouseLookDegreesPerCount);
}

bool AMudOperator::ApplyMouseDrag(double X, double Y)
{
    const auto* PC = Cast<APlayerController>(GetController());
    const auto* Machine = GetMachine();
    if (!PC || !Machine)
        return false;
    MachineMotion::MouseTool Tool;
    switch (Machine->Kind)
    {
    case EMudMachine::Excavator: Tool = MachineMotion::MouseTool::Excavator; break;
    case EMudMachine::Dozer: Tool = MachineMotion::MouseTool::Blade; break;
    case EMudMachine::Loader: Tool = MachineMotion::MouseTool::Loader; break;
    default: return false;
    }
    const auto Pressed = [PC](const FKey Key)
    {
        return PC->IsInputKeyDown(Key) || PC->WasInputKeyJustPressed(Key);
    };
    bool bLeft = Pressed(EKeys::LeftMouseButton);
    bool bRight = Pressed(EKeys::RightMouseButton);
    if (!bLeft && !bRight)
    {
        bLeft = PC->WasInputKeyJustReleased(EKeys::LeftMouseButton);
        bRight = PC->WasInputKeyJustReleased(EKeys::RightMouseButton);
    }
    if (!bLeft && !bRight)
        return false;
    const double Sensitivity = MachineMotion::MouseDragSensitivity(Tool);
    MachineMotion::AccumulateMouseDrag(MouseHydraulics, bLeft, bRight, Tool, X * Sensitivity, Y * Sensitivity);
    return true;
}

void AMudOperator::MovePadX(float Value)
{
    PadHydraulicB = Value;
    if (!IsPadHydraulicsActive() && GetWorld())
        PanView(Value * GetWorld()->GetDeltaSeconds() * 96, 0);
}

void AMudOperator::MovePadY(float Value)
{
    PadHydraulicA = Value;
    if (!IsPadHydraulicsActive() && GetWorld())
        PanView(0, Value * GetWorld()->GetDeltaSeconds() * 72);
}

bool AMudOperator::IsPadHydraulicsActive() const
{
    const auto* Machine = GetMachine();
    return Machine && (bPadHydraulics || (bPadSecondHydraulics && Machine->Kind == EMudMachine::Excavator));
}

void AMudOperator::PadSecondHydraulicsOn()
{
    bPadSecondHydraulics = true;
    if (const auto* Machine = GetMachine(); Machine && Machine->Kind != EMudMachine::Excavator)
        ChangeGroup();
}

void AMudOperator::PanView(double Yaw, double Pitch)
{
    if (bCab)
    {
        CabYaw = FMath::Clamp(CabYaw + Yaw, -115.0, 115.0);
        CabPitch = FMath::Clamp(CabPitch + Pitch, -67.0, 38.0);
        return;
    }
    LookYaw = FRotator::NormalizeAxis(LookYaw + Yaw);
    LookPitch = GetMachine() ? FMath::Clamp(LookPitch + Pitch, -76.0, 34.0)
        : FMath::Clamp(LookPitch + Pitch, -58.0, 12.0);
}

void AMudOperator::SwitchCamera()
{
    if (!GetMachine())
        return;
    bCab = !bCab;
    CabYaw = 0;
    CabPitch = -15;
    Camera->SetFieldOfView(bCab ? 92.f : 80.f);
    Audio->Interface(TEXT("Switch"));
}

void AMudOperator::SwitchMap()
{
    bShowMap = !bShowMap;
    Audio->Interface(TEXT("Switch"));
}

void AMudOperator::SwitchHelp()
{
    bShowHelp = !bShowHelp;
    Audio->Interface(TEXT("Switch"));
}

void AMudOperator::Zoom(float Value)
{
    if (GetMachine())
        Distance = FMath::Clamp(Distance - Value * 1.4, 5.0, 33.0);
    else
        FootDistance = FMath::Clamp(FootDistance - Value * 0.45, 2.4, 6.5);
}

void AMudOperator::EnterExit()
{
    VerticalTap = HorizontalTap = {};
    if (!Terrain || bInitializeFailed)
        return;
    MouseHydraulics = {};
    if (auto* Machine = GetMachine())
    {
        const TOptional<FVector2D> Exit = ExitPosition(*Machine);
        if (!Exit.IsSet())
        {
            Notify(TEXT("No room to step out. Move the machine first."));
            return;
        }
        Machine->Halt();
        Audio->Play(TEXT("Door"), Machine->CabView().GetLocation(), 0.62f, 0.94f);
        if (bCab)
            SwitchCamera();
        OnFoot = Exit.GetValue();
        LookYaw = FRotator::NormalizeAxis(LookYaw + Machine->Heading);
        LookPitch = -12;
        Active = INDEX_NONE;
        ResetFootMotion(Machine->Heading);
        bDepotLoad = bCruise = bBrake = false;
        UpdateCamera();
        return;
    }
    if (!bFootGrounded)
        return;
    double Best = FMath::Square(3.35);
    int32 Entry = INDEX_NONE;
    for (int32 Id = 0; Id < Fleet.Num(); ++Id)
    {
        const double Dist = FVector2D::DistSquared(OnFoot, XY(Fleet[Id]->CabView().GetLocation()) / 100.0);
        if (Dist < Best)
        {
            Entry = Id;
            Best = Dist;
        }
    }
    if (Entry != INDEX_NONE)
    {
        Active = Entry;
        const auto* Machine = GetMachine();
        Audio->Play(TEXT("Door"), Machine->CabView().GetLocation(), 0.72f, 1.04f);
        LookYaw = FRotator::NormalizeAxis(LookYaw - Machine->Heading);
        LookPitch = -21;
        bBrake = false;
        UpdateCamera();
    }
}

void AMudOperator::SwapVehicle()
{
    VerticalTap = HorizontalTap = {};
    if (Fleet.IsEmpty())
        return;
    if (auto* Old = GetMachine())
        Old->Halt();
    MouseHydraulics = {};
    Active = (Active + 1) % Fleet.Num();
    Audio->Interface(TEXT("Switch"));
    CabYaw = 0;
    CabPitch = -15;
    LookYaw = GetMachine()->Kind == EMudMachine::Excavator ? -55 : -35;
    LookPitch = GetMachine()->Kind == EMudMachine::Excavator ? -27 : -21;
    bDepotLoad = bCruise = false;
    UpdateCamera();
}

void AMudOperator::WorkTool()
{
    if (auto* Machine = GetMachine())
    {
        Machine->ToggleTool();
        Audio->Interface(TEXT("Switch"));
    }
}

void AMudOperator::ChangeGroup()
{
    if (auto* Machine = GetMachine())
    {
        Machine->ChangeGroup();
        Audio->Interface(TEXT("Switch"));
    }
}
void AMudOperator::Unload()
{
    if (auto* Machine = GetMachine())
    {
        Machine->ToggleUnload();
        bDepotLoad = false;
        Audio->Interface(TEXT("Switch"));
    }
}

void AMudOperator::Recover()
{
    if (!Terrain || bInitializeFailed)
        return;
    auto* Machine = GetMachine();
    if (!Machine)
    {
        Notify(TEXT("Enter a vehicle to recover it."));
        return;
    }

    const FVector2D Parking = XY(Terrain->Start) + ParkingOffsets[Active];
    const auto IsClear = [&](FVector2D Position)
    {
        constexpr double Radius = 1.85;
        const double Limit = Terrain->GetWorldSize() - 6;
        if (Position.X < 6 || Position.Y < 6 || Position.X > Limit || Position.Y > Limit)
            return false;
        for (double Along : {-3.7, 0.0, 3.7})
        {
            const FVector2D Probe = Position + FVector2D(Along, 0);
            if (Terrain->WaterDepth(Probe) > 0.15 || Terrain->Normal(Probe).Z < 0.92 ||
                Terrain->HitsTree(Probe, Radius, false))
                return false;
            const double Height = Terrain->Height(Probe);
            for (const auto* Other : Fleet)
                if (Other != Machine && Other->BlocksPedestrian(Probe, Height, Radius))
                    return false;
        }
        return true;
    };

    FVector2D Destination = Parking;
    if (!IsClear(Destination))
    {
        // Try the closest positions first when another machine is parked in the usual spot.
        bool bFound = false;
        for (const FVector2D Offset : {FVector2D(10, 0), FVector2D(-10, 0), FVector2D(0, 8), FVector2D(0, -8),
            FVector2D(20, 0), FVector2D(-20, 0), FVector2D(0, 16), FVector2D(0, -16)})
        {
            Destination = Parking + Offset;
            if (IsClear(Destination))
            {
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            Notify(TEXT("No clear recovery spot at the start yard."));
            return;
        }
    }

    Machine->StopWork();
    Machine->Position = Destination;
    Machine->Heading = 0;
    Machine->RolledSide = 0;
    Machine->Boom = Machine->Kind == EMudMachine::Loader ? 5 : 15;
    Machine->Stick = -28;
    Machine->Curl = Machine->BucketTilt = Machine->Swing = Machine->Blade = 0;
    bDepotLoad = bCruise = bBrake = false;
    VerticalTap = HorizontalTap = {};
    MouseHydraulics = {};
    Machine->UpdatePose(*Terrain);
    Terrain->UpdateTiles(Destination);
    UpdateCamera();
    Notify(TEXT("Current vehicle recovered to the start yard."));
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
    if (Message != Text)
        Audio->Interface(TEXT("Notice"));
    Message = Text;
    MessageTime = 4;
}

FString AMudOperator::MissionText() const
{
    if (Terrain && Terrain->IsSoilLab())
        return TEXT("Soil lab: soil x150, sand x210, wet clay x250, gravel x285, rock x310. Y800. 9 for diagnostics.");
    if (bInitializeFailed)
        return TEXT("The valley could not be loaded. Rebuild the project assets.");
    if (bFinished)
        return TEXT("Delivery complete. Keep working the valley.");
    if (!Terrain->IsRoadReady())
        return TEXT("Build a gravel road at a marked ford. Level it with the dozer. Press 9 to survey.");
    if (bDelivery)
        return TEXT("Deliver the loaded truck to the red marker.");
    if (Terrain->GetWorldId().StartsWith(TEXT("earthworks-v")))
        return TEXT("Cut a route through the earth bank, fill the ravine and reinforce soft ground. Load the truck at the yellow yard; deliver to red.");
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
    if (bShowMap && Terrain->IsSoilLab())
    {
        double Balance = Terrain->MaterialBalance();
        for (const auto* Vehicle : Fleet) Balance += Vehicle->Cargo.Mass();
        Info += FString::Printf(TEXT(" | %d active | %.2f ms soil | balance %.6f t"),
            Terrain->ActiveSoilCells(), Terrain->SoilStepMilliseconds, Balance);
    }
    if (bShowMap && !Terrain->HasRoadContract())
    {
        for (int32 I = 0; I < Terrain->Fords.Num(); ++I)
        {
            const FVector2D Ford = Terrain->Fords[I];
            const double Fill = Terrain->EstimateFordFill(Ford, 0.44);
            Info += FString::Printf(TEXT(" | %.0f m: ~%.0f m3 fill"), FVector2D::Distance(Position, Ford), Fill);
        }
    }
    return Info;
}

FString AMudOperator::StatusText() const { return MessageTime > 0 ? Message : FString(); }

void AMudOperator::Save()
{
    if (WriteProgress())
    {
        bProtectSave = false;
        if (NextAutosave > 0)
            NextAutosave = FPlatformTime::Seconds() + AutosaveInterval;
        Notify(TEXT("Progress saved."));
    }
    else
        Notify(TEXT("Could not save progress."));
}

bool AMudOperator::WriteProgress() const
{
    if (!Terrain || bInitializeFailed || Fleet.Num() != UE_ARRAY_COUNT(ParkingOffsets))
        return false;
    const auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("version"), SaveVersion);
    Root->SetNumberField(TEXT("seed"), Terrain->GetSeed());
    Root->SetNumberField(TEXT("active"), Active);
    Root->SetNumberField(TEXT("footX"), OnFoot.X);
    Root->SetNumberField(TEXT("footY"), OnFoot.Y);
    Root->SetNumberField(TEXT("footFacing"), FootFacing);
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
        if (Machine->ArticulationLimit() > 0)
            Row->SetNumberField(TEXT("articulation"), Machine->GetArticulation());
        Row->SetNumberField(TEXT("boom"), Machine->Boom);
        Row->SetNumberField(TEXT("stick"), Machine->Stick);
        Row->SetNumberField(TEXT("curl"), Machine->Curl);
        Row->SetNumberField(TEXT("bucketTilt"), Machine->BucketTilt);
        Row->SetNumberField(TEXT("swing"), Machine->Swing);
        Row->SetNumberField(TEXT("blade"), Machine->Blade);
        Row->SetNumberField(TEXT("bladeYaw"), Machine->BladeYaw);
        Row->SetNumberField(TEXT("rolled"), Machine->RolledSide);
        TArray<TSharedPtr<FJsonValue>> Cargo;
        for (const double Volume : Machine->Cargo.Volumes)
            Cargo.Add(MakeShared<FJsonValueNumber>(Volume));
        Row->SetArrayField(TEXT("cargo"), Cargo);
        Vehicles.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("vehicles"), Vehicles);
    FString Json;
    const FString Path = SavePath();
    const FString Temporary = FString::Printf(TEXT("%s.%u.tmp"), *Path, FPlatformProcess::GetCurrentProcessId());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const auto Writer = TJsonWriterFactory<>::Create(&Json);
    if (FJsonSerializer::Serialize(Root, Writer, true) && FFileHelper::SaveStringToFile(Json, *Temporary))
    {
#if PLATFORM_WINDOWS
        // Replace the old save in place only after the new file has been written in full.
        const FString Source = FPaths::ConvertRelativePathToFull(Temporary).Replace(TEXT("/"), TEXT("\\"));
        const FString Destination = FPaths::ConvertRelativePathToFull(Path).Replace(TEXT("/"), TEXT("\\"));
        if (::MoveFileExW(*Source, *Destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
#else
        if (IFileManager::Get().Move(*Path, *Temporary, true, false, false, true))
            return true;
#endif
    }
    IFileManager::Get().Delete(*Temporary, false, false, true);
    UE_LOG(LogTemp, Warning, TEXT("Could not write the valley profile to %s"), *Path);
    return false;
}

void AMudOperator::Load()
{
    if (RestoreProgress())
    {
        bProtectSave = false;
        if (NextAutosave > 0)
            NextAutosave = FPlatformTime::Seconds() + AutosaveInterval;
    }
}

bool AMudOperator::RestoreProgress()
{
    if (!Terrain || bInitializeFailed)
        return false;
    FString Json;
    TSharedPtr<FJsonObject> Root;
    int32 Version = 0, Seed = 0, Chosen = INDEX_NONE;
    const TArray<TSharedPtr<FJsonValue>>* Vehicles = nullptr;
    const TSharedPtr<FJsonObject>* TerrainData = nullptr;
    if (!FFileHelper::LoadFileToString(Json, *LoadPath()) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() ||
        !Root->TryGetNumberField(TEXT("version"), Version) || (Version != SaveVersion && Version != 2) ||
        !Root->TryGetNumberField(TEXT("seed"), Seed) || Seed != Terrain->GetSeed() ||
        !Root->TryGetNumberField(TEXT("active"), Chosen) || (Chosen != INDEX_NONE && !Fleet.IsValidIndex(Chosen)) ||
        !Root->TryGetArrayField(TEXT("vehicles"), Vehicles) ||
        Vehicles->Num() != (Version == 2 ? LegacyVehicleCount : Fleet.Num()) ||
        Chosen >= Vehicles->Num() ||
        !Root->TryGetObjectField(TEXT("terrain"), TerrainData))
    {
        Notify(TEXT("No compatible save found."));
        return false;
    }
    struct FRestore
    {
        FVector2D XY;
        double Heading, Articulation, Boom, Stick, Curl, BucketTilt, Swing, Blade, BladeYaw;
        int32 RolledSide;
        Earthwork::Load Cargo;
    };
    TArray<FRestore> Machines;
    const auto ValidPosition = [this](FVector2D Point)
    {
        return Point.X >= 0 && Point.Y >= 0 && Point.X <= Terrain->GetWorldSize() && Point.Y <= Terrain->GetWorldSize();
    };
    for (int32 Id = 0; Id < Vehicles->Num(); ++Id)
    {
        const TSharedPtr<FJsonObject>* Row;
        const TArray<TSharedPtr<FJsonValue>>* Cargo;
        FRestore Entry{};
        if (!(*Vehicles)[Id]->TryGetObject(Row) ||
            !(*Row)->TryGetNumberField(TEXT("x"), Entry.XY.X) || !(*Row)->TryGetNumberField(TEXT("y"), Entry.XY.Y) ||
            !ValidPosition(Entry.XY) || !(*Row)->TryGetNumberField(TEXT("heading"), Entry.Heading) ||
            ((*Row)->HasField(TEXT("articulation")) && (!(*Row)->TryGetNumberField(TEXT("articulation"), Entry.Articulation) ||
                !FMath::IsFinite(Entry.Articulation) || FMath::Abs(Entry.Articulation) > Fleet[Id]->ArticulationLimit())) ||
            !(*Row)->TryGetNumberField(TEXT("boom"), Entry.Boom) || !(*Row)->TryGetNumberField(TEXT("stick"), Entry.Stick) ||
            !(*Row)->TryGetNumberField(TEXT("curl"), Entry.Curl) || !(*Row)->TryGetNumberField(TEXT("swing"), Entry.Swing) ||
            ((*Row)->HasField(TEXT("bucketTilt")) && (!(*Row)->TryGetNumberField(TEXT("bucketTilt"), Entry.BucketTilt) ||
                !FMath::IsFinite(Entry.BucketTilt) || FMath::Abs(Entry.BucketTilt) > MachineMotion::MaxBucketTilt)) ||
            !(*Row)->TryGetNumberField(TEXT("blade"), Entry.Blade) ||
            !(*Row)->TryGetNumberField(TEXT("rolled"), Entry.RolledSide) || FMath::Abs(Entry.RolledSide) > 1 ||
            !(*Row)->TryGetArrayField(TEXT("cargo"), Cargo) || (Cargo->Num() != 3 && Cargo->Num() != MaterialCount))
        {
            Notify(TEXT("Vehicle data in the save is invalid."));
            return false;
        }
        for (int32 M = 0; M < Cargo->Num(); ++M)
        {
            if (!(*Cargo)[M]->TryGetNumber(Entry.Cargo.Volumes[M]) ||
                !FMath::IsFinite(Entry.Cargo.Volumes[M]) || Entry.Cargo.Volumes[M] < 0 ||
                Entry.Cargo.Volumes[M] > Fleet[Id]->Capacity())
            {
                Notify(TEXT("Invalid cargo in the save."));
                return false;
            }
        }
        if (Entry.Cargo.Volume() > Fleet[Id]->Capacity() + Epsilon)
        {
            Notify(TEXT("Cargo exceeds vehicle capacity."));
            return false;
        }
        if ((*Row)->HasField(TEXT("bladeYaw")) && (!(*Row)->TryGetNumberField(TEXT("bladeYaw"), Entry.BladeYaw) ||
            !FMath::IsFinite(Entry.BladeYaw) || FMath::Abs(Entry.BladeYaw) > 25))
            return false;
        Machines.Add(Entry);
    }
    if (Version == 2)
    {
        FRestore Loader{};
        Loader.XY = XY(Terrain->Start) + ParkingOffsets[LegacyVehicleCount];
        Loader.Boom = 5;
        Machines.Add(Loader);
    }
    FVector2D Foot;
    double SavedFacing = LookYaw;
    Root->TryGetNumberField(TEXT("footFacing"), SavedFacing);
    bool bSavedDelivery, bSavedComplete;
    if (!Root->TryGetNumberField(TEXT("footX"), Foot.X) || !Root->TryGetNumberField(TEXT("footY"), Foot.Y) ||
        !ValidPosition(Foot) || !Root->TryGetBoolField(TEXT("delivery"), bSavedDelivery) ||
        !Root->TryGetBoolField(TEXT("complete"), bSavedComplete) || !Terrain->RestoreTerrain(*TerrainData))
    {
        Notify(TEXT("Terrain data in the save is invalid."));
        return false;
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
        Machine->BucketTilt = Entry.BucketTilt;
        Machine->Swing = Entry.Swing;
        Machine->Blade = Entry.Blade;
        Machine->BladeYaw = Entry.BladeYaw;
        Machine->RolledSide = Entry.RolledSide;
        Machine->Cargo = Entry.Cargo;
        Machine->StopWork();
        Machine->RestoreArticulation(Entry.Articulation);
        Machine->bOtherGroup = Machine->bGrade = false;
        Machine->UpdatePose(*Terrain);
    }
    if (Chosen == INDEX_NONE && bCab)
        SwitchCamera();
    Active = Chosen;
    OnFoot = Foot;
    ResetFootMotion(FMath::IsFinite(SavedFacing) ? SavedFacing : LookYaw);
    if (!GetMachine())
        LookPitch = -12;
    UpdateCamera();
    bDelivery = bSavedDelivery;
    bFinished = bSavedComplete;
    bDepotLoad = bCruise = false;
    Notify(TEXT("Progress restored."));
    return true;
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
    ApplyQualityMode(false);
}

void AMudOperator::ApplyQualityMode(bool bUseDesktop)
{
    auto* Settings = GEngine->GetGameUserSettings();
    if (bUseDesktop)
    {
        Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
        Settings->SetScreenResolution(Settings->GetDesktopResolution());
    }
    Settings->SetOverallScalabilityLevel(2);
    Settings->SetTextureQuality(3);
    Settings->SetAntiAliasingQuality(3);
    Settings->SetDynamicResolutionEnabled(false);
    Settings->SetResolutionScaleValueEx(100);
    Settings->SetFrameRateLimit(120);
    Settings->SetVSyncEnabled(false);
    if (bUseDesktop)
    {
        Settings->ApplyResolutionSettings(false);
        Settings->ApplyNonResolutionSettings();
    }
    else
        Settings->ApplySettings(false);
    const FIntPoint Resolution = Settings->GetFullscreenMode() == EWindowMode::WindowedFullscreen ?
        Settings->GetDesktopResolution() : Settings->GetScreenResolution();
    const auto* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    const auto* Anisotropy = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MaxAnisotropy"));
    UE_LOG(LogTemp, Display, TEXT("Mud quality mode: %dx%d output, %.0f%% render scale, anisotropy %d"),
        Resolution.X, Resolution.Y, ScreenPercentage ? ScreenPercentage->GetFloat() : 0,
        Anisotropy ? Anisotropy->GetInt() : 0);
    Notify(FString::Printf(TEXT("Quality: %d x %d, 100%% render scale."), Resolution.X, Resolution.Y));
}

void AMudOperator::ToggleFullscreen()
{
    const auto* Viewport = GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
    if (!Viewport)
        return;

    auto* Settings = GEngine->GetGameUserSettings();
    const bool bFullscreen = Viewport->IsFullscreen();
    const FIntPoint Desktop = Settings->GetDesktopResolution();
    if (bFullscreen)
    {
        const FIntPoint DefaultWindow(1600, 900);
        const FIntPoint Window = WindowedSize.X > 0 && WindowedSize.Y > 0 ? WindowedSize : DefaultWindow;
        Settings->SetScreenResolution(FIntPoint(
            FMath::Min(Window.X, FMath::Max(1, Desktop.X - 80)),
            FMath::Min(Window.Y, FMath::Max(1, Desktop.Y - 100))));
        Settings->SetFullscreenMode(EWindowMode::Windowed);
    }
    else
    {
        WindowedSize = Viewport->GetSizeXY();
        Settings->SetScreenResolution(Desktop);
        Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
    }
    Settings->ApplyResolutionSettings(false);
    Settings->ConfirmVideoMode();
    Settings->SaveSettings();
    Notify(bFullscreen ? TEXT("Windowed") : TEXT("Fullscreen at desktop resolution"));
}

void AMudOperator::Quit() { UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false); }

void AMudOperator::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("Throttle"), this, &AMudOperator::SetThrottle);
    Input->BindAxis(TEXT("Steering"), this, &AMudOperator::SetSteering);
    Input->BindAxis(TEXT("FootForward"), this, &AMudOperator::SetFootPadForward);
    Input->BindAxis(TEXT("LookX"), this, &AMudOperator::MoveMouseX);
    Input->BindAxis(TEXT("LookY"), this, &AMudOperator::MoveMouseY);
    Input->BindAxis(TEXT("PadX"), this, &AMudOperator::MovePadX);
    Input->BindAxis(TEXT("PadY"), this, &AMudOperator::MovePadY);
    Input->BindAxis(TEXT("HydraulicA"), this, &AMudOperator::SetHydraulicA);
    Input->BindAxis(TEXT("HydraulicB"), this, &AMudOperator::SetHydraulicB);
    Input->BindAxis(TEXT("HydraulicKeyA"), this, &AMudOperator::SetHydraulicKeyA);
    Input->BindAxis(TEXT("HydraulicKeyB"), this, &AMudOperator::SetHydraulicKeyB);
    Input->BindAction(TEXT("ToolUpTap"), IE_Pressed, this, &AMudOperator::ToolUpTap).bConsumeInput = false;
    Input->BindAction(TEXT("ToolDownTap"), IE_Pressed, this, &AMudOperator::ToolDownTap).bConsumeInput = false;
    Input->BindAction(TEXT("ToolLeftTap"), IE_Pressed, this, &AMudOperator::ToolLeftTap).bConsumeInput = false;
    Input->BindAction(TEXT("ToolRightTap"), IE_Pressed, this, &AMudOperator::ToolRightTap).bConsumeInput = false;
    Input->BindAxis(TEXT("Zoom"), this, &AMudOperator::Zoom);
    Input->BindAction(TEXT("EnterExit"), IE_Pressed, this, &AMudOperator::EnterExit);
    Input->BindAction(TEXT("Swap"), IE_Pressed, this, &AMudOperator::SwapVehicle);
    Input->BindAction(TEXT("Camera"), IE_Pressed, this, &AMudOperator::SwitchCamera);
    Input->BindAction(TEXT("Map"), IE_Pressed, this, &AMudOperator::SwitchMap);
    Input->BindAction(TEXT("Help"), IE_Pressed, this, &AMudOperator::SwitchHelp);
    Input->BindAction(TEXT("PadHydraulics"), IE_Pressed, this, &AMudOperator::PadHydraulicsOn);
    Input->BindAction(TEXT("PadHydraulics"), IE_Released, this, &AMudOperator::PadHydraulicsOff);
    Input->BindAction(TEXT("PadSecondHydraulics"), IE_Pressed, this, &AMudOperator::PadSecondHydraulicsOn);
    Input->BindAction(TEXT("PadSecondHydraulics"), IE_Released, this, &AMudOperator::PadSecondHydraulicsOff);
    Input->BindAction(TEXT("Brake"), IE_Pressed, this, &AMudOperator::BrakeOn);
    Input->BindAction(TEXT("Brake"), IE_Released, this, &AMudOperator::BrakeOff);
    Input->BindAction(TEXT("Sprint"), IE_Pressed, this, &AMudOperator::SprintOn);
    Input->BindAction(TEXT("Sprint"), IE_Released, this, &AMudOperator::SprintOff);
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
    // Bind the physical key so older saved "Quit" mappings cannot bypass the menu.
    Input->BindKey(EKeys::Escape, IE_Pressed, this, &AMudOperator::TogglePauseMenu).bExecuteWhenPaused = true;
    auto& MenuClick = Input->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AMudOperator::ClickPauseMenu);
    MenuClick.bExecuteWhenPaused = true;
    MenuClick.bConsumeInput = false;
}

void AMudHUD::DrawHUD()
{
    Super::DrawHUD();
    const auto* Operator = Cast<AMudOperator>(GetOwningPawn());
    if (!Operator || !Canvas)
        return;
    const auto* Terrain = Operator->GetTerrain();
    const auto* Machine = Operator->GetMachine();
    const bool bInCab = Machine && Operator->IsCabView();
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

    if (Operator->IsPauseMenuOpen())
    {
        Rect(FLinearColor(0, 0, 0, 0.65f), 0, 0, W, H);
        const float CenterX = W * 0.5f, CenterY = H * 0.5f;
        Rect(Panel, CenterX - 210, CenterY - 180, 420, 360);
        Label(TEXT("Pausat"), Gold, CenterX - 180, CenterY - 148, 2.2f);
        float MouseX = -1, MouseY = -1;
        GetOwningPlayerController()->GetMousePosition(MouseX, MouseY);
        const TCHAR* Buttons[] = {TEXT("Fortsätt"), TEXT("Spara världen"), TEXT("Spara och avsluta")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(Buttons); ++I)
        {
            const FBox2D Bounds = AMudOperator::PauseButtonBounds(FVector2D(Canvas->SizeX, Canvas->SizeY), I);
            const bool bHover = Bounds.IsInside(FVector2D(MouseX, MouseY));
            Rect(bHover ? FLinearColor(0.25f, 0.37f, 0.29f) : FLinearColor(0.12f, 0.19f, 0.16f),
                Bounds.Min.X / Scale, Bounds.Min.Y / Scale, Bounds.GetSize().X / Scale, Bounds.GetSize().Y / Scale);
            Label(Buttons[I], bHover ? Gold : Text, Bounds.Min.X / Scale + 18, Bounds.Min.Y / Scale + 17, 1.2f);
        }
        Label(Operator->StatusText().IsEmpty() ? TEXT("Esc för att fortsätta") : Operator->StatusText(),
            Muted, CenterX - 180, CenterY + 140, 0.94f);
        return;
    }

    if (!bInCab || Operator->bShowMap)
    {
        Rect(Panel, 22, 22, 600, 80);
        Rect(Gold, 22, 22, 3, 80);
        Label(TEXT("VALLEY ROAD  /  DELIVERY CONTRACT"), Gold, 38, 33, 1.15);
        Label(Operator->MissionText(), Text, 38, 61);
        Label(Operator->SurveyText(), Muted, 38, 81, 0.90);
    }
    if (!Terrain)
        return;

    const FLinearColor ReadyColor(0.32f, 0.86f, 0.43f);
    const FLinearColor DeepColor(0.35f, 0.72f, 0.98f);
    if (Operator->bShowMap && Terrain->HasRoadContract())
    {
        const FVector2D Here = Operator->GetPosition();
        const float CardWidth = FMath::Clamp(W * 0.44f - 32.f, 258.f, 408.f);
        float Y = 113.f;
        for (int32 I = 0; I < Terrain->Fords.Num(); ++I)
        {
            const auto* Road = Terrain->GetRoadSurvey(I);
            if (!Road || Road->Sections.IsEmpty())
                continue;

            const auto Color = Road->IsReady() ? ReadyColor : Text;
            const TCHAR* Name = Terrain->Fords[I].Y > Terrain->GetWorldSize() * 0.5 ? TEXT("South ford") : TEXT("North ford");
            Rect(Panel, 22, Y, CardWidth, 130);
            Label(FString::Printf(TEXT("%s   %d%%"), Name, Road->Progress()), Color, 38, Y + 10, 1.10);
            if (Road->IsReady())
                Label(TEXT("Ready for the loaded truck"), ReadyColor, 38, Y + 34, 0.94);
            else
            {
                Label(FString::Printf(TEXT("Raise streambed   %d m"), Road->Deep), Road->Deep ? DeepColor : Muted, 38, Y + 34, 0.94);
                Label(FString::Printf(TEXT("Add gravel   %d m"), Road->Soft), Road->Soft ? Gold : Muted, 38, Y + 51, 0.94);
                Label(FString::Printf(TEXT("Level with dozer   %d m"), Road->Rough), Road->Rough ? Red : Muted, 38, Y + 68, 0.94);
                if (Road->Fill > 0)
                    Label(FString::Printf(TEXT("~%.1f m3 fill"), Road->Fill), Muted, 22 + CardWidth - 96, Y + 10, 0.90);
            }

            const float Left = 56.f, StripWidth = CardWidth - 68.f;
            const float Step = StripWidth / Road->Sections.Num();
            for (int32 S = 0; S < Road->Sections.Num(); ++S)
            {
                const auto& Section = Road->Sections[S];
                const float X = Left + S * Step;
                Rect(Section.bDeep ? DeepColor : Panel, X, Y + 92, Step, 5);
                Rect(Section.bSoft ? Gold : Panel, X, Y + 98, Step, 5);
                Rect(Section.bRough ? Red : Panel, X, Y + 104, Step, 5);
                if (Section.IsReady())
                    Rect(ReadyColor, X, Y + 92, Step, 17);
            }
            Label(TEXT("W"), Muted, 38, Y + 93, 0.85);
            Label(TEXT("E"), Muted, Left + StripWidth + 6, Y + 93, 0.85);
            const FVector2D Offset = Here - Road->Start;
            if (FMath::Abs(Offset.Y) < 20 && Offset.X >= -0.5 && Offset.X < Road->Sections.Num() - 0.5)
                Rect(FLinearColor::White, Left + (Offset.X + 0.5) * Step - 1, Y + 111, 2, 7);
            Y += 138;
        }
    }

    if (!Machine)
    {
        if (Operator->bShowHelp)
        {
            Rect(Panel, W - 350, H - 77, 328, 55);
            Label(TEXT("WASD  Move   Shift  Sprint   Space  Jump"), Text, W - 336, H - 67, 0.91);
            Label(TEXT("E  Enter   Tab  Switch vehicle   Wheel  Zoom"), Muted, W - 336, H - 45, 0.91);
        }
        if (!Operator->bShowMap)
            return;
    }

    if (Machine)
    {
        const auto Status = Operator->StatusText();
        if (bInCab && !Operator->bShowMap)
        {
            Rect(Panel, 22, H - 88, 295, 66);
            const bool bEarthmover = Machine->Kind == EMudMachine::Excavator ||
                Machine->Kind == EMudMachine::Dozer || Machine->Kind == EMudMachine::Loader;
            const FVector Tip = Machine->ToolPoint();
            const double Clearance = Tip.Z - Terrain->Height(XY(Tip));
            const FString WorkReading = bEarthmover ?
                FString::Printf(TEXT("Edge vs ground  %+.2f m"), Clearance) :
                FString::Printf(TEXT("Bed  %.1f / %.1f m3"), Machine->Cargo.Volume(), Machine->Capacity());
            Label(WorkReading, Text, 35, H - 79);
            Label(Machine->ToolDescription(), Machine->IsMovingEarth() ? Gold : Muted, 35, H - 58, 0.94);
            if (Operator->bShowHelp)
            {
                Label(ArrowHelp(*Machine), Muted, 35, H - 39, 0.85);
            }
            if (!Status.IsEmpty())
            {
                Rect(Panel, 22, H - 127, 495, 31);
                Label(Status, Gold, 35, H - 120);
            }
            return;
        }

        Rect(Panel, 22, H - 154, 322, 132);
        Label(Machine->Name(), Text, 38, H - 143, 1.24);
        Label(Machine->ToolDescription(), Machine->IsMovingEarth() ? Gold : Muted, 38, H - 119);
        Label(ArrowHelp(*Machine), Text, 38, H - 99, 0.91);
        Label(FString::Printf(TEXT("%02.0f km/h   |   %02.0f%% grip   |   %4.1f t payload"),
            FMath::Abs(Machine->Speed) * 3.6, Machine->Grip * 100, Machine->Cargo.Mass()), Text, 38, H - 76);
        Rect(FLinearColor(0.13, 0.19, 0.17), 38, H - 48, 138, 6);
        Rect(Gold, 38, H - 48, 138 * Machine->Cargo.Volume() / Machine->Capacity(), 6);
        Label(FString::Printf(TEXT("%.1f / %.1f m3"), Machine->Cargo.Volume(), Machine->Capacity()), Text, 190, H - 55);
        if (!Status.IsEmpty())
        {
            Rect(Panel, 22, H - 196, 495, 32);
            Label(Status, Gold, 36, H - 188);
        }

        if (!Operator->bShowMap && (Machine->bTool || Machine->IsTipping() ||
            (Operator->bShowHelp && (Machine->Kind == EMudMachine::Excavator || Machine->Kind == EMudMachine::Loader))))
        {
            const FVector Lip = Machine->ToolPoint();
            const FVector Ground(Lip.X, Lip.Y, Terrain->Height(XY(Lip)) + 0.035);
            FVector2D Pixel;
            if (const auto* PC = GetOwningPlayerController(); PC && PC->ProjectWorldLocationToScreen(Ground * 100, Pixel))
            {
                const FLinearColor Indicator = Machine->IsMovingEarth() ? FLinearColor(0.55, 0.89, 0.56) : Gold;
                const double Radius = 7 * Scale;
                DrawLine(Pixel.X - Radius, Pixel.Y, Pixel.X, Pixel.Y - Radius, Indicator, 1.5 * Scale);
                DrawLine(Pixel.X, Pixel.Y - Radius, Pixel.X + Radius, Pixel.Y, Indicator, 1.5 * Scale);
                DrawLine(Pixel.X + Radius, Pixel.Y, Pixel.X, Pixel.Y + Radius, Indicator, 1.5 * Scale);
                DrawLine(Pixel.X, Pixel.Y + Radius, Pixel.X - Radius, Pixel.Y, Indicator, 1.5 * Scale);
            }
        }

        const float FrameMs = Operator->FrameSeconds * 1000;
        Rect(Panel, W - 188, 22, 166, 30);
        Label(FString::Printf(TEXT("%.0f FPS   /   %.2f ms"), 1000 / FMath::Max(FrameMs, 0.01f), FrameMs), Muted, W - 174, 29);
        if (Operator->bShowHelp)
        {
            Rect(Panel, W - 368, H - 156, 346, 134);
            Label(TEXT("WASD  Drive    E  Enter / exit    Tab  Next vehicle"), Text, W - 353, H - 143);
            const TCHAR* WorkHelp = Machine->Kind == EMudMachine::Excavator ? TEXT("V  Dig    I  Tip    Both mouse buttons: tilt") :
                (Machine->Kind == EMudMachine::Dozer ? TEXT("I  Spread    J/K  Blade") :
                    (Machine->Kind == EMudMachine::Loader ? TEXT("Lower bucket and drive into the pile") : TEXT("R  Load at a pile    I  Auto tip")));
            Label(WorkHelp, Text, W - 353, H - 122);
            const TCHAR* MouseHelp = Machine->Kind == EMudMachine::Excavator ?
                TEXT("Hold LMB  Boom / bucket    RMB  Stick / swing") :
                (Machine->Kind == EMudMachine::Dozer ? TEXT("Hold mouse + drag up / down  Blade") :
                    (Machine->Kind == EMudMachine::Loader ? TEXT("LMB up/down  Lift    LMB left/right or RMB up/down  Bucket") : TEXT("Mouse  Look around")));
            Label(MouseHelp, Text, W - 353, H - 101);
            Label(TEXT("C  Camera     9  Survey     Backspace  Recover"), Text, W - 353, H - 80);
            Label(TEXT("F5 / F9  Save / load    F7 / F8  Graphics"), Text, W - 353, H - 59);
            Label(TEXT("F1  Controls     F6  Fullscreen     Esc  Menu"), Muted, W - 353, H - 39);
        }
    }

    const float MapSize = Operator->bShowMap ? FMath::Clamp(FMath::Min(H - 230, W * 0.52f - 26), 180.f, 500.f) : 180.f;
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
    for (int32 I = 0; I < Terrain->Fords.Num(); ++I)
    {
        const auto* Road = Terrain->GetRoadSurvey(I);
        const FLinearColor Color = Road && Road->IsReady() ? ReadyColor : DeepColor;
        if (Operator->bShowMap && Road && !Road->Sections.IsEmpty())
        {
            const FVector2D Pixel = Road->Start * (MapSize / Terrain->GetWorldSize()) + FVector2D(MapX, MapY);
            Rect(Color, Pixel.X, Pixel.Y - 1, Road->Sections.Num() * MapSize / Terrain->GetWorldSize(), 2);
        }
        else
            MapPoint(Terrain->Fords[I], Color, 4);
    }
    for (const auto* Vehicle : Operator->GetFleet())
        MapPoint(Vehicle->Position, Vehicle == Machine ? FLinearColor::White : Gold, Vehicle == Machine ? 6 : 3);
    if (Operator->bShowMap)
    {
        MapPoint(Operator->GetPosition(), FLinearColor::White, 6);
        for (const auto& Pile : Terrain->Piles)
        {
            const FVector2D Point = MapPoint(Pile.Center, Text, 5);
            Label(FString::Printf(TEXT("%s %.0f m3"), MaterialName(Pile.Material), Terrain->Remaining(Pile)),
                Text, Point.X + 5, Point.Y - 8);
        }
    }
}
