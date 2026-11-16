#pragma once

#include <algorithm>
#include <cmath>

namespace MachineMotion
{
inline double Approach(double Current, double Target, double MaxRate, double Dt, double Response)
{
    const double Difference = Target - Current;
    const double Distance = std::abs(Difference);
    const double SlowZone = MaxRate * Response;
    const double FullRateTime = std::max(0.0, (Distance - SlowZone) / MaxRate);
    const double Remaining = Dt <= FullRateTime ? Distance - Dt * MaxRate :
        std::min(SlowZone, Distance) * std::exp(-(Dt - FullRateTime) / Response);
    return Remaining <= 0.0001 ? Target : Target - std::copysign(Remaining, Difference);
}

// Integrate the valve's gradual response over the step so short inputs work at any frame rate.
inline double HydraulicTravel(double& Rate, double TargetRate, double Dt, double Response)
{
    const double Blend = -std::expm1(-Dt / Response);
    const double Travel = TargetRate * Dt + (Rate - TargetRate) * Response * Blend;
    Rate += (TargetRate - Rate) * Blend;
    if (std::abs(Rate - TargetRate) < 0.0001)
        Rate = TargetRate;
    return Travel;
}
}
