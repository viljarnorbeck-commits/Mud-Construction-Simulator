#include "MachineMotion.h"

#include <cassert>

using namespace MachineMotion;

static void Near(double Actual, double Expected)
{
    assert(std::abs(Actual - Expected) < 1e-8);
}

int main()
{
    constexpr double Frame = 1.0 / 60.0;
    const double Moving = Approach(0, 2, 4, Frame, 0.2);
    Near(Moving, 4 * Frame);
    const double Settling = Approach(1.9, 2, 4, Frame, 0.2);
    assert(Settling > 1.9 && Settling < 1.9 + Moving);
    Near(Approach(2, 2, 4, Frame, 0.2), 2);
    Near(Approach(-1, 1, 5, 0.4, 0.12),
        Approach(Approach(-1, 1, 5, 0.2, 0.12), 1, 5, 0.2, 0.12));
    assert(Approach(1, 0, 9, Frame, 0.08) < Approach(1, 0, 4, Frame, 0.2));

    double Rate = 0;
    const double Start = HydraulicTravel(Rate, 29, Frame, 0.10);
    assert(Start > 0 && Start < 29 * Frame / 4);
    const double Next = HydraulicTravel(Rate, 29, Frame, 0.10);
    assert(Next > Start);
    double HigherFrameRate = 0;
    double TwoFrames = 0;
    for (int FrameIndex = 0; FrameIndex < 4; ++FrameIndex)
        TwoFrames += HydraulicTravel(HigherFrameRate, 29, Frame / 2, 0.10);
    Near(TwoFrames, Start + Next);
    Near(HigherFrameRate, Rate);
    const double Release = HydraulicTravel(Rate, 0, Frame, 0.10);
    assert(Release > HydraulicTravel(Rate, 0, Frame, 0.10));
    assert(Rate > 0);
    assert(HydraulicTravel(Rate, -29, 0.2, 0.10) < 0);
    assert(Rate < 0);
}
