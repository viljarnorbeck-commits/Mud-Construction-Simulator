#include "MudWorkerAnimation.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"

namespace
{
enum class EWorkerClip : uint8 { Idle, Walk, Run, Jump, Land };
enum class EWorkerState : uint8 { Idle, Moving, Jump, Land };
constexpr const TCHAR* ClipNames[] = { TEXT("Idle"), TEXT("Walk"), TEXT("Run"), TEXT("Jump"), TEXT("Land") };
constexpr double LandingDuration = 0.23;
// The run FBX ends one frame before it returns to the first sample. Its left heel lands near mid-clip.
constexpr double RunClosingFrame = 1.0 / 24.0;
constexpr double RunLeftContact = 0.46;
}

struct FMudWorkerAnimProxy : FAnimInstanceProxy
{
    explicit FMudWorkerAnimProxy(UMudWorkerAnimation* Animation)
        : FAnimInstanceProxy(Animation)
    {
    }

    void PreUpdate(UAnimInstance* Instance, float Dt) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, Dt);
        const auto& Worker = *CastChecked<UMudWorkerAnimation>(Instance);
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sequences); ++Index)
            Sequences[Index] = Worker.Clips.IsValidIndex(Index) ? Worker.Clips[Index].Get() : nullptr;
        EWorkerState Wanted = EWorkerState::Idle;
        if (!Worker.bGrounded)
            Wanted = EWorkerState::Jump;
        else if (Worker.Landing > 0 && Worker.Speed < 0.85)
            Wanted = EWorkerState::Land;
        else if (Worker.Speed > (Current.State == EWorkerState::Moving ? 0.10 : 0.19))
            Wanted = EWorkerState::Moving;

        if (Wanted != Current.State)
        {
            Previous = Current;
            Current = {Wanted};
            BlendTime = 0;
            BlendDuration = Wanted == EWorkerState::Jump || Wanted == EWorkerState::Land || Previous.State == EWorkerState::Jump ? 0.10 : 0.15;
        }
        BlendTime = FMath::Min(BlendTime + Dt, BlendDuration);
        Current.Phase = Worker.Phase;
        Current.RunWeight = Worker.RunWeight;
        switch (Wanted)
        {
        case EWorkerState::Idle: Current.Time = IdleTime(Current.Time + Dt); break;
        case EWorkerState::Jump: Current.Time = Worker.AirTime; break;
        case EWorkerState::Land:
            Current.Time = (1.0 - Worker.Landing / LandingDuration) * Length(EWorkerClip::Land);
            break;
        default: break;
        }
        if (Previous.State == EWorkerState::Idle)
            Previous.Time = IdleTime(Previous.Time + Dt);
    }

    bool Evaluate(FPoseContext& Output) override
    {
        if (BlendTime >= BlendDuration)
        {
            Sample(Current, Output);
            return true;
        }
        FPoseContext Source(Output), Destination(Output);
        Sample(Previous, Source);
        Sample(Current, Destination);
        const float PreviousWeight = static_cast<float>(1.0 - FMath::SmoothStep(0.0, BlendDuration, BlendTime));
        Blend(Source, Destination, PreviousWeight, Output);
        return true;
    }

    struct PoseState
    {
        EWorkerState State = EWorkerState::Idle;
        double Time = 0;
        double Phase = 0;
        double RunWeight = 0;
    };

    const UAnimSequence* Clip(EWorkerClip Index) const { return Sequences[static_cast<int32>(Index)]; }

    double Length(EWorkerClip Index) const
    {
        const auto* Sequence = Clip(Index);
        return Sequence ? Sequence->GetPlayLength() : 0;
    }

    double IdleTime(double Time) const { return FMath::Fmod(Time, FMath::Max(Length(EWorkerClip::Idle), UE_DOUBLE_SMALL_NUMBER)); }

    static void Blend(FPoseContext& A, FPoseContext& B, float Weight, FPoseContext& Output)
    {
        FAnimationPoseData First(A), Second(B), Result(Output);
        FAnimationRuntime::BlendTwoPosesTogether(First, Second, Weight, Result);
    }

    void SampleClip(EWorkerClip Index, double Time, FPoseContext& Output) const
    {
        Output.ResetToRefPose();
        const auto* Sequence = Clip(Index);
        if (!Sequence)
        {
            Sequence = Clip(EWorkerClip::Idle);
            Time = 0;
        }
        if (Sequence)
        {
            FAnimationPoseData Pose(Output);
            Sequence->GetAnimationPose(Pose, FAnimExtractContext(FMath::Clamp(Time, 0.0, double(Sequence->GetPlayLength())), false));
        }
    }

    void SampleRun(double Phase, FPoseContext& Output) const
    {
        const double End = Length(EWorkerClip::Run);
        const double Time = FMath::Fmod(Phase + RunLeftContact, 1.0) * (End + RunClosingFrame);
        if (Time <= End || !Clip(EWorkerClip::Run))
        {
            SampleClip(EWorkerClip::Run, Time, Output);
            return;
        }
        FPoseContext Last(Output), First(Output);
        SampleClip(EWorkerClip::Run, End, Last);
        SampleClip(EWorkerClip::Run, 0, First);
        Blend(Last, First, 1.f - static_cast<float>((Time - End) / RunClosingFrame), Output);
    }

    void Sample(const PoseState& State, FPoseContext& Output) const
    {
        switch (State.State)
        {
        case EWorkerState::Idle: SampleClip(EWorkerClip::Idle, State.Time, Output); return;
        case EWorkerState::Jump: SampleClip(EWorkerClip::Jump, State.Time, Output); return;
        case EWorkerState::Land: SampleClip(EWorkerClip::Land, State.Time, Output); return;
        default: break;
        }
        const double WalkTime = State.Phase * Length(EWorkerClip::Walk);
        if (State.RunWeight <= 0.01 || !Clip(EWorkerClip::Run))
            SampleClip(EWorkerClip::Walk, WalkTime, Output);
        else if (State.RunWeight >= 0.99 || !Clip(EWorkerClip::Walk))
            SampleRun(State.Phase, Output);
        else
        {
            FPoseContext Walk(Output), Run(Output);
            SampleClip(EWorkerClip::Walk, WalkTime, Walk);
            SampleRun(State.Phase, Run);
            Blend(Walk, Run, 1.f - static_cast<float>(State.RunWeight), Output);
        }
    }

    const UAnimSequence* Sequences[UE_ARRAY_COUNT(ClipNames)]{};
    PoseState Current;
    PoseState Previous;
    double BlendTime = 0;
    double BlendDuration = 0.16;
};

void UMudWorkerAnimation::SetMotion(double SpeedMps, double GaitPhase, double RunBlend, double TimeInAir,
    double LandingRemaining, bool bOnGround)
{
    Speed = SpeedMps;
    Phase = GaitPhase;
    RunWeight = RunBlend;
    AirTime = TimeInAir;
    Landing = LandingRemaining;
    bGrounded = bOnGround;
}

void UMudWorkerAnimation::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    Clips.Reset();
    for (const TCHAR* Name : ClipNames)
        Clips.Add(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Generated/Worker/%s.%s"), Name, Name)));
}

FAnimInstanceProxy* UMudWorkerAnimation::CreateAnimInstanceProxy()
{
    return new FMudWorkerAnimProxy(this);
}

void UMudWorkerAnimation::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
    delete static_cast<FMudWorkerAnimProxy*>(Proxy);
}
