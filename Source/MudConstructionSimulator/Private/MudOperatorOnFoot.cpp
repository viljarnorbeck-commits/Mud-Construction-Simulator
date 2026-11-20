#include "MudGameMode.h"

#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MudAudio.h"
#include "MudMachine.h"
#include "MudTerrain.h"
#include "MudWorkerAnimation.h"

namespace
{
constexpr double PersonHeight = 1.82;
constexpr double PersonRadius = 0.27;
constexpr double JumpSpeed = 4.1;

FVector2D XY(const FVector& Point)
{
    return FVector2D(Point.X, Point.Y);
}
}

void AMudOperator::InitializeFootAvatar()
{
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Generated/Worker/Worker.Worker"));
    if (!Mesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Worker assets are missing. Generate them with tools/build-windows.ps1."));
        return;
    }
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const double MeshHeight = Bounds.BoxExtent.Z * 2;
    if (MeshHeight <= UE_SMALL_NUMBER)
        return;
    const double Scale = PersonHeight * 100 / MeshHeight;
    FootAvatar->SetSkeletalMesh(Mesh);
    FootAvatar->SetWorldScale3D(FVector(Scale));
    FootAvatar->SetAnimInstanceClass(UMudWorkerAnimation::StaticClass());
    DriverAvatar->SetSkinnedAssetAndUpdate(Mesh);
    FootAvatarGroundOffset = (Bounds.BoxExtent.Z - Bounds.Origin.Z) * Scale;
}

void AMudOperator::UpdateDriverAvatar(const AMudMachine* Machine)
{
    const auto* Mesh = Cast<USkeletalMesh>(DriverAvatar->GetSkinnedAsset());
    auto* Seat = Machine ? Machine->GetDriverSeat() : nullptr;
    if (!Mesh || !Seat || bCab)
    {
        DriverAvatar->SetVisibility(false);
        return;
    }
    if (DriverAvatar->GetAttachParent() == Seat)
    {
        DriverAvatar->SetVisibility(true);
        return;
    }

    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    const int32 Pelvis = Skeleton.FindBoneIndex(TEXT("Body"));
    if (Pelvis == INDEX_NONE)
        return;
    auto& Local = DriverAvatar->BoneSpaceTransforms;
    Local = Skeleton.GetRefBonePose();
    TArray<FTransform> RefPose;
    RefPose.SetNumUninitialized(Local.Num());
    for (int32 Index = 0; Index < Local.Num(); ++Index)
    {
        const int32 Parent = Skeleton.GetParentIndex(Index);
        RefPose[Index] = Parent == INDEX_NONE ? Local[Index] : Local[Index] * RefPose[Parent];
    }

    const FVector Hip = RefPose[Pelvis].GetLocation();
    const double PersonScale = FootAvatar->GetComponentScale().X;
    const double SeatScale = Seat->GetComponentScale().X;
    const auto Reach = [&](FName UpperName, FName LowerName, FName EndName,
        FVector FromSeatCm, FVector BendDirection, bool bFoot)
    {
        const int32 Upper = Skeleton.FindBoneIndex(UpperName);
        const int32 Lower = Skeleton.FindBoneIndex(LowerName);
        const int32 End = Skeleton.FindBoneIndex(EndName);
        if (Upper == INDEX_NONE || Lower == INDEX_NONE || End == INDEX_NONE ||
            Skeleton.GetParentIndex(Lower) != Upper)
            return;
        const FVector Start = RefPose[Upper].GetLocation();
        const FVector First = RefPose[Lower].GetLocation() - Start;
        const FVector Second = RefPose[End].GetLocation() - RefPose[Lower].GetLocation();
        const double A = First.Size(), B = Second.Size();
        const FVector Span = Hip + FromSeatCm / (PersonScale * SeatScale) - Start;
        const FVector Direction = Span.GetSafeNormal();
        const double Distance = FMath::Clamp(Span.Size(), FMath::Abs(A - B) + 0.01, A + B - 0.01);
        const FVector Bend = FVector::VectorPlaneProject(BendDirection, Direction).GetSafeNormal();
        const double Along = (A * A - B * B + Distance * Distance) / (2.0 * Distance);
        const FVector Joint = Direction * Along + Bend * FMath::Sqrt(FMath::Max(0.0, A * A - Along * Along));
        const FQuat UpperRotation = FQuat::FindBetweenVectors(First, Joint) * RefPose[Upper].GetRotation();
        const FQuat LowerRotation = FQuat::FindBetweenVectors(Second, Direction * Distance - Joint) * RefPose[Lower].GetRotation();
        Local[Upper].SetRotation(RefPose[Skeleton.GetParentIndex(Upper)].InverseTransformRotation(UpperRotation));
        Local[Lower].SetRotation(UpperRotation.Inverse() * LowerRotation);
        // The Worker's foot controls are parented to the rig root independently of the shin.
        if (bFoot)
            Local[End].SetTranslation(RefPose[Skeleton.GetParentIndex(End)].InverseTransformPosition(Start + Direction * Distance));
    };
    const double AnkleHeight = 10.0 - Seat->GetRelativeLocation().Z;
    Reach(TEXT("UpperLeg_L"), TEXT("LowerLeg_L"), TEXT("Foot_L"), {48, -12, AnkleHeight}, FVector::ForwardVector, true);
    Reach(TEXT("UpperLeg_R"), TEXT("LowerLeg_R"), TEXT("Foot_R"), {50, 12, AnkleHeight}, FVector::ForwardVector, true);
    const bool bSteeringWheel = Machine->Kind != EMudMachine::Excavator && Machine->Kind != EMudMachine::Dozer;
    const double HandX = bSteeringWheel ? 49 : 27;
    const double HandY = bSteeringWheel ? 17 : 30;
    const double HandZ = bSteeringWheel ? 37 : 15;
    Reach(TEXT("UpperArm_L"), TEXT("LowerArm_L"), TEXT("Wrist_L"), {HandX, -HandY, HandZ}, FVector(0, -0.5, -1), false);
    Reach(TEXT("UpperArm_R"), TEXT("LowerArm_R"), TEXT("Wrist_R"), {HandX, HandY, HandZ}, FVector(0, 0.5, -1), false);

    DriverAvatar->AttachToComponent(Seat, FAttachmentTransformRules::KeepRelativeTransform);
    DriverAvatar->SetRelativeTransform(FTransform(FQuat::Identity, -Hip * PersonScale, FVector(PersonScale)));
    DriverAvatar->MarkRefreshTransformDirty();
    DriverAvatar->SetVisibility(true);
}

