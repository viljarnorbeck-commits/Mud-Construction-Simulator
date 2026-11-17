#pragma once

#include <algorithm>
#include <cmath>

namespace RunningGear
{
constexpr double Pi = 3.14159265358979323846;

inline double Wrap(double Value, double Cycle)
{
    const double Phase = std::fmod(Value, Cycle);
    return Phase < 0 ? Phase + Cycle : Phase;
}

inline double SideDistance(double ForwardDistance, double YawRadians, double LateralOffset)
{
    return ForwardDistance - YawRadians * LateralOffset;
}

inline double RollDegrees(double Distance, double Radius)
{
    return -Distance / Radius * (180.0 / Pi);
}

inline double SteeringDegrees(double CenterDegrees, double Wheelbase, double LateralOffset)
{
    const double Angle = CenterDegrees * (Pi / 180.0);
    const double Sin = std::sin(Angle);
    return std::atan2(Wheelbase * Sin, Wheelbase * std::cos(Angle) - LateralOffset * Sin) * (180.0 / Pi);
}

struct TrackPressure
{
    int Wear;
    double Depth;
};

inline TrackPressure PressGround(int CurrentWear, double Contact, double Softness, double Wet, double Scrub, bool Tracked)
{
    const int Limit = Tracked ? static_cast<int>(std::lround(150 + Scrub * 105)) : 120;
    const int Gain = static_cast<int>(std::lround(Contact * (Tracked ? 50 + 68 * Scrub : 29) *
        (0.3 + Wet * 0.7) * (0.38 + Softness * 0.62)));
    const int Applied = std::clamp(Gain, 0, std::max(0, Limit - CurrentWear));
    const double Depth = (Applied / 255.0) * Softness *
        (Tracked ? 0.055 + Wet * 0.17 + Scrub * (0.14 + Wet * 0.20) : 0.025 + Wet * 0.13);
    return {CurrentWear + Applied, Depth};
}

struct TrackPath
{
    double HalfStraight;
    double Radius;

    struct ShoePose { double Ahead; double Height; double Pitch; };

    double Length() const { return 4 * HalfStraight + 2 * Pi * Radius; }

    ShoePose Sample(double Distance) const
    {
        const double Straight = 2 * HalfStraight;
        const double Arc = Pi * Radius;
        double Phase = Wrap(Distance, Length());
        if (Phase < Straight)
            return {Phase - HalfStraight, Radius, 0};
        Phase -= Straight;
        if (Phase < Arc)
        {
            const double Angle = Phase / Radius;
            return {HalfStraight + Radius * std::sin(Angle), Radius * std::cos(Angle), -Angle * (180.0 / Pi)};
        }
        Phase -= Arc;
        if (Phase < Straight)
            return {HalfStraight - Phase, -Radius, 180};
        const double Angle = (Phase - Straight) / Radius;
        return {-HalfStraight - Radius * std::sin(Angle), -Radius * std::cos(Angle), 180 - Angle * (180.0 / Pi)};
    }
};
}
