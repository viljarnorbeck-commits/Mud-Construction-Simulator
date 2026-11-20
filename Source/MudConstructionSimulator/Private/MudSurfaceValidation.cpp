#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"

bool AMudTerrain::ValidateCollisionQueries(FVector2D Focus)
{
    double Error=0;int32 Hits=0;bool Passed=true;
    for(int32 Y=-1;Y<=1;++Y)for(int32 X=-1;X<=1;++X)
    {
        const FVector2D P=Focus+FVector2D(X*.83+.13,Y*.79+.17);
        const int32 Id=TileIndex(FMath::FloorToInt(P.X),FMath::FloorToInt(P.Y));
        auto* Mesh=TerrainMeshes[Id].Get();const double Z=Height(P);
        FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(NaturalTerrainCollision),true);
        const bool Found=Mesh&&TileSteps[Id]==0&&Mesh->LineTraceComponent(Hit,FVector(P.X,P.Y,Z+5)*100,FVector(P.X,P.Y,Z-5)*100,Query);
        if(Found){++Hits;Error=FMath::Max(Error,FMath::Abs(Hit.ImpactPoint.Z/100-Z));Passed&=Hit.ImpactNormal.Z>0;}
        else Passed=false;
    }
    Passed&=Hits==9&&Error<.01;
    UE_LOG(LogTemp,Display,TEXT("Natural terrain Chaos queries: %d, hits %d/9, maximum height error %.7f m"),Passed,Hits,Error);
    return Passed;
}

bool AMudTerrain::ValidateSurfaceRendering(FVector2D Focus)
{
    const auto OriginalState=SaveTerrain();
    const FVector2D P(FMath::FloorToDouble(Focus.X),FMath::FloorToDouble(Focus.Y));
    Earthwork::Load Fill; Fill.Add(Earthwork::Material::Sand,3);
    Deposit(P,1.2,Fill);
    TArray<double> Before;
    for (int32 I=0; I<25; ++I) Before.Add(Height(P+FVector2D((I%5)*.37-.74,(I/5)*.29-.58)));
    const auto Saved=SaveTerrain();
    Excavate(P,1.2,1,Height(P)-0.4);
    for (int32 I=0; I<25; ++I) Height(P+FVector2D((I%5)*.37-.74,(I/5)*.29-.58));
    bool Passed=RestoreTerrain(Saved);
    for (int32 I=0; I<25; ++I)
        Passed &= FMath::Abs(Height(P+FVector2D((I%5)*.37-.74,(I/5)*.29-.58))-Before[I])<0.00001;
    for (int32 Id=0; Id<TerrainMeshes.Num(); ++Id)
    {
        const FVector2D Center((Id%TilesPerSide+.5)*TileSize,(Id/TilesPerSide+.5)*TileSize);
        if (FVector2D::DistSquared(Center,Focus)<=FMath::Square(95.0)) BuildTile(Id,Id%3==0 ? 4 : 0);
    }
    // A second local edit exercises the partial update of an existing fine mesh.
    Earthwork::Load LocalFill; LocalFill.Add(Earthwork::Material::Gravel,0.35);
    Deposit(P+FVector2D(.6,.3),.8,LocalFill);
    double Error=0; int32 Checked=0,VerticalTriangles=0,InvalidNormals=0;
    for (int32 Id=0; Id<TerrainMeshes.Num(); ++Id)
    {
        const FVector2D Center((Id%TilesPerSide+.5)*TileSize,(Id/TilesPerSide+.5)*TileSize);
        if (FVector2D::DistSquared(Center,Focus)>FMath::Square(95.0)) continue;
        BuildTile(Id,Id%3==0 ? 4 : 0);
        auto* Mesh=TerrainMeshes[Id].Get(); const auto* Section=Mesh->GetProcMeshSection(0);
        for(int32 I=0;I<Section->ProcIndexBuffer.Num();I+=3)
        {
            const FVector A=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I]].Position;
            const FVector B=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+1]].Position;
            const FVector C=Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+2]].Position;
            // A vertical skirt has zero XY area even when all three 3D points differ.
            if(FMath::Abs((B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X))<.01)++VerticalTriangles;
        }
        for(const auto& V:Section->ProcVertexBuffer)if(V.Normal.ContainsNaN()||V.Normal.Z<=0)++InvalidNormals;
        for (int32 I=0; I<Section->ProcIndexBuffer.Num(); I+=3*31)
        {
            if (TileSteps[Id]!=0) break;
            FVector Point=FVector::ZeroVector;
            for (int32 Corner=0; Corner<3; ++Corner)
                Point+=Mesh->GetComponentTransform().TransformPosition(Section->ProcVertexBuffer[Section->ProcIndexBuffer[I+Corner]].Position)/300;
            Error=FMath::Max(Error,FMath::Abs(Point.Z-Height(FVector2D(Point.X,Point.Y)))); ++Checked;
        }
        // All LODs must agree with the shared half-metre boundary, without skirts.
        for (const auto& Vertex:Section->ProcVertexBuffer)
            if (Vertex.Position.X==0 || Vertex.Position.Y==0 || Vertex.Position.X==TileSize*100 || Vertex.Position.Y==TileSize*100)
            {
                const FVector Point=Mesh->GetComponentTransform().TransformPosition(Vertex.Position)/100;
                Error=FMath::Max(Error,FMath::Abs(Point.Z-Height(FVector2D(Point.X,Point.Y)))); ++Checked;
            }
    }
    Passed &= Error<0.0005 && Checked>100 && !VerticalTriangles && !InvalidNormals;
    UE_LOG(LogTemp,Display,TEXT("Terrain topology: vertical/degenerate triangles %d, invalid normals %d"),VerticalTriangles,InvalidNormals);
    UE_LOG(LogTemp,Display,TEXT("Surface reconstruction: restore/local-update/mesh/seams %d, samples %d, maximum contact error %.8f m"),Passed,Checked,Error);
    return RestoreTerrain(OriginalState) && Passed;
}