bool AMudOperator::CanOccupyFoot(FVector2D Point, double Height, double Radius)
{
    const double Limit = Terrain->GetWorldSize() - 3;
    if (Point.X < 3 || Point.Y < 3 || Point.X > Limit || Point.Y > Limit || Terrain->HitsTree(Point, Radius + 0.20, false))
        return false;
    for (const auto* Machine : Fleet)
        if (FVector2D::DistSquared(Point, Machine->Position) < FMath::Square(9.0) &&
            Machine->BlocksPedestrian(Point, Height, Radius))
            return false;
    return true;
}

TOptional<FVector2D> AMudOperator::ExitPosition(const AMudMachine& Machine)
{
    const FTransform Cab = Machine.CabView();
    const FVector2D Forward = XY(Cab.GetUnitAxis(EAxis::X)).GetSafeNormal();
    const FVector2D Right(-Forward.Y, Forward.X);
    const FVector2D Seat = XY(Cab.GetLocation()) / 100.0;
    const double Limit = Terrain->GetWorldSize() - 3;
    const auto Clamp = [Limit](FVector2D Point)
    {
        return FVector2D(FMath::Clamp(Point.X, 3.0, Limit), FMath::Clamp(Point.Y, 3.0, Limit));
    };
    for (const double SideOffset : {2.05, 2.60, 3.20, 4.0, 5.0})
        for (const double Side : {-1.0, 1.0})
            for (const double Along : {0.0, -1.1, 1.1})
            {
                const FVector2D Point = Clamp(Seat + Right * (SideOffset * Side) + Forward * Along);
                if (Terrain->WaterDepth(Point) <= 0.65 && CanOccupyFoot(Point, Terrain->MeshHeight(Point), PersonRadius))
                    return Point;
            }
    return {};
}

void AMudOperator::ResetFootMotion(double Facing)
{
    FootVelocity = FVector2D::ZeroVector;
    FootFacing = Facing;
    FootHeight = Terrain->MeshHeight(OnFoot);
    FootCameraHeight = FootHeight;
    FootCameraBoom = FootDistance;
    FootAnimationSpeed = 0;
    FootGait = {};
    FootVerticalSpeed = FootAirTime = FootLanding = FootJumpBuffer = 0;
    bFootGrounded = true;
}

