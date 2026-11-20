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
#include "Components/BoxComponent.h"

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
    const bool Dozer=TestDozerMechanics();
    Finish(Fallen&&Resisted&&Undercut&&Blocks&&Restored&&Collision&&Mobility&&Earthwork&&Dozer);
}

bool AMudWorldValidation::TestDozerMechanics()
{
    TArray<AMudMachine*> Fleet;AMudMachine* D=nullptr;
    for(TActorIterator<AMudMachine> It(GetWorld());It;++It){Fleet.Add(*It);if(It->Kind==EMudMachine::Dozer)D=*It;}
    if(!D)return false;
    const auto Saved=Terrain->SaveTerrain();const auto Pits=Terrain->Pits;
    const FVector2D Origin(Terrain->Start.X-18,Terrain->Start.Y-18);
    const double Z=Terrain->Start.Z;
    const auto Prepare=[&](Earthwork::Material Type=Earthwork::Material::Soil)
    {
        if(!Terrain->RestoreTerrain(Saved))return false;Terrain->Pits=Pits;
        for(int X=-10;X<=52;++X)for(int Y=-9;Y<=9;++Y)
        {
            const FVector2D P=Origin+FVector2D(X,Y);Terrain->Excavate(P,.49,1000,Z-.8);
            Earthwork::Load Fill;Fill.Add(Type,FMath::Max(0.,Z-Terrain->Height(P)));Terrain->Deposit(P,.49,Fill);
        }
        for(int I=0;I<Fleet.Num();++I){auto* M=Fleet[I];M->StopWork();M->Cargo={};M->RolledSide=0;
            M->Position=Origin+FVector2D(-65,I*9);M->UpdatePose(*Terrain);}
        D->Position=Origin;D->Heading=0;D->Blade=0;D->BladeYaw=D->BucketTilt=0;D->UpdatePose(*Terrain);
        return true;
    };
    const auto Mass=[&](){double M=Terrain->MaterialBalance();for(auto* Machine:Fleet)M+=Machine->Cargo.Mass();return M;};
    if(!Prepare())return false;
    bool Geometry=true,Pins=true,Edges=true,Strokes=true,Ends=true;
    double MinLink=1e30,MaxLink=0;
    for(double Lift:{DozerRig::MinLift,.4,DozerRig::MaxLift})for(double Yaw:{-25.,0.,25.})for(double Tilt:{-8.,0.,8.})
    {
        D->Blade=Lift-D->BladeLift;D->BladeYaw=Yaw;D->BucketTilt=Tilt;D->UpdatePose(*Terrain);
        const FVector A=D->Tool->GetComponentTransform().TransformPosition(D->DozerBladePin);
        const FVector B=D->DozerPushArms->GetComponentTransform().TransformPosition({D->DozerPushLength,0,0});
        Pins&=FVector::Distance(A,B)<.001;
        Pins&=FVector::Distance(A,D->DozerYawCarrier->GetComponentLocation())<.001;
        for(double Side:{-172.,0.,172.})
        {
            const auto Edge=DozerRig::Edge(Side),O=DozerRig::Origin(Lift,Yaw,Tilt),R=DozerRig::BladeRotate(Edge,Lift,Yaw,Tilt);
            const FVector Expected=D->GetActorTransform().TransformPosition({O.X+R.X,O.Y+R.Y,O.Z+R.Z});
            Edges&=FVector::Distance(Expected,D->Tool->GetComponentTransform().TransformPosition({Edge.X,Edge.Y,Edge.Z}))<.001;
        }
        for(const auto& Link:D->Hydraulics)
        {
            const FVector Base=Link.Base->GetComponentTransform().TransformPosition(Link.BasePin);
            const FVector End=Link.End->GetComponentTransform().TransformPosition(Link.RodPin);
            const double Length=FVector::Distance(Base,End);MinLink=FMath::Min(MinLink,Length);MaxLink=FMath::Max(MaxLink,Length);
            const bool Stroke=Length>Link.LengthCm+1&&Length<Link.LengthCm*2.05;
            if(!Stroke)UE_LOG(LogTemp,Display,TEXT("Dozer stroke mismatch: lift %.2f yaw %.0f tilt %.0f span %.2f barrel %.1f base %s end %s"),
                Lift,Yaw,Tilt,Length,Link.LengthCm,*Link.BasePin.ToString(),*Link.RodPin.ToString());
            Strokes&=Stroke;
            const FVector RodEnd=Link.Rod->GetComponentTransform().TransformPosition({0,0,50});
            Ends&=FVector::Distance(RodEnd,End)<.02;
        }
    }
    Geometry=Pins&&Edges&&Strokes&&Ends;
    UE_LOG(LogTemp,Display,TEXT("Dozer geometry: pins %d edges %d strokes %d rod ends %d"),Pins,Edges,Strokes,Ends);
    if(!Prepare())return false;
    for(double X:{12.,23.})for(double Side:{-1.,1.})
    {Earthwork::Load Heap;Heap.Add(Earthwork::Material::Soil,2);Terrain->Deposit(Origin+FVector2D(X,Side*1.1),1.1,Heap);}
    Earthwork::Load Narrow;Narrow.Add(Earthwork::Material::Soil,.65);
    Terrain->Deposit(Origin+FVector2D(17,0),.8,Narrow);
    const double MoundMass=Mass();
    D->Blade=DozerRig::MaxLift-D->BladeLift;D->UpdatePose(*Terrain);
    for(int I=0;I<1200&&D->Position.X<Origin.X+30;++I)
    {D->Drive(1./60,1,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
    const double Mounds=D->Position.X-Origin.X;const bool Upright=D->RolledSide==0;
    const bool MoundMassKept=FMath::Abs(Mass()-MoundMass)<.00001;
    // A narrow crest between the tracks is not supported by the belts. Loose
    // material must yield under the belly; the same intact rock must still block.
    const auto BellyCrest=[&](bool Fixed)
    {
        if(!Prepare())return -1.;
        const FVector2D Peak=Origin+FVector2D(8,0);
        Earthwork::Load Crest;Crest.Add(Fixed?Earthwork::Material::Rock:Earthwork::Material::Soil,.75);
        Terrain->Deposit(Peak,.49,Crest);
        if(Fixed){const int Cell=Terrain->Index(FMath::RoundToInt(Peak.X),FMath::RoundToInt(Peak.Y));
            Terrain->Edit(Cell).Layers.back().Loose=false;}
        D->Blade=DozerRig::MaxLift-D->BladeLift;D->UpdatePose(*Terrain);
        const double InitialMass=Mass();
        for(int I=0;I<900&&D->Position.X<Origin.X+17;++I)
        {D->Drive(1./60,1,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
        if(FMath::Abs(Mass()-InitialMass)>.00001)return -1.;
        return D->Position.X-Origin.X;
    };
    const double LooseCrest=BellyCrest(false),RockCrest=BellyCrest(true);
    UE_LOG(LogTemp,Display,TEXT("Dozer belly crest: loose %.3f m, intact rock %.3f m"),LooseCrest,RockCrest);
    const auto TiltCut=[&](double Tilt)
    {
        if(!Prepare())return 0.;D->Blade=-.65;D->BucketTilt=Tilt;D->UpdatePose(*Terrain);
        D->Work(1./60,0,0,*Terrain,Fleet);const double InitialMass=Mass();
        for(int I=0;I<600;++I){D->Drive(1./60,.6,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
        const FVector2D Cross=Origin+FVector2D(4.5,0);
        const double Difference=Terrain->Height(Cross-FVector2D(0,1))-Terrain->Height(Cross+FVector2D(0,1));
        for(int I=0;I<180;++I)D->Work(1./60,1,0,*Terrain,Fleet);
        const double End=D->Position.X;
        UE_LOG(LogTemp,Display,TEXT("Dozer reverse start: rolled %d, speed %.3f, belt %.3f, grip %.3f, water %.3f, cargo %.3f, rear obstacle %d"),
            D->RolledSide,D->Speed,D->Drivetrain.WheelSpeed,D->Grip,D->Water,D->Cargo.Volume(),Terrain->HitsTree(D->Position-FVector2D(1.5,0),D->OffRoadRadius(),false));
        for(int I=0;I<600&&D->Position.X>Origin.X-4;++I){D->Drive(1./60,-.7,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
        const double Backed=End-D->Position.X;
        UE_LOG(LogTemp,Display,TEXT("Dozer reverse finish: rolled %d, speed %.3f, belt %.3f, grip %.3f, water %.3f"),D->RolledSide,D->Speed,D->Drivetrain.WheelSpeed,D->Grip,D->Water);
        UE_LOG(LogTemp,Display,TEXT("Dozer tilted cut %.0f: crossfall delta %.4f m, forward %.3f m, reversed %.3f m, mass error %.9f t"),Tilt,Difference,End-Origin.X,Backed,Mass()-InitialMass);
        return Backed>2&&FMath::Abs(Mass()-InitialMass)<.00001?Difference:0.;
    };
    const double TiltLeft=TiltCut(-8),TiltRight=TiltCut(8);
    if(!Prepare())return false;
    Earthwork::Load Heap;Heap.Add(Earthwork::Material::Soil,3);Terrain->Deposit(Origin+FVector2D(10,0),2.3,Heap);
    const double PileBefore=Terrain->Height(Origin+FVector2D(10,0));
    D->Blade=-.55;D->UpdatePose(*Terrain);D->Work(1./60,0,0,*Terrain,Fleet);
    const double Before=Mass();double Moved=0,PeakDraft=0;
    for(int I=0;I<900&&D->Position.X<Origin.X+22;++I)
    {
        D->Drive(1./60,1,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);
        Moved+=D->LastWorkVolume;PeakDraft=FMath::Max(PeakDraft,D->DraftForceKN);
    }
    const double Push=D->Position.X-Origin.X,PushError=Mass()-Before;
    const double PileDrop=PileBefore-Terrain->Height(Origin+FVector2D(10,0));
    const bool PileCut=PileDrop>.05;
    UE_LOG(LogTemp,Display,TEXT("Dozer pile centre cut: %.4f m"),PileDrop);
    if(!Prepare())return false;
    Earthwork::Load Huge;Huge.Add(Earthwork::Material::Soil,100);Terrain->Deposit(Origin+FVector2D(4,0),4,Huge);
    D->Blade=-.55;D->UpdatePose(*Terrain);D->Work(1./60,0,0,*Terrain,Fleet);
    for(int I=0;I<300;++I)D->Drive(1./60,1,0,false,*Terrain,Fleet);
    const double Heavy=D->Position.X-Origin.X,Spin=FMath::Abs(D->Drivetrain.WheelSpeed-D->Speed);
    const double HeavySpeed=D->Speed;
    const auto SideFlow=[&](double Yaw)
    {
        if(!Prepare())return 0.;D->Blade=-.55;D->BladeYaw=Yaw;D->Cargo.Add(Earthwork::Material::Soil,3);
        D->UpdatePose(*Terrain);D->Work(1./60,0,0,*Terrain,Fleet);const auto BeforeCells=Terrain->AccountedMass;
        const double InitialMass=Mass();
        for(int I=0;I<240;++I){D->Drive(1./60,.7,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
        double Total=0,Moment=0;
        for(const auto& Entry:Terrain->AccountedMass)
        {
            const double Added=FMath::Max(0.,Entry.Value-BeforeCells.FindRef(Entry.Key));
            Total+=Added;Moment+=Added*(Entry.Key/Terrain->Samples-Origin.Y);
        }
        if(FMath::Abs(Mass()-InitialMass)>.00001)return 0.;
        return Total>.01?Moment/Total:0.;
    };
    const double Left=SideFlow(-25),Right=SideFlow(25);
    if(!Prepare())return false;D->bGrade=false;D->ToggleTool();
    for(int I=0;I<90;++I)D->Work(1./60,0,0,*Terrain,Fleet);
    for(int I=0;I<1200&&D->Position.X<Origin.X+18;++I)
    {D->Drive(1./60,.7,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
    const double HeldDepth=Z-Terrain->Height(Origin+FVector2D(8,0));
    const double HeldTravel=D->Position.X-Origin.X,HeldCargo=D->Cargo.Volume(),HeldDraft=D->DraftForceKN;
    bool Relief=false;
    if(HeldTravel<=8&&HeldCargo>.2)
    {
        // A deep continuous cut can exhaust drive force before the volume limit.
        // Prove that raising/releasing the blade restores travel, rather than
        // demanding unlimited pushing force from the reference-height regression.
        for(int I=0;I<180;++I)D->Work(1./60,1,0,*Terrain,Fleet);
        const double StartX=D->Position.X;
        for(int I=0;I<360;++I){D->Drive(1./60,.7,0,false,*Terrain,Fleet);D->Work(1./60,0,0,*Terrain,Fleet);}
        Relief=D->Cargo.Volume()<.001&&D->Position.X-StartX>2;
    }
    const bool GradeHold=HeldTravel>6&&HeldDepth>.10&&HeldDepth<.45&&(HeldTravel>8||Relief);
    UE_LOG(LogTemp,Display,TEXT("Dozer grade hold: travel %.2f m, centre cut %.3f m, cargo %.2f m3, draft %.1f kN, relief %d, stable %d"),HeldTravel,HeldDepth,HeldCargo,HeldDraft,Relief,GradeHold);
    // Existing native surface validation compares updated triangles/normals and also
    // performs a fractional-cell deformation/restore; verify actual Chaos height too.
    Terrain->UpdateTiles(Origin);for(int I=0;I<12;++I)Terrain->UpdateTiles(Origin);
    const FVector2D Probe=Origin+FVector2D(8,0);const double H=Terrain->Height(Probe)*100;
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(DozerDeformedSurface),true);
    const bool Collision=GetWorld()->LineTraceSingleByChannel(Hit,{Probe.X*100,Probe.Y*100,H+300},
        {Probe.X*100,Probe.Y*100,H-300},ECC_Visibility,Query)&&FMath::Abs(Hit.ImpactPoint.Z-H)<1;
    Terrain->Pits=Pits;const bool Restored=Terrain->RestoreTerrain(Saved);
    UE_LOG(LogTemp,Display,TEXT("Dozer A-H: geometry %d (cylinder %.1f..%.1f cm), mounds %.2f m upright %d, push %.2f m moved %.2f m3 draft %.1f kN mass %.9f t, overload %.2f m slip %.2f m/s, side %.2f/%.2f m, collision %d restore %d"),
        Geometry,MinLink,MaxLink,Mounds,Upright,Push,Moved,PeakDraft,PushError,Heavy,Spin,Left,Right,Collision,Restored);
    return Geometry&&Mounds>26&&Upright&&MoundMassKept&&LooseCrest>15&&RockCrest>0&&RockCrest<8&&TiltLeft<-.06&&TiltRight>.06&&Push>6&&Moved>.3&&PileCut&&FMath::Abs(PushError)<.00001&&Heavy<1&&
        (Spin>1||FMath::Abs(HeavySpeed)<.05)&&
        Left<-.2&&Right>.2&&GradeHold&&Collision&&Restored;
}

bool AMudWorldValidation::TestGeneratedEarthwork()
{
    const bool Natural=Terrain->GetWorldId().StartsWith(TEXT("earthworks-v4-"));
    if(!Natural&&!Terrain->GetWorldId().StartsWith(TEXT("earthworks-v3-")))return true;
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
    return Wall>(Natural?.35:3)&&(!Natural||Wall<1)&&SoftFace&&Changed&&FMath::Abs(Error)<.00001&&Restored;
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
