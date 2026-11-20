#pragma once

#include <algorithm>
#include <cmath>

namespace MachineMotion
{
constexpr double MaxBucketTilt = 36.0;

struct HydraulicInput
{
    double Boom = 0;
    double Stick = 0;
    double Curl = 0;
    double Swing = 0;
    double Tilt = 0;
};

enum class MouseTool { Excavator, Blade, Loader };

constexpr double MouseLookDegreesPerCount = 0.090;

inline double MouseDragSensitivity(MouseTool Tool)
{
    return Tool == MouseTool::Excavator ? 0.075 : 0.057;
}

inline double MouseValveScale(double FrameSeconds)
{
    // Convert accumulated mouse counts to a valve command based on their rate at 60 Hz.
    return 1.0 / (60.0 * FrameSeconds);
}

// Unreal sends positive Y for upward mouse motion.
inline void AccumulateMouseDrag(HydraulicInput& Input, bool Left, bool Right, MouseTool Tool, double X, double Y)
{
    if (!Left && !Right)
        return;
    if (Tool == MouseTool::Blade)
    {
        Input.Boom += Y;
        if(Left&&Right)Input.Tilt+=X;
        else Input.Swing += X;
        return;
    }
    if (Tool == MouseTool::Loader)
    {
        if (Left)
        {
            Input.Boom += Y;
            Input.Curl -= X;
        }
        else if (Right)
            Input.Curl += Y;
        return;
    }
    if (Left && Right)
    {
        Input.Tilt += X;
        Input.Curl += Y;
    }
    else if (Left)
    {
        Input.Boom += Y;
        Input.Curl += X;
    }
    else
    {
        Input.Stick += Y;
        Input.Swing += X;
    }
}

inline void AddExcavatorKeys(HydraulicInput& Input, bool Shift, double Across, double Vertical)
{
    if (Shift)
    {
        Input.Stick += Vertical;
        Input.Curl += Across;
    }
    else
    {
        Input.Boom += Vertical;
        Input.Swing += Across;
    }
}

inline void AccumulateExcavatorGamepad(HydraulicInput& Input, bool Left, bool Right, double X, double Y)
{
    if (Left && Right)
    {
        Input.Tilt += X;
        Input.Curl += Y;
    }
    else if (Left || Right)
        AddExcavatorKeys(Input, Right, X, Y);
}

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

struct TipControl
{
    double Angle = 0;
    double ManualTarget = 0;
    bool Manual = false;

    void Step(double Input, bool Automatic, double Dt)
    {
        if (std::abs(Input) > 0.08)
        {
            if (!Manual)
                ManualTarget = Angle;
            Manual = true;
            ManualTarget = std::clamp(ManualTarget + std::clamp(Input, -1.0, 1.0) * 40 * Dt, 0.0, 51.0);
        }
        const double Target = Manual ? ManualTarget : (Automatic ? 51.0 : 0.0);
        Angle = Approach(Angle, Target, Target > Angle ? 42.0 : 58.0, Dt, 0.15);
        if (Target == 0 && Angle < 0.12)
            Angle = 0;
    }

    bool CanSpill(bool Automatic) const { return (Manual || Automatic) && Angle >= 25; }
};

struct DirectionalTap
{
    double Positive = 0;
    double Negative = 0;

    void Press(bool Forward) { (Forward ? Positive : Negative) = 0.08; }

    double Resolve(double Axis, double Dt)
    {
        // Keyboard transitions can occur between two samples of the continuous axis.
        const double Tap = static_cast<double>(Positive > 0) - static_cast<double>(Negative > 0);
        Positive = std::max(0.0, Positive - Dt);
        Negative = std::max(0.0, Negative - Dt);
        return Axis != 0 ? Axis : Tap;
    }
};
}
