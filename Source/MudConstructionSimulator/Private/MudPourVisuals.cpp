#include "MudTerrain.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

void AMudTerrain::UpdatePourSurfaces()
{
    if (Pours.IsEmpty() && !bPourSurfacesVisible) return;
    if (PourSurfaces.IsEmpty())
    {
        const TCHAR* Paints[] = {TEXT("Loose"),TEXT("LooseGravel"),TEXT("LooseRock"),TEXT("LooseSand"),TEXT("LooseClay")};
        for (int32 M=0; M<Earthwork::MaterialCount; ++M)
        {
            auto* Mesh=NewObject<UProceduralMeshComponent>(this);
            Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->SetCastShadow(false);
            Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Generated/%s.%s"),Paints[M],Paints[M])));
            Mesh->RegisterComponent(); PourSurfaces.Add(Mesh);
        }
    }
    for (int32 M=0; M<Earthwork::MaterialCount; ++M)
    {
        TArray<FVector> Vertices, Normals; TArray<FVector2D> UV; TArray<int32> Indices;
        int32 Count=0;
        for (const auto& Pour : Pours)
        {
            if (static_cast<int32>(Pour.Mass.DominantMaterial())!=M || Count++>=96) continue;
            // Reconstruct the actual mass-bearing packet, not an independent particle simulation.
            const FVector Axis=Pour.Velocity.GetSafeNormal(UE_SMALL_NUMBER,FVector::DownVector);
            const FVector Right=FVector::CrossProduct(Axis,FVector::ForwardVector).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector);
            const FVector Up=FVector::CrossProduct(Axis,Right).GetSafeNormal();
            const double Radius=FMath::Clamp(FMath::Pow(Pour.Mass.Volume(),1.0/3)*0.38,0.025,0.19);
            const double Length=FMath::Clamp(Pour.Velocity.Size()*0.055,Radius,0.40);
            const int32 Base=Vertices.Num();
            constexpr int32 Sides=12, Rings=6;
            for (int32 J=0; J<=Rings; ++J)
                for (int32 I=0; I<=Sides; ++I)
                {
                    const double A=PI*J/Rings, B=2*PI*I/Sides;
                    const FVector Radial=Right*FMath::Cos(B)+Up*FMath::Sin(B);
                    Vertices.Add((Pour.Position+Axis*(FMath::Cos(A)*Length)+Radial*(FMath::Sin(A)*Radius))*100);
                    Normals.Add((Axis*(FMath::Cos(A)/Length)+Radial*(FMath::Sin(A)/Radius)).GetSafeNormal());
                    UV.Emplace(static_cast<double>(I)/Sides,static_cast<double>(J)/Rings);
                    if (I && J)
                    {
                        const int32 N=Base+J*(Sides+1)+I;
                        Indices.Append({N-Sides-2,N-1,N-Sides-1,N-Sides-1,N-1,N});
                    }
                }
        }
        auto* Mesh=PourSurfaces[M].Get();
        if (Vertices.IsEmpty()) Mesh->ClearAllMeshSections();
        else if (const auto* Section=Mesh->GetProcMeshSection(0); Section && Section->ProcVertexBuffer.Num()==Vertices.Num())
            Mesh->UpdateMeshSection_LinearColor(0,Vertices,Normals,UV,{}, {},false);
        else Mesh->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,{}, {},false,false);
    }
    bPourSurfacesVisible=!Pours.IsEmpty();
}
