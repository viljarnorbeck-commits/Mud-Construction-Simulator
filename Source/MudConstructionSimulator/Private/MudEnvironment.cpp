#include "MudTerrain.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

void AMudTerrain::UpdateTreeSupport(int32 Cell)
{
    if(bRestoringTerrain)return;
    const FVector2D Point(Cell%Samples,Cell/Samples);
    for(auto It=TreeTiles.CreateConstKeyIterator(TileIndex(Point.X,Point.Y));It;++It)
    {
        const int32 Id=It.Value();auto& T=Trees[Id];
        if(T.bRemoved||FVector2D::DistSquared(T.XY,Point)>1.1*1.1)continue;
        const double RootBase=T.Trunk.GetLocation().Z/100-T.Root.Height*.5;
        if(T.Root.Unsupported(RootBase-Height(T.XY)))ActivateTree(Id);
    }
}

void AMudTerrain::BendTree(int32 Id)
{
    auto& Tree = Trees[Id];
    const FVector Base(Tree.XY.X*100,Tree.XY.Y*100,Height(Tree.XY)*100);
    const FQuat Q(FVector(-Tree.BendDirection.Y,Tree.BendDirection.X,0),Tree.Root.Angle);
    const auto Bend = [&](FTransform Pose)
    {
        Pose.SetLocation(Base+Q.RotateVector(Pose.GetLocation()-Base));
        Pose.SetRotation(Q*Pose.GetRotation()); return Pose;
    };
    Trunks->UpdateInstanceTransform(Id,Bend(Tree.Trunk),false,true,true);
    for(int32 Tier=0;Tier<CrownInstancesPerTree;++Tier)
        Crowns->UpdateInstanceTransform(Id*CrownInstancesPerTree+Tier,Bend(Tree.Crown[Tier]),false,true,true);
}