void AMudOperator::BrakeOn()
{
    if (const auto* Machine = GetMachine())
    {
        if (!bBrake && Machine->Kind != EMudMachine::Truck && FMath::Abs(Machine->Speed) > 0.7)
        {
            const bool bTracked = Machine->Kind == EMudMachine::Dozer || Machine->Kind == EMudMachine::Excavator;
            Audio->Play(TEXT("Brake"), Machine->GetActorLocation(), bTracked ? 0.26f : 0.58f, bTracked ? 0.68f : 1.f);
        }
        bBrake = true;
        bCruise = false;
    }
    else
        FootJumpBuffer = 0.14;
}

void AMudOperator::StepOnFoot(double Dt)
{
    const FVector2D Forward = XY(FRotator(0, LookYaw, 0).Vector());
    const FVector2D Right(-Forward.Y, Forward.X);
    FVector2D Input = Forward * FMath::Clamp(Throttle + FootPadForward, -1.0, 1.0) + Right * Steering;
    if (Input.SizeSquared() > 1)
        Input.Normalize();
    const double Wetness = FMath::Clamp(Terrain->WaterDepth(OnFoot) / 0.85, 0.0, 1.0);
    const double Wade = FMath::Lerp(1.0, 0.39, Wetness);
    const double MudDrag = 1.0 - Terrain->ContactSink(OnFoot, true, 0) * 2.1;
    const double SpeedLimit = (bSprint ? WorkerGait::SprintSpeed : WorkerGait::WalkSpeed) * FMath::Min(Wade, MudDrag);
    const FVector2D Target = !bFootGrounded && Input.IsNearlyZero() ? FootVelocity : Input * SpeedLimit;
    const FVector2D Change = Target - FootVelocity;
    const bool bBraking = Input.IsNearlyZero() || FVector2D::DotProduct(Input, FootVelocity) < 0;
    const double MaxDv = (bFootGrounded ? (bBraking ? 15.0 : 10.5) : 3.1) * Dt;
    FootVelocity += Change.SizeSquared() > FMath::Square(MaxDv) ? Change.GetSafeNormal() * MaxDv : Change;

    if (bFootGrounded && FootJumpBuffer > 0 && Wetness < 0.88)
    {
        bFootGrounded = false;
        FootVerticalSpeed = JumpSpeed;
        FootAirTime = FootLanding = FootJumpBuffer = 0;
        Audio->Play(TEXT("Jump"), FVector(OnFoot.X, OnFoot.Y, FootHeight) * 100, 0.42f);
    }
    FootJumpBuffer = FMath::Max(0.0, FootJumpBuffer - Dt);
    FootLanding = FMath::Max(0.0, FootLanding - Dt);
    const FVector2D Previous = OnFoot;
    const auto TryMove = [&](FVector2D Point)
    {
        const double Ground = Terrain->MeshHeight(Point);
        const double Distance = FVector2D::Distance(OnFoot, Point);
        const double Reach = bFootGrounded ? FMath::Max(0.065, Distance * 1.18)
            : FMath::Max(FootVerticalSpeed * Dt, 0.0) + 0.12;
        if (Ground > FootHeight + Reach || !CanOccupyFoot(Point, FMath::Max(FootHeight, Ground), PersonRadius))
            return false;
        OnFoot = Point;
        return true;
    };
    const FVector2D Step = FootVelocity * Dt;
    if (!Step.IsNearlyZero() && !TryMove(OnFoot + Step))
    {
        if (!FMath::IsNearlyZero(Step.X) && !TryMove(OnFoot + FVector2D(Step.X, 0)))
            FootVelocity.X = 0;
        if (!FMath::IsNearlyZero(Step.Y) && !TryMove(OnFoot + FVector2D(0, Step.Y)))
            FootVelocity.Y = 0;
    }

    const FVector2D Travel = OnFoot - Previous;
    const double Ground = Terrain->MeshHeight(OnFoot);
    if (bFootGrounded && Ground < FootHeight - FMath::Max(0.07, Travel.Size() * 1.18))
    {
        bFootGrounded = false;
        FootVerticalSpeed = FootAirTime = 0;
    }
    bool bLanded = false;
    if (bFootGrounded)
        FootHeight = Ground;
    else
    {
        FootAirTime += Dt;
        FootVerticalSpeed -= 10.8 * Dt;
        FootHeight += FootVerticalSpeed * Dt;
        if (FootHeight <= Ground && FootVerticalSpeed <= 0)
        {
            bFootGrounded = true;
            FootHeight = Ground;
            FootLanding = FootVerticalSpeed < -1.5 ? 0.23 : 0.0;
            bLanded = FootLanding > 0;
            FootVerticalSpeed = 0;
        }
    }

    if (bFootGrounded)
    {
        const double DistanceTravelled = Travel.Size();
        const bool bContact = FootGait.Advance(bLanded ? 0 : DistanceTravelled, Dt, FootAvatar->GetComponentScale().X);
        const FVector2D FacingRight = XY(FRotator(0, FootFacing + 90, 0).Vector());
        const FVector2D SoleXY = OnFoot + FacingRight * (FootGait.Phase < 0.5 ? -0.11 : 0.11);
        const FVector Sole = FVector(SoleXY.X, SoleXY.Y, FootHeight + 0.08) * 100;
        if (bLanded)
        {
            FootGait.Phase = FootGait.Phase < 0.5 ? 0.0 : 0.5;
            Audio->Footstep(*Terrain, Sole, 0.92f, true);
        }
        else if (bContact)
            Audio->Footstep(*Terrain, Sole, FMath::Lerp(0.54, 0.83, FootGait.RunWeight));
    }

    const FVector2D Direction = Travel.SizeSquared() > FMath::Square(0.1 * Dt) ? Travel : Input;
    if (!Direction.IsNearlyZero())
    {
        const double TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
        const double Turn = FMath::FindDeltaAngleDegrees(FootFacing, TargetYaw);
        const double Follow = 1.0 - FMath::Exp(-Dt * (Travel.Size() > 1.0 * Dt ? 19.0 : 10.0));
        FootFacing = FRotator::NormalizeAxis(FootFacing + FMath::Clamp(Turn * Follow, -560.0 * Dt, 560.0 * Dt));
    }
    FootAnimationSpeed = Travel.Size() / Dt;
}

