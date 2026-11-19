#include "MachineMotion.h"

#include <cassert>

using namespace MachineMotion;

static void Near(double Actual, double Expected)
{
    assert(std::abs(Actual - Expected) < 1e-8);
}

int main()
{
    HydraulicInput Left;
    AccumulateMouseDrag(Left, true, false, MouseTool::Excavator, 0.6, -0.4);
    Near(Left.Boom, -0.4);
    Near(Left.Stick, 0);
    Near(Left.Curl, 0.6);
    Near(Left.Swing, 0);
    Near(Left.Tilt, 0);

    HydraulicInput Right;
    AccumulateMouseDrag(Right, false, true, MouseTool::Excavator, -0.3, 0.7);
    Near(Right.Boom, 0);
    Near(Right.Stick, 0.7);
    Near(Right.Curl, 0);
    Near(Right.Swing, -0.3);
    Near(Right.Tilt, 0);

    HydraulicInput Both;
    AccumulateMouseDrag(Both, true, true, MouseTool::Excavator, 0.4, -0.25);
    AccumulateMouseDrag(Both, false, false, MouseTool::Excavator, 1.0, 1.0);
    Near(Both.Boom, 0);
    Near(Both.Stick, 0);
    Near(Both.Curl, -0.25);
    Near(Both.Swing, 0);
    Near(Both.Tilt, 0.4);

    HydraulicInput Keys;
    AddExcavatorKeys(Keys, false, 1, -1);
    Near(Keys.Boom, -1);
    Near(Keys.Swing, 1);
    Near(Keys.Curl, 0);
    Near(Keys.Stick, 0);
    AddExcavatorKeys(Keys, true, -1, 1);
    Near(Keys.Boom, -1);
    Near(Keys.Swing, 1);
    Near(Keys.Curl, -1);
    Near(Keys.Stick, 1);

    HydraulicInput Blade;
    AccumulateMouseDrag(Blade, true, false, MouseTool::Blade, 1.0, -0.5);
    AccumulateMouseDrag(Blade, false, true, MouseTool::Blade, 1.0, 0.2);
    Near(Blade.Boom, -0.3);
    Near(Blade.Stick, 0);
    Near(Blade.Curl, 0);
    Near(Blade.Swing, 2.0);

    HydraulicInput Loader;
    AccumulateMouseDrag(Loader, true, false, MouseTool::Loader, 0.8, 0.6);
    Near(Loader.Boom, 0.6);
    Near(Loader.Curl, -0.8);
    AccumulateMouseDrag(Loader, false, true, MouseTool::Loader, -0.7, -0.5);
    Near(Loader.Boom, 0.6);
    Near(Loader.Curl, -1.3);
    Near(Loader.Stick, 0);
    Near(Loader.Swing, 0);

    constexpr double Frame = 1.0 / 60.0;
    const auto MouseLift = [](int Fps, double CountsPerSecond, MouseTool Tool)
    {
        const double Dt = 1.0 / Fps;
        const double MouseDrag = CountsPerSecond * Dt * MouseDragSensitivity(Tool);
        HydraulicInput Direct;
        AccumulateMouseDrag(Direct, true, false, Tool, 0, MouseDrag);
        const double Valve = std::clamp(Direct.Boom * MouseValveScale(Dt), -1.0, 1.0);
        double Rate = 0, Distance = 0;
        for (int Index = 0; Index < Fps / 2; ++Index)
            Distance += HydraulicTravel(Rate, Valve, Dt, 0.10);
        return Distance;
    };
    for (const auto Tool : {MouseTool::Excavator, MouseTool::Loader, MouseTool::Blade})
    {
        const double Slow = MouseLift(60, 120, Tool);
        assert(Slow > 0.04);
        Near(MouseLift(30, 120, Tool), Slow);
        Near(MouseLift(120, 120, Tool), Slow);
        Near(MouseLift(30, -1800, Tool), MouseLift(120, -1800, Tool));
    }

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

    TipControl Bed;
    auto OperateBed = [&](double Input, bool Automatic, int Frames)
    {
        for (int Index = 0; Index < Frames; ++Index)
            Bed.Step(Input, Automatic, Frame);
    };
    OperateBed(1, false, 54);
    assert(Bed.Angle > 25 && Bed.CanSpill(false));
    OperateBed(0, false, 120);
    Near(Bed.Angle, 36);
    OperateBed(-1, false, 30);
    assert(Bed.Angle < 25 && !Bed.CanSpill(false));
    OperateBed(-1, false, 120);
    Near(Bed.Angle, 0);
    Bed.Manual = false;
    OperateBed(0, true, 120);
    assert(Bed.Angle > 50 && Bed.CanSpill(true));
    OperateBed(0, false, 120);
    Near(Bed.Angle, 0);

    DirectionalTap Tap;
    Tap.Press(true);
    Near(Tap.Resolve(0, Frame), 1);
    Near(Tap.Resolve(-0.5, Frame), -0.5);
    for (int Index = 0; Index < 5; ++Index)
        Tap.Resolve(0, Frame);
    Near(Tap.Resolve(0, Frame), 0);
    Tap.Press(false);
    Near(Tap.Resolve(0, Frame), -1);
    Tap.Press(true);
    Near(Tap.Resolve(0, Frame), 0);
}