void AMudTerrain::ActivateTree(int32 Id)
{
    auto& Tree=Trees[Id];
    if(Tree.Body)return;
    HideTree(Id); Tree.bFallen=true; Tree.Root.Broken=true; BentTrees.Remove(Id);
    if(auto* Proxy=TreeContacts.FindRef(Id))Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Only trees within reach of an activated trunk need native static-body proxies.
    TSet<int32> Tiles;
    for(double X:{-18.0,18.0})for(double Y:{-18.0,18.0})Tiles.Add(TileIndex(Tree.XY.X+X,Tree.XY.Y+Y));
    for(int32 Tile:Tiles)for(auto It=TreeTiles.CreateConstKeyIterator(Tile);It;++It)
    {
        const int32 OtherId=It.Value();const auto& Other=Trees[OtherId];
        if(Other.bRemoved||TreeContacts.Contains(OtherId)||FVector2D::Distance(Other.XY,Tree.XY)>18)continue;
        auto* Proxy=NewObject<UStaticMeshComponent>(this);
        Proxy->SetupAttachment(RootComponent);Proxy->SetStaticMesh(Trunks->GetStaticMesh());
        Proxy->SetRelativeTransform(Other.Trunk);Proxy->SetVisibility(false);
        Proxy->SetCollisionProfileName(TEXT("BlockAll"));Proxy->RegisterComponent();TreeContacts.Add(OtherId,Proxy);
    }
    auto* Actor=GetWorld()->SpawnActor<AActor>();
    FallenActors.Add(Actor);
    auto* Body=NewObject<UStaticMeshComponent>(Actor);
    Actor->SetRootComponent(Body);
    Body->SetStaticMesh(Trunks->GetStaticMesh());
    Body->SetMaterial(0,Trunks->GetMaterial(0));
    Body->SetCollisionProfileName(TEXT("PhysicsActor"));
    Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Body->SetWorldTransform(Tree.Trunk);
    Body->RegisterComponent();
    Tree.Body=Body;
    // The primitive cylinder contains a cooked simple collision shape.
    Body->SetSimulatePhysics(true);
    Body->SetMassOverrideInKg(NAME_None,Tree.Root.MassKg(),true);
    Body->SetLinearDamping(0.9); Body->SetAngularDamping(1.8);
    Body->SetPhysicsMaxAngularVelocityInDegrees(100);
    Body->SetUseCCD(true);Body->SetMaxDepenetrationVelocity(NAME_None,200);
    const FVector Base(Tree.XY.X*100,Tree.XY.Y*100,Height(Tree.XY)*100);
    const auto AddPart=[&](UStaticMesh* Mesh,UMaterialInterface* Material,FTransform Pose,bool Collision)
    {
        auto* Part=NewObject<UStaticMeshComponent>(Actor);
        Part->SetStaticMesh(Mesh);Part->SetMaterial(0,Material);
        Part->SetWorldTransform(Pose);
        Part->SetCollisionEnabled(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        if(Collision)Part->SetCollisionProfileName(TEXT("PhysicsActor"));
        Part->RegisterComponent();
        Part->AttachToComponent(Body,FAttachmentTransformRules(EAttachmentRule::KeepWorld,true));
    };
    for(int32 Tier=0;Tier<CrownInstancesPerTree;++Tier)
        AddPart(Crowns->GetStaticMesh(),Crowns->GetMaterial(0),Tree.Crown[Tier],false);
    // Two welded major limbs share the trunk body. Twigs and foliage are cosmetic.
    for(double Side:{-1.0,1.0})
    {
        const FVector A=Base+FVector(0,0,Tree.Root.Height*55);
        const FVector B=A+FVector(Side*Tree.Root.Height*15,0,Tree.Root.Height*8);
        const FVector Span=B-A;
        AddPart(Trunks->GetStaticMesh(),Trunks->GetMaterial(0),
            FTransform(FQuat::FindBetweenNormals(FVector::UpVector,Span.GetSafeNormal()),(A+B)*.5,
                FVector(Tree.Root.Diameter*.32,Tree.Root.Diameter*.32,Span.Size()/100)),true);
    }
    Body->SetMassOverrideInKg(NAME_None,Tree.Root.MassKg(),true);
    const FVector Direction(Tree.BendDirection.X,Tree.BendDirection.Y,0);
    Body->AddImpulseAtLocation(Direction*Tree.Root.MassKg()*80,Base+FVector(0,0,Tree.Root.Height*70));
}

void AMudTerrain::PushTrees(FVector2D XY,double Radius,FVector2D Direction,double ForceN,double ContactHeight,double Dt)
{
    Direction=Direction.GetSafeNormal();
    if(Direction.IsNearlyZero()||ForceN<=0||Dt<=0)return;
    TSet<int32> Tiles;
    for(int32 Y=TileIndex(0,XY.Y-Radius)/TilesPerSide;Y<=TileIndex(0,XY.Y+Radius)/TilesPerSide;++Y)
        for(int32 X=TileIndex(XY.X-Radius,0);X<=TileIndex(XY.X+Radius,0);++X)Tiles.Add(Y*TilesPerSide+X);
    TArray<int32> Contacts;
    for(int32 Tile:Tiles)for(auto It=TreeTiles.CreateConstKeyIterator(Tile);It;++It)
    {
        const auto& T=Trees[It.Value()];
        if(!T.bRemoved&&FVector2D::Distance(XY,T.XY)<Radius+T.Root.Diameter*.5)Contacts.Add(It.Value());
    }
    const double Share=ForceN/FMath::Max(1,Contacts.Num());
    for(int32 Id:Contacts)
    {
        auto& T=Trees[Id];T.BendDirection=Direction;T.LastPush=GetWorld()->GetTimeSeconds();
        if(T.Root.Push(Share,ContactHeight,Wetness(T.XY),Dt))ActivateTree(Id);
        else {BentTrees.Add(Id);BendTree(Id);}
    }
    for(AActor* Actor:FallenActors)
    {
        auto* Body=Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        const FVector Axis=Body->GetUpVector()*Body->GetComponentScale().Z*50;
        const FVector P(XY.X*100,XY.Y*100,(Height(XY)+ContactHeight)*100);
        const FVector Center=Body->GetComponentLocation();
        const double Distance=FMath::PointDistToSegment(P,Center-Axis,Center+Axis);
        if(Distance>Radius*100+Body->GetComponentScale().X*50)continue;
        if(!Body->IsSimulatingPhysics())Body->SetSimulatePhysics(true);
        // A contact transmits a bounded impulse; light saplings cannot receive the entire vehicle momentum.
        const double Impulse=FMath::Min(ForceN*Dt,Body->GetMass()*0.35);
        Body->AddImpulseAtLocation(FVector(Direction.X,Direction.Y,0)*Impulse*100,P);
    }
}

void AMudTerrain::UpdateEnvironment(FVector2D Focus,double Dt)
{
    TArray<int32> Relaxed;
    for(int32 Id:BentTrees)
    {
        auto& T=Trees[Id];
        if(GetWorld()->GetTimeSeconds()-T.LastPush<.05)continue;
        T.Root.Push(0,1,Wetness(T.XY),FMath::Min(Dt,1.0/30));
        BendTree(Id);
        if(T.Root.Angle<0.0001&&FMath::Abs(T.Root.Rate)<.0001)Relaxed.Add(Id);
    }
    for(int32 Id:Relaxed)BentTrees.Remove(Id);
    for(AActor* Actor:FallenActors)
    {
        auto* Body=Cast<UStaticMeshComponent>(Actor->GetRootComponent());
        const FVector P=Body->GetComponentLocation();
        const double Distance=FVector2D::Distance(Focus,FVector2D(P.X,P.Y)/100);
        if(Distance>120&&Body->IsSimulatingPhysics())Body->SetSimulatePhysics(false);
        else if(Distance<100&&!Body->IsSimulatingPhysics())Body->SetSimulatePhysics(true);
    }
}

void AMudTerrain::SaveEnvironment(FJsonObject& Data) const
{
    TArray<TSharedPtr<FJsonValue>> States;
    for(int32 Id=0;Id<Trees.Num();++Id)
    {
        const auto& T=Trees[Id];
        if(!T.Body&&T.Root.Angle<.0001)continue;
        auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("id"),Id);
        Row->SetNumberField(TEXT("bend"),T.Root.Angle);Row->SetNumberField(TEXT("rate"),T.Root.Rate);
        Row->SetNumberField(TEXT("dx"),T.BendDirection.X);Row->SetNumberField(TEXT("dy"),T.BendDirection.Y);
        if(T.Body)
        {
            TArray<TSharedPtr<FJsonValue>> Pose;
            const FVector P=T.Body->GetComponentLocation();const FQuat Q=T.Body->GetComponentQuat();
            const FVector V=T.Body->GetPhysicsLinearVelocity(),W=T.Body->GetPhysicsAngularVelocityInRadians();
            for(double N:{P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,V.X,V.Y,V.Z,W.X,W.Y,W.Z})Pose.Add(MakeShared<FJsonValueNumber>(N));
            Row->SetArrayField(TEXT("pose"),Pose);
        }
        States.Add(MakeShared<FJsonValueObject>(Row));
    }
    Data.SetArrayField(TEXT("treePhysics"),States);
}

