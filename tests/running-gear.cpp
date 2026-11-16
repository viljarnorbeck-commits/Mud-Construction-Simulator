#include "RunningGear.h"

#include <cassert>

using namespace RunningGear;

static void Near(double Actual, double Expected, double Tolerance = 1e-9)
{
    assert(std::abs(Actual - Expected) < Tolerance);
}

int main()
{
    constexpr double Radius = 0.65;
    Near(RollDegrees(2 * Pi * Radius, Radius), -360);
    Near(RollDegrees(-2 * Pi * Radius, Radius), 360);
    Near(RollDegrees(0, Radius), 0);
    Near(Wrap(-30, 360), 330);

    assert(SideDistance(3, 0.5, -1.2) > SideDistance(3, 0.5, 1.2));
    Near(SideDistance(0, 0.5, -1.2), 0.6);
    Near(SideDistance(0, 0.5, 1.2), -0.6);
    Near(SteeringDegrees(0, 4.16, 1.22), 0);
    assert(SteeringDegrees(30, 4.16, 1.22) > 30);
    assert(SteeringDegrees(30, 4.16, -1.22) < 30);
    Near(SteeringDegrees(-30, 4.16, -1.22), -SteeringDegrees(30, 4.16, 1.22));

    constexpr TrackPath Belt{1.70, 0.47};
    constexpr double Straight = 2 * Belt.HalfStraight;
    constexpr double Arc = Pi * Belt.Radius;
    const auto Front = Belt.Sample(Straight + Arc / 2);
    Near(Front.Ahead, Belt.HalfStraight + Belt.Radius);
    Near(Front.Height, 0);
    Near(Front.Pitch, -90);
    assert(Belt.Sample(0.12).Ahead > Belt.Sample(0).Ahead);
    assert(Belt.Sample(Straight + Arc + 0.12).Ahead < Belt.Sample(Straight + Arc).Ahead);
    Near(Belt.Sample(-0.12).Ahead, Belt.Sample(Belt.Length() - 0.12).Ahead);

    const double Joins[] = {0, Straight, Straight + Arc, 2 * Straight + Arc};
    for (const double Join : Joins)
    {
        constexpr double Step = 1e-6;
        const auto Before = Belt.Sample(Join - Step);
        const auto After = Belt.Sample(Join + Step);
        Near(std::hypot(After.Ahead - Before.Ahead, After.Height - Before.Height), 2 * Step);
        Near(std::remainder(After.Pitch - Before.Pitch, 360), 0, 0.001);
    }
}
