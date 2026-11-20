#include "MudMaterialReview.h"
#include "MudTerrain.h"
#include "MudMachine.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

AMudMaterialReview::AMudMaterialReview()
{
    PrimaryActorTick.bCanEverTick=true;
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("ReviewCamera")); RootComponent=Camera;
    Camera->SetFieldOfView(64);
    auto& P=Camera->PostProcessSettings;
    P.bOverride_AutoExposureMethod=true; P.AutoExposureMethod=AEM_Manual;
    P.bOverride_AutoExposureBias=true; P.AutoExposureBias=0;
    P.bOverride_AutoExposureApplyPhysicalCameraExposure=true; P.AutoExposureApplyPhysicalCameraExposure=false;
    P.bOverride_AmbientOcclusionIntensity=true; P.AmbientOcclusionIntensity=0.40;
    P.bOverride_AmbientOcclusionRadius=true; P.AmbientOcclusionRadius=65;
}
void AMudMaterialReview::View(FVector Location,FVector Target)
{
    const FVector Origin(Center.X,Center.Y,Terrain->Height(Center));
    Camera->SetWorldLocation((Origin+Location)*100);
    Camera->SetWorldRotation((Target-Location).Rotation());
}
void AMudMaterialReview::Initialize(AMudTerrain& Ground,const TArray<AMudMachine*>& Machines)
{
    Terrain=&Ground; Fleet=Machines;
    FParse::Value(FCommandLine::Get(),TEXT("MudMaterialReview="),Folder);
    IFileManager::Get().MakeDirectory(*Folder,true);
    Center=FVector2D(Terrain->Start.X+18,Terrain->Start.Y+18);
    for (int32 M=0; M<Earthwork::MaterialCount; ++M)
    {
        const FVector2D P=Center+FVector2D(0,(M-2)*6.0);
        Earthwork::Load Mass; Mass.Add(static_cast<Earthwork::Material>(M),14);
        Terrain->Deposit(P,2.7,Mass);
        Terrain->CutPass(P+FVector2D(0.8,0),FVector2D(0,1),FVector2D(1.8,0.45),1.2,Terrain->Height(Center)+0.45);
        Fleet[M]->Position=Center+FVector2D(-25,25+M*8); Fleet[M]->UpdatePose(*Terrain);
    }
    auto* Loader=Fleet[4]; Loader->Position=Center+FVector2D(-6,-8); Loader->Heading=0;
    Loader->Boom=12; Loader->Curl=24; Loader->Cargo.Add(Earthwork::Material::Gravel,2.2); Loader->UpdatePose(*Terrain);
    auto* Dozer=Fleet[0]; Dozer->Position=Center+FVector2D(-6,-12); Dozer->Heading=0;
    Dozer->ToggleTool(); Dozer->UpdatePose(*Terrain);
    for (int32 I=0; I<60; ++I) Terrain->UpdateTiles(Center);
    auto* PC=GetWorld()->GetFirstPlayerController(); PC->SetViewTarget(this);
    if (auto* HUD=PC->GetHUD()) HUD->bShowHUD=false;
    for (const auto* Setting : {TEXT("r.ScreenPercentage"),TEXT("t.MaxFPS"),TEXT("r.VSync")})
        if (auto* Value=IConsoleManager::Get().FindConsoleVariable(Setting))
            Value->Set(FCString::Strcmp(Setting,TEXT("r.ScreenPercentage"))==0 ? 100 : 0,ECVF_SetByCode);
    View(FVector(10,-8,5.6),FVector(0,-7,0.6));
}
void AMudMaterialReview::Tick(float Dt)
{
    Super::Tick(Dt); ++Frame;
    Terrain->UpdateTiles(Center);
    Terrain->UpdateSpills(1.0/60);
    if (Frame>=180 && Frame<300) Timings.Add(Dt*1000.0);
    if (Frame>=450 && Frame<535) WorkTimings.Add(Dt*1000.0);
    if (Frame==300) FScreenshotRequest::RequestScreenshot(Folder/TEXT("near.png"),false,false);
    if (Frame==305) View(FVector(24,-27,19),FVector(0,0,0.7));
    if (Frame==365) FScreenshotRequest::RequestScreenshot(Folder/TEXT("overview.png"),false,false);
    if (Frame==370) View(FVector(7,-23,5),FVector(-1,-11,1));
    if (Frame>370 && Frame<550)
    {
        Fleet[0]->Drive(1.0/60,0.30,0,false,*Terrain,Fleet);
        Fleet[0]->Work(1.0/60,0,0,*Terrain,Fleet);
        Earthwork::Load Mass; Mass.Add(Earthwork::Material::Soil,0.025);
        const FVector2D P=Center+FVector2D(0.8,-11);
        Terrain->Pour(FVector(P.X,P.Y,Terrain->Height(P)+2.6),FVector(0.2,0,0),0.7,Mass);
        Terrain->AdvancePours(1.0/60); Terrain->AdvanceSoil(1.0/60);
    }
    if (Frame==540) FScreenshotRequest::RequestScreenshot(Folder/TEXT("work.png"),false,false);
    if (Frame==550)
    {
        Timings.Sort(); double Mean=0; for (double Time:Timings) Mean+=Time/Timings.Num();
        const FString Stats=FString::Printf(TEXT("MeanMs,P95Ms,MeanFPS\n%.3f,%.3f,%.1f\n"),
            Mean,Timings[FMath::FloorToInt(Timings.Num()*0.95)],1000/Mean);
        FFileHelper::SaveStringToFile(Stats,*(Folder/TEXT("timings.csv")));
        WorkTimings.Sort(); double WorkMean=0;
        for (double Time:WorkTimings) WorkMean+=Time/WorkTimings.Num();
        const FString WorkStats=FString::Printf(TEXT("MeanMs,P95Ms,MeanFPS\n%.3f,%.3f,%.1f\n"),
            WorkMean,WorkTimings[FMath::FloorToInt(WorkTimings.Num()*0.95)],1000/WorkMean);
        FFileHelper::SaveStringToFile(WorkStats,*(Folder/TEXT("work-timings.csv")));
        FPlatformMisc::RequestExit(false);
    }
}
