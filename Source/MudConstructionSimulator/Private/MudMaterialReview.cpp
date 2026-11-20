#include "MudMaterialReview.h"
#include "MudTerrain.h"
#include "MudMachine.h"
#include "LoaderRig.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
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
    const FVector Origin(Center.X,Center.Y,bSoftSoilReview?18:Terrain->Height(Center));
    Camera->SetWorldLocation((Origin+Location)*100);
    Camera->SetWorldRotation((Target-Location).Rotation());
}
void AMudMaterialReview::Initialize(AMudTerrain& Ground,const TArray<AMudMachine*>& Machines)
{
    Terrain=&Ground; Fleet=Machines;
    FParse::Value(FCommandLine::Get(),TEXT("MudMaterialReview="),Folder);
    bDozerReview=FParse::Param(FCommandLine::Get(),TEXT("MudDozerReview"));
    bTerrainReview=FParse::Param(FCommandLine::Get(),TEXT("MudTerrainReview"));
    bSoftSoilReview=FParse::Param(FCommandLine::Get(),TEXT("MudSoftSoilReview"));
    if(bDozerReview||bSoftSoilReview)Camera->PostProcessSettings.AutoExposureBias=.8;
    if(bDozerReview||bSoftSoilReview)
    {
        // Inspection fill light: keeps shaded joints visible in controlled screenshots.
        auto* Fill=NewObject<UDirectionalLightComponent>(this);Fill->SetupAttachment(Camera);
        Fill->SetIntensity(2.0);Fill->SetCastShadows(false);Fill->RegisterComponent();
    }
    IFileManager::Get().MakeDirectory(*Folder,true);
    if(bSoftSoilReview)
    {
        Center={250,800};
        for(int32 I=0;I<Fleet.Num();++I){Fleet[I]->StopWork();Fleet[I]->Cargo={};Fleet[I]->Position={140.,730.+I*8};Fleet[I]->UpdatePose(*Terrain);}
        Earthwork::Load Mass;Mass.Add(Earthwork::Material::Clay,24);Terrain->Deposit(Center,1.25,Mass);
        SoilMass=Terrain->MaterialBalance();SoilInitialHeight=Terrain->Height(Center);
        auto* PC=GetWorld()->GetFirstPlayerController();PC->SetViewTarget(this);if(auto* HUD=PC->GetHUD())HUD->bShowHUD=false;
        View({16,-20,14},{0,0,4});return;
    }
    if(bTerrainReview)
    {
        auto* PC=GetWorld()->GetFirstPlayerController();PC->SetViewTarget(this);
        if(auto* HUD=PC->GetHUD())HUD->bShowHUD=false;
        TerrainDriveLog=TEXT("stage,machine,travel_m\n");
        UE_LOG(LogTemp,Display,TEXT("Terrain review world: %s"),*Terrain->GetWorldId());
        return;
    }
    Center=FVector2D(Terrain->Start.X+18,Terrain->Start.Y+18);
    for (int32 M=0; M<Earthwork::MaterialCount; ++M)
    {
        const FVector2D P=Center+FVector2D(0,(M-2)*6.0);
        Earthwork::Load Mass; Mass.Add(static_cast<Earthwork::Material>(M),14);
        if(!bDozerReview)
        {
            Terrain->Deposit(P,2.7,Mass);
            Terrain->CutPass(P+FVector2D(0.8,0),FVector2D(0,1),FVector2D(1.8,0.45),1.2,Terrain->Height(Center)+0.45);
        }
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
    if(bDozerReview)
    {
        Dozer->StopWork();Dozer->Position=Center;Dozer->Heading=0;
        Dozer->Blade=DozerRig::MaxLift-Dozer->BladeLift;Dozer->BladeYaw=-25;Dozer->BucketTilt=-8;Dozer->UpdatePose(*Terrain);
        View({6,-5,3.2},{2.5,0,1.});
    }
}
void AMudMaterialReview::Tick(float Dt)
{
    Super::Tick(Dt); ++Frame;
    if(bSoftSoilReview){if(Frame>400&&Frame<850)WorkTimings.Add(Dt*1000.);ReviewSoftSoil();return;}
    if(bTerrainReview){ReviewTerrain();return;}
    Terrain->UpdateTiles(Center);
    Terrain->UpdateSpills(1.0/60);
    if (Frame>=180 && Frame<300) Timings.Add(Dt*1000.0);
    if (Frame>=450 && Frame<535) WorkTimings.Add(Dt*1000.0);
    if (Frame==300) FScreenshotRequest::RequestScreenshot(Folder/TEXT("near.png"),false,false);
    if (Frame==305)
    {
        if(bDozerReview){Fleet[0]->BladeYaw=25;Fleet[0]->BucketTilt=8;Fleet[0]->UpdatePose(*Terrain);View({0,-5,3.1},{2.5,0,1.});}
        else View(FVector(24,-27,19),FVector(0,0,0.7));
    }
    if (Frame==365) FScreenshotRequest::RequestScreenshot(Folder/TEXT("overview.png"),false,false);
    if (Frame==370)
    {
        if(bDozerReview){Fleet[0]->Blade=DozerRig::MinLift-Fleet[0]->BladeLift;Fleet[0]->BladeYaw=-25;Fleet[0]->BucketTilt=8;Fleet[0]->UpdatePose(*Terrain);View({5,-5,2.6},{2.5,0,.8});}
        else View(FVector(7,-23,5),FVector(-1,-11,1));
    }
    if (!bDozerReview && Frame>370 && Frame<550)
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

void AMudMaterialReview::ReviewTerrain()
{
    // Actual rendered camera traversal, with production Drive/Work on three machines.
    // No terrain edits, profile loading, autosave or teleporting through test obstacles.
    struct FView { const TCHAR* Name; FVector2D Focus,Offset; double Height; int32 Machine; };
    const FView Views[]={
        {TEXT("yard-slope-low"),{236,1640},{-38,-14},4,0},
        {TEXT("ravine-low"),{337,1665},{-20,-38},4,4},
        {TEXT("terrace-aerial"),{440,1630},{-140,-160},140,3},
        {TEXT("western-ridges"),{535,610},{-145,-95},100,-1},
        {TEXT("tributary-valley"),{650,1320},{-105,-70},65,-1},
        {TEXT("eastern-rock"),{1690,790},{-100,-90},110,-1}};
    const int32 Stage=(Frame-1)/180;
    if(Stage>=UE_ARRAY_COUNT(Views)){FPlatformMisc::RequestExit(false);return;}
    const auto& V=Views[Stage];
    if(Stage!=TerrainStage)
    {
        TerrainStage=Stage;Center=V.Focus;
        const FVector2D CameraXY=V.Focus+V.Offset;
        const FVector Position(CameraXY.X,CameraXY.Y,Terrain->Height(CameraXY)+V.Height);
        const FVector Target(Center.X,Center.Y,Terrain->Height(Center)+1);
        Camera->SetWorldLocation(Position*100);Camera->SetWorldRotation((Target-Position).Rotation());
        if(V.Machine>=0)
        {
            auto* M=Fleet[V.Machine];M->StopWork();M->Halt();
            M->Position={Terrain->Start.X-20+Stage*20,Terrain->Start.Y-24};M->Heading=90;M->UpdatePose(*Terrain);
            DriveStart=M->Position;
        }
    }
    if(V.Machine>=0)
    {
        auto* M=Fleet[V.Machine];
        M->Drive(1.0/60,.35,0,false,*Terrain,Fleet);M->Work(1.0/60,0,0,*Terrain,Fleet);
    }
    Terrain->UpdateTiles(Center);Terrain->UpdateSpills(1.0/60);
    if(Frame%180==170)
    {
        if(!Terrain->ValidateCollisionQueries(Center)){FPlatformMisc::RequestExitWithStatus(false,1);return;}
        FScreenshotRequest::RequestScreenshot(Folder/(FString(V.Name)+TEXT(".png")),false,false);
        if(V.Machine>=0)
        {
            const double Travel=FVector2D::Distance(DriveStart,Fleet[V.Machine]->Position);
            TerrainDriveLog+=FString::Printf(TEXT("%s,%d,%.4f\n"),V.Name,V.Machine,Travel);
            UE_LOG(LogTemp,Display,TEXT("Terrain review drive: %s machine %d travel %.2f m"),V.Name,V.Machine,Travel);
            Fleet[V.Machine]->Halt();
        }
        FFileHelper::SaveStringToFile(TerrainDriveLog,*(Folder/TEXT("drive.csv")));
    }
}

void AMudMaterialReview::ReviewSoftSoil()
{
    constexpr double Step=1./60;
    const double Started=FPlatformTime::Seconds();
    auto* Dozer=Fleet[0];auto* Loader=Fleet[4];
    Terrain->AdvancePours(Step);
    if(Frame>150)Terrain->AdvanceSoil(Step);
    if(Frame==150)FScreenshotRequest::RequestScreenshot(Folder/TEXT("soil-steep-before.png"),false,false);
    if(Frame==390)
    {
        const double Drop=SoilInitialHeight-Terrain->Height(Center);
        SoftSoilPassed&=Drop>1;
        UE_LOG(LogTemp,Display,TEXT("Soft soil visible collapse: %.3f m, active %d"),Drop,Terrain->ActiveSoilCells());
        FScreenshotRequest::RequestScreenshot(Folder/TEXT("soil-slump.png"),false,false);
    }
    if(Frame==400)
    {
        Center={210,760};Earthwork::Load WorkPile;WorkPile.Add(Earthwork::Material::Soil,16);
        Terrain->Deposit(Center,1.8,WorkPile);SoilMass+=WorkPile.Mass();View({7,-10,5},{0,0,1});
        Dozer->Position={200,760};Dozer->Heading=0;Dozer->Blade=0;Dozer->BladeYaw=0;Dozer->BucketTilt=0;
        Dozer->UpdatePose(*Terrain);Dozer->ToggleTool();
    }
    if(Frame>400&&Frame<=850)
    {
        if(Frame==590){Dozer->BladeYaw=25;Dozer->BucketTilt=8;}
        if(Frame==710){Dozer->BladeYaw=-25;Dozer->BucketTilt=-8;}
        Dozer->Drive(Step,.7,0,false,*Terrain,Fleet);Dozer->Work(Step,0,0,*Terrain,Fleet);
        DozerPeak=FMath::Max(DozerPeak,Dozer->Cargo.Volume());
    }
    if(Frame==850){Dozer->Halt();FScreenshotRequest::RequestScreenshot(Folder/TEXT("blade-push.png"),false,false);}
    if(Frame>850&&Frame<=1040)Dozer->Work(Step,1,0,*Terrain,Fleet);
    if(Frame==1040)
    {
        SoftSoilPassed&=DozerPeak>.2&&Dozer->Cargo.Volume()<.001;
        UE_LOG(LogTemp,Display,TEXT("Soft soil dozer: peak %.3f m3, remaining %.5f"),DozerPeak,Dozer->Cargo.Volume());
        FScreenshotRequest::RequestScreenshot(Folder/TEXT("blade-release.png"),false,false);
    }
    if(Frame==1050)
    {
        Dozer->StopWork();Dozer->Position={140,730};Dozer->UpdatePose(*Terrain);
        FVector2D Highest=Center;double Z=-BIG_NUMBER;
        for(int Y=754;Y<=766;++Y)for(int X=203;X<=223;++X)if(Terrain->Height({double(X),double(Y)})>Z){Z=Terrain->Height({double(X),double(Y)});Highest={double(X),double(Y)};}
        Loader->StopWork();Loader->Position=Highest-FVector2D(7,0);Loader->Heading=0;Loader->Boom=LoaderRig::MinBoom;Loader->Curl=-12;Loader->UpdatePose(*Terrain);
        Loader->Position.X+=Highest.X-2.8-Loader->ToolPoint().X;Loader->UpdatePose(*Terrain);
        View({4,-11,4},{0,0,1});
    }
    if(Frame>1050&&Frame<=1460)
    {
        const FVector Tip=Loader->ToolPoint();const double Depth=Terrain->Height({Tip.X,Tip.Y})-Tip.Z;
        const bool Loaded=Loader->Cargo.Volume()>.55;
        const double Lift=Loaded?.8:Depth>.28?.7:Depth<.04?-.3:0;
        Loader->Drive(Step,Loaded?-.35:Depth>.5?0:.45,0,false,*Terrain,Fleet);
        Loader->Work(Step,Lift,Loaded?-.7:0,*Terrain,Fleet);
        LoaderPeak=FMath::Max(LoaderPeak,Loader->Cargo.Volume());
    }
    if(Frame==1460){Loader->Halt();Center=Loader->Position+FVector2D(2,0);View({7,-10,5},{0,0,1});FScreenshotRequest::RequestScreenshot(Folder/TEXT("loader-carry.png"),false,false);}
    if(Frame>1460&&Frame<=1700)Loader->Work(Step,0,1,*Terrain,Fleet);
    if(Frame==1700)
    {
        FScreenshotRequest::RequestScreenshot(Folder/TEXT("loader-tip.png"),false,false);
        SoftSoilPassed&=LoaderPeak>.2&&Loader->Cargo.Volume()<.001;
        UE_LOG(LogTemp,Display,TEXT("Soft soil loader: peak %.3f m3, remaining %.5f"),LoaderPeak,Loader->Cargo.Volume());
    }
    double Mass=Terrain->MaterialBalance();for(auto* Machine:Fleet)Mass+=Machine->Cargo.Mass();
    SoilMassError=FMath::Max(SoilMassError,FMath::Abs(Mass-SoilMass));
    const double SimMs=(FPlatformTime::Seconds()-Started)*1000;
    const double MeshStarted=FPlatformTime::Seconds();
    Terrain->UpdateTiles(Center);Terrain->UpdateSpills(Step);
    if(Frame>400&&Frame<850)
    {
        if(TerrainDriveLog.IsEmpty())TerrainDriveLog=TEXT("frame,simulation_ms,terrain_render_update_ms,soil_solver_ms\n");
        TerrainDriveLog+=FString::Printf(TEXT("%d,%.4f,%.4f,%.4f\n"),Frame,SimMs,(FPlatformTime::Seconds()-MeshStarted)*1000,Terrain->SoilStepMilliseconds);
    }
    if(Frame>=1800&&(!Terrain->ActiveSoilCells()||Frame==2400))
    {
        const bool Settled=Terrain->ActiveSoilCells()==0;
        const auto Saved=Terrain->SaveTerrain();
        const double Before=Terrain->Height(Center);
        SoftSoilPassed&=Terrain->RestoreTerrain(Saved)&&FMath::Abs(Terrain->Height(Center)-Before)<.00001;
        SoftSoilPassed&=SoilMassError<.00001&&Settled;
        const FString Stats=FString::Printf(TEXT("dozer_peak_m3,loader_peak_m3,max_mass_error_t,passed\n%.6f,%.6f,%.9f,%d\n"),DozerPeak,LoaderPeak,SoilMassError,SoftSoilPassed);
        FFileHelper::SaveStringToFile(Stats,*(Folder/TEXT("soft-soil.csv")));
        FFileHelper::SaveStringToFile(TerrainDriveLog,*(Folder/TEXT("costs.csv")));
        if(!WorkTimings.IsEmpty())
        {
            WorkTimings.Sort();double Mean=0;for(double Time:WorkTimings)Mean+=Time/WorkTimings.Num();
            FFileHelper::SaveStringToFile(FString::Printf(TEXT("mean_ms,p95_ms,mean_fps\n%.3f,%.3f,%.2f\n"),
                Mean,WorkTimings[FMath::Min(WorkTimings.Num()-1,FMath::FloorToInt(WorkTimings.Num()*.95))],1000/Mean),*(Folder/TEXT("work-timings.csv")));
        }
        FFileHelper::SaveStringToFile(SoftSoilPassed?TEXT("passed"):TEXT("failed"),*(Folder/TEXT("Result.txt")));
        FPlatformMisc::RequestExitWithStatus(false,SoftSoilPassed?0:1);
    }
}
