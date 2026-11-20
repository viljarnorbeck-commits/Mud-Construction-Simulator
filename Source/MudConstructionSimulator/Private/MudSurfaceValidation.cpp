#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Dom/JsonObject.h"

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
    double Error=0; int32 Checked=0;
    for (int32 Id=0; Id<TerrainMeshes.Num(); ++Id)
    {
        const FVector2D Center((Id%TilesPerSide+.5)*TileSize,(Id/TilesPerSide+.5)*TileSize);
        if (FVector2D::DistSquared(Center,Focus)>FMath::Square(95.0)) continue;
        BuildTile(Id,Id%3==0 ? 4 : 0);
        auto* Mesh=TerrainMeshes[Id].Get(); const auto* Section=Mesh->GetProcMeshSection(0);
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
    Passed &= Error<0.0005 && Checked>100;
    UE_LOG(LogTemp,Display,TEXT("Surface reconstruction: restore/local-update/mesh/seams %d, samples %d, maximum contact error %.8f m"),Passed,Checked,Error);
    return RestoreTerrain(OriginalState) && Passed;
}
