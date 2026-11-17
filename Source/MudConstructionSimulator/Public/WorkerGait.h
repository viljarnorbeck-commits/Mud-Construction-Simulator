#pragma once

#include <algorithm>
#include <cmath>

namespace WorkerGait
{
constexpr double WalkSpeed = 1.8;
constexpr double SprintSpeed = 5.1;
constexpr double WalkCycleMetres = 1.76;
constexpr double RunCycleMetres = 2.58;

struct Cycle
{
    double Phase = 0.90;
    double RunWeight = 0;

    bool Advance(double Distance, double Dt, double Scale)
    {
        const double Speed = Distance / Dt;
        const double Blend = std::clamp((Speed - WalkSpeed) / (3.1 - WalkSpeed), 0.0, 1.0);
        const double Target = Blend * Blend * (3 - 2 * Blend);
        RunWeight += (Target - RunWeight) * -std::expm1(-Dt * 13);
        const double CycleLength = (WalkCycleMetres + (RunCycleMetres - WalkCycleMetres) * RunWeight) * Scale;
        const double Next = Phase + Distance / CycleLength;
        const bool Contact = std::floor(Next * 2) > std::floor(Phase * 2);
        Phase = Next - std::floor(Next);
        return Contact;
    }
};
}
