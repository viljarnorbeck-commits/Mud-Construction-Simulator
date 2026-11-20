#include "MudWorldValidation.h"
#include "MudTerrain.h"
#include "MudMachine.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

AMudWorldValidation::AMudWorldValidation(){PrimaryActorTick.bCanEverTick=true;}
void AMudWorldValidation::Initialize(AMudTerrain* Ground)
{
    Terrain=Ground;
    double SmallDistance=1e20,LargeDistance=1e20;
    const FVector2D Start(Terrain->Start.X,Terrain->Start.Y);
    for(int32 Id=0;Id<Terrain->Trees.Num();++Id)
    {
        const auto& T=Terrain->Trees[Id];const double Distance=FVector2D::DistSquared(Start,T.XY);
        if(Terrain->Normal(T.XY).Z<.98||Distance>100*100)continue;
        if(T.Root.Diameter<.25&&Distance<SmallDistance){Small=Id;SmallDistance=Distance;}
        if(T.Root.Diameter>.58&&Distance<LargeDistance){Large=Id;LargeDistance=Distance;}
    }
    if(Small<0||Large<0)Finish(false);
}
void AMudWorldValidation::Finish(bool Passed)
{
    Complete=true;FString Path;
    if(FParse::Value(FCommandLine::Get(),TEXT("MudSmokeReport="),Path))
        Passed=FFileHelper::SaveStringToFile(Passed?TEXT("passed"):TEXT("failed"),*Path)&&Passed;
    UE_LOG(LogTemp,Display,TEXT("Highlands world validation: %s"),Passed?TEXT("passed"):TEXT("failed"));
    FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
void AMudWorldValidation::Tick(float Dt)
{
    Super::Tick(Dt);if(Complete||!Terrain)return;
    Elapsed+=Dt;
    Terrain->UpdateTiles(FVector2D(Terrain->Start.X,Terrain->Start.Y));
    if(Elapsed<1)return; // Let asynchronous near-field Chaos collision cooking finish.
    if(Elapsed<3)
    {
        Terrain->PushTrees(Terrain->Trees[Small].XY,.1,{1,0},35000,1,Dt);
        Terrain->PushTrees(Terrain->Trees[Large].XY,.1,{1,0},10000,1,Dt);
        return;
    }
    if(Elapsed<11)return; // Real world ticks advance Chaos, including gravity and ground contact.
    auto& S=Terrain->Trees[Small];auto& L=Terrain->Trees[Large];
    const bool Fallen=S.Body&&S.bFallen&&FMath::Abs(S.Body->GetUpVector().Z)<.5;
    const bool Resisted=!L.bFallen;
    const auto RootSoil=Terrain->Excavate(L.XY,.49,10,Terrain->Height(L.XY)-1.3);
    Terrain->Deposit(L.XY+FVector2D(4,0),.9,RootSoil);
    const bool Undercut=L.Body&&L.bFallen;
    bool Blocks=false,Restored=false;
    if(S.Body)
    {
        const FVector P=S.Body->GetComponentLocation();
        Blocks=Terrain->HitsTree(FVector2D(P.X,P.Y)/100,1.0,false);
        auto Saved=Terrain->SaveTerrain();FString Json,ReadBack;
        const FString Path=FPaths::ProjectSavedDir()/TEXT("HighlandsValidation.json");
        TSharedPtr<FJsonObject> Loaded;
        if(FJsonSerializer::Serialize(Saved,TJsonWriterFactory<>::Create(&Json))&&FFileHelper::SaveStringToFile(Json,*Path)&&
            FFileHelper::LoadFileToString(ReadBack,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ReadBack),Loaded)&&
            Terrain->RestoreTerrain(Loaded)&&Terrain->Trees[Small].Body)
            Restored=FVector::Distance(P,Terrain->Trees[Small].Body->GetComponentLocation())<.01&&Terrain->Trees[Large].Body;
        if(Loaded){Loaded->SetStringField(TEXT("worldId"),TEXT("incorrect-world"));Restored&=!Terrain->RestoreTerrain(Loaded);}
    }
    FHitResult Hit;const FVector2D XY(Terrain->Start.X,Terrain->Start.Y);
    const double Z=Terrain->Height(XY)*100;FCollisionQueryParams Query(SCENE_QUERY_STAT(HighlandsSurface),true);
    const bool Collision=GetWorld()->LineTraceSingleByChannel(Hit,FVector(XY.X*100,XY.Y*100,Z+300),
        FVector(XY.X*100,XY.Y*100,Z-300),ECC_Visibility,Query)&&FMath::Abs(Hit.ImpactPoint.Z-Z)<1;
    UE_LOG(LogTemp,Display,TEXT("Highlands Chaos: fallen %d, large resisted %d, undercut %d, blocks %d, disk restore %d, surface collision %d"),
        Fallen,Resisted,Undercut,Blocks,Restored,Collision);
    const bool Mobility=TestMobility();
    const bool Earthwork=TestGeneratedEarthwork();
    Finish(Fallen&&Resisted&&Undercut&&Blocks&&Restored&&Collision&&Mobility&&Earthwork);
}