bool AMudTerrain::RestoreEnvironment(const FJsonObject& Data,bool ValidateOnly)
{
    struct FState{int32 Id;double Bend,Rate;FVector2D Direction;TArray<double> Pose;};
    TArray<FState> States;
    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if(Data.HasField(TEXT("treePhysics")))
    {
        if(!Data.TryGetArrayField(TEXT("treePhysics"),Rows)||Rows->Num()>Trees.Num())return false;
        TSet<int32> Seen;
        for(const auto& Value:*Rows)
        {
            const TSharedPtr<FJsonObject>* Row;FState S{};
            if(!Value->TryGetObject(Row)||!(*Row)->TryGetNumberField(TEXT("id"),S.Id)||!Trees.IsValidIndex(S.Id)||Seen.Contains(S.Id)||
                !(*Row)->TryGetNumberField(TEXT("bend"),S.Bend)||!FMath::IsFinite(S.Bend)||S.Bend<0||S.Bend>3.2||
                !(*Row)->TryGetNumberField(TEXT("rate"),S.Rate)||!FMath::IsFinite(S.Rate)||FMath::Abs(S.Rate)>100||
                !(*Row)->TryGetNumberField(TEXT("dx"),S.Direction.X)||!(*Row)->TryGetNumberField(TEXT("dy"),S.Direction.Y)||
                !FMath::IsFinite(S.Direction.X)||!FMath::IsFinite(S.Direction.Y))return false;
            Seen.Add(S.Id);
            if((*Row)->HasField(TEXT("pose")))
            {
                const TArray<TSharedPtr<FJsonValue>>* P;
                if(!(*Row)->TryGetArrayField(TEXT("pose"),P)||P->Num()!=13)return false;
                for(const auto& V:*P){double N;if(!V->TryGetNumber(N)||!FMath::IsFinite(N))return false;S.Pose.Add(N);}
                const auto& N=S.Pose;
                if(N[0]<-2000||N[1]<-2000||N[0]>(GetWorldSize()+20)*100||N[1]>(GetWorldSize()+20)*100||FMath::Abs(N[2])>100000||
                    FMath::Abs(FQuat(N[3],N[4],N[5],N[6]).SizeSquared()-1)>.01)return false;
                for(int I=7;I<13;++I)if(FMath::Abs(N[I])>10000)return false;
            }
            States.Add(MoveTemp(S));
        }
    }
    if(ValidateOnly)return true;
    for(AActor* Actor:FallenActors)Actor->Destroy();FallenActors.Reset();BentTrees.Reset();
    for(const auto& Proxy:TreeContacts)Proxy.Value->DestroyComponent();TreeContacts.Reset();
    for(auto& T:Trees){T.Body=nullptr;T.bFallen=false;T.Root.Angle=T.Root.Rate=0;T.Root.Broken=false;}
    for(const auto& S:States)
    {
        auto& T=Trees[S.Id];T.Root.Angle=S.Bend;T.Root.Rate=S.Rate;T.BendDirection=S.Direction.GetSafeNormal();
        if(!S.Pose.IsEmpty())
        {
            ActivateTree(S.Id);const auto& N=S.Pose;
            T.Body->SetWorldLocationAndRotation(FVector(N[0],N[1],N[2]),FQuat(N[3],N[4],N[5],N[6]),false,nullptr,ETeleportType::TeleportPhysics);
            T.Body->SetPhysicsLinearVelocity(FVector(N[7],N[8],N[9]));T.Body->SetPhysicsAngularVelocityInRadians(FVector(N[10],N[11],N[12]));
        }
        else if(!T.bRemoved){BentTrees.Add(S.Id);BendTree(S.Id);}
    }
    return true;
}
