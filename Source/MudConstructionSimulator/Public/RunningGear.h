#pragma once

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