bool AMudWorldValidation::TestGeneratedEarthwork()
{
    if(!Terrain->GetWorldId().StartsWith(TEXT("earthworks-v3-")))return true;
    const auto Saved=Terrain->SaveTerrain();
    const FVector2D Bank(239,1640),Ravine(337,1640);
    const double BankBefore=Terrain->Height(Bank),RavineBefore=Terrain->Height(Ravine);
    const double MassBefore=Terrain->MaterialBalance();
    const double Wall=(Terrain->Height(Bank)-Terrain->Height(Bank-FVector2D(3,0)))/3;
    const auto Soil=Terrain->Excavate(Bank,.49,40,BankBefore-6);
    const bool SoftFace=Soil.Volume()>5.9&&Soil.Volumes[static_cast<size_t>(Earthwork::Material::Rock)]<.001;
    Terrain->Deposit(Ravine,.49,Soil);
    const bool Changed=Terrain->Height(Bank)<BankBefore-5.9&&Terrain->Height(Ravine)>RavineBefore+5.9;
    const double Error=Terrain->MaterialBalance()-MassBefore;
    // Round-trip the actual altered columns before restoring the original test snapshot.
    const auto Worked=Terrain->SaveTerrain();
    bool Restored=Terrain->RestoreTerrain(Worked)&&Terrain->Height(Bank)<BankBefore-5.9&&
        Terrain->Height(Ravine)>RavineBefore+5.9;
    Restored&=Terrain->RestoreTerrain(Saved)&&FMath::Abs(Terrain->Height(Bank)-BankBefore)<.001&&
        FMath::Abs(Terrain->Height(Ravine)-RavineBefore)<.001;
    UE_LOG(LogTemp,Display,TEXT("Earthworks generated bank: slope %.2f, excavated %.3f m3, soft %d, cut/fill %d, mass error %.9f t, restore %d"),
        Wall,Soil.Volume(),SoftFace,Changed,Error,Restored);
    return Wall>3&&SoftFace&&Changed&&FMath::Abs(Error)<.00001&&Restored;
}

bool AMudWorldValidation::TestMobility()
{
    TArray<AMudMachine*> Fleet;AMudMachine* Truck=nullptr;AMudMachine* Dozer=nullptr;
    for(TActorIterator<AMudMachine> It(GetWorld());It;++It)
    {
        Fleet.Add(*It);if(It->Kind==EMudMachine::Truck)Truck=*It;if(It->Kind==EMudMachine::Dozer)Dozer=*It;
    }
    if(!Truck||!Dozer)return false;
    const auto Baseline=Terrain->SaveTerrain();const auto Pits=Terrain->Pits;
    const FVector2D Origin(Terrain->Start.X,Terrain->Start.Y-9);
    const double Base=Terrain->Height(Origin);
    const auto Prepare=[&](Earthwork::Material Type,double Grade,bool Wet)
    {
        if(!Terrain->RestoreTerrain(Baseline))return false;
        Terrain->Pits=Pits;
        if(Wet)Terrain->Pits.Add({Origin,FVector2D(100,100)});
        // Construct controlled profiles through the production excavation/deposit API.
        for(int32 X=-12;X<=40;++X)for(int32 Y=-20;Y<=20;++Y)
        {
            const FVector2D P=Origin+FVector2D(X,Y);const double Z=Base+X*Grade;
            Terrain->Excavate(P,.49,1000,Z-.7);
            Earthwork::Load Fill;Fill.Add(Type,FMath::Max(0.0,Z-Terrain->Height(P)));Terrain->Deposit(P,.49,Fill);
        }
        for(int32 I=0;I<Fleet.Num();++I)
        {
            Fleet[I]->StopWork();Fleet[I]->Cargo={};Fleet[I]->RolledSide=0;
            Fleet[I]->Position=Origin+FVector2D(-65,I*9);Fleet[I]->UpdatePose(*Terrain);
        }
        return true;
    };
    const auto Drive=[&](AMudMachine* Machine)
    {
        Machine->StopWork();Machine->Position=Origin;Machine->Heading=0;Machine->RolledSide=0;
        if(Machine==Truck)Machine->Cargo.Add(Earthwork::Material::Gravel,Machine->Capacity()*.8);
        Machine->UpdatePose(*Terrain);
        for(int32 I=0;I<600&&Machine->Position.X<Origin.X+18;++I)
        {
            Machine->Drive(1.0/60,1,0,false,*Terrain,Fleet);
            Terrain->AdvanceSoil(1.0/60);
        }
        Machine->Halt();return Machine->Position.X-Origin.X;
    };
    if(!Prepare(Earthwork::Material::Rock,.65,false))return false;
    const double Steep=Drive(Truck);
    if(!Prepare(Earthwork::Material::Gravel,.05,false))return false;
    const double Graded=Drive(Truck);
    if(!Prepare(Earthwork::Material::Clay,0,true))return false;
    const auto Clay=Terrain->SaveTerrain();const double Mud=Drive(Truck);
    if(!Prepare(Earthwork::Material::Clay,0,true))return false;
    const double Tracked=Drive(Dozer);
    if(!Terrain->RestoreTerrain(Clay))return false;
    for(int32 X=-12;X<=40;++X)for(int32 Y=-4;Y<=4;++Y)
    {
        Earthwork::Load Gravel;Gravel.Add(Earthwork::Material::Gravel,.35);Terrain->Deposit(Origin+FVector2D(X,Y),.49,Gravel);
    }
    Dozer->StopWork();Dozer->Position=Origin+FVector2D(-65,0);Dozer->UpdatePose(*Terrain);
    Truck->Cargo={};const double Reinforced=Drive(Truck);
    Terrain->Pits=Pits;const bool Reset=Terrain->RestoreTerrain(Baseline);
    UE_LOG(LogTemp,Display,TEXT("Highlands vehicle travel: steep %.2f m, graded %.2f m, wet clay %.2f m, tracked clay %.2f m, reinforced %.2f m"),
        Steep,Graded,Mud,Tracked,Reinforced);
    return Reset&&Steep<1&&Graded>10&&Mud<3&&Tracked>Mud+2&&Reinforced>Mud+5;
}
