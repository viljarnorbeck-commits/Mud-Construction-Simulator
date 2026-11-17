#pragma once

#include "Animation/AnimInstance.h"
#include "MudWorkerAnimation.generated.h"

class UAnimSequence;
struct FMudWorkerAnimProxy;

UCLASS(Transient)
class UMudWorkerAnimation : public UAnimInstance
{
    GENERATED_BODY()

public:
    void SetMotion(double SpeedMps, double GaitPhase, double RunBlend, double TimeInAir, double LandingRemaining, bool bOnGround);

protected:
    void NativeInitializeAnimation() override;
    FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;

private:
    friend struct FMudWorkerAnimProxy;
    UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;
    double Speed = 0;
    double Phase = 0;
    double RunWeight = 0;
    double AirTime = 0;
    double Landing = 0;
    bool bGrounded = true;
};