void AMudOperator::UpdateFootView(double Dt)
{
    const double BootSink = bFootGrounded ? Terrain->ContactSink(OnFoot, true, 0) * 0.32 : 0;
    FootAvatar->SetWorldLocationAndRotation(
        FVector(OnFoot.X * 100, OnFoot.Y * 100, (FootHeight - BootSink) * 100 + FootAvatarGroundOffset),
        FRotator(0, FootFacing, 0));
    if (auto* Animation = Cast<UMudWorkerAnimation>(FootAvatar->GetAnimInstance()))
        Animation->SetMotion(FootAnimationSpeed, FootGait.Phase, FootGait.RunWeight, FootAirTime, FootLanding, bFootGrounded);

    FootCameraHeight = FMath::Lerp(FootCameraHeight, FootHeight, Dt > 0 ? 1.0 - FMath::Exp(-Dt * 10.0) : 1.0);
    const FRotator Rotation(LookPitch, LookYaw, 0);
    const double BodyCenter = PersonHeight * 0.5 - BootSink;
    const FVector Focus = FVector(OnFoot.X, OnFoot.Y, FootCameraHeight + BodyCenter) * 100;
    const FVector Back = -Rotation.Vector() * 100;
    double ClearLength = FootDistance;
    for (int32 I = 1; I <= 16; ++I)
    {
        const double ProbeDistance = FootDistance * I / 16;
        const FVector Sample = Focus + Back * ProbeDistance;
        if (Sample.Z <= (Terrain->MeshHeight(XY(Sample) / 100.0) + 0.25) * 100 ||
            !CanOccupyFoot(XY(Sample) / 100.0, Sample.Z / 100.0, 0.14))
        {
            ClearLength = FMath::Max(1.10, FootDistance * (I - 1) / 16);
            break;
        }
    }
    FootCameraBoom = ClearLength <= FootCameraBoom || Dt <= 0 ? ClearLength :
        FMath::Lerp(FootCameraBoom, ClearLength, 1.0 - FMath::Exp(-Dt * 7.0));
    FVector View = Focus + Back * FootCameraBoom;
    View.Z = FMath::Max(View.Z, (Terrain->MeshHeight(XY(View) / 100.0) + 0.25) * 100);
    const FVector Target = FVector(OnFoot.X, OnFoot.Y, FootHeight + BodyCenter) * 100;
    Camera->SetWorldLocationAndRotation(View, (Target - View).Rotation());
}
