#pragma once

#include <algorithm>
#include <array>
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

struct ArticulatedStep
{
    double FrontYaw;
    double RearYaw;
    double RearTravel;
};

// Integrate one steering step with both axle centers rolling along their own frame.
inline ArticulatedStep MoveArticulated(double FrontTravel, double FrontToJoint, double JointToRear,
    double PreviousBend, double NextBend)
{
    const double Bend = (PreviousBend + NextBend) * 0.5;
    const double Delta = NextBend - PreviousBend;
    const double Sin = std::sin(Bend), Cos = std::cos(Bend);
    const double FrontYaw = (FrontTravel * Sin + JointToRear * Delta) / (FrontToJoint * Cos + JointToRear);
    return {FrontYaw, FrontYaw - Delta, FrontTravel * Cos + FrontToJoint * FrontYaw * Sin};
}

struct TrackPressure
{
    int Wear;
    double Depth;
};

inline double ContactSink(double Softness, double Wetness, double LoadFraction, bool Tracked)
{
    const double Saturation = std::clamp((Wetness - 0.20) / 0.80, 0.0, 1.0);
    const double Load = std::clamp(LoadFraction, 0.0, 1.0);
    return Softness * Saturation * Saturation * (Tracked ? 0.13 + Load * 0.13 : 0.23 + Load * 0.32);
}

inline TrackPressure PressGround(int CurrentWear, double Contact, double Softness, double Wet, double Scrub,
    bool Tracked, double LoadFactor = 1)
{
    const int Limit = Tracked ? static_cast<int>(std::lround(150 + Scrub * 105)) : static_cast<int>(120 + Scrub * 73);
    const int Gain = static_cast<int>(std::lround(Contact * LoadFactor * (Tracked ? 50 + 68 * Scrub : 29 + 39 * Scrub) *
        (0.3 + Wet * 0.7) * (0.38 + Softness * 0.62)));
    const int Applied = std::clamp(Gain, 0, std::max(0, Limit - CurrentWear));
    const double Depth = (Applied / 255.0) * Softness *
        (Tracked ? 0.055 + Wet * 0.17 + Scrub * (0.14 + Wet * 0.20) : 0.025 + Wet * 0.16 + Scrub * Wet * 0.15);
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

class ElevatedTrackPath
{
public:
    ElevatedTrackPath(double HalfContact, double SprocketX, double SprocketRise, double Radius)
        : Radius(Radius)
    {
        const double X[] = {SprocketX, HalfContact, -HalfContact};
        const double Z[] = {SprocketRise, 0, 0};
        for (std::size_t I = 0; I < Edges.size(); ++I)
        {
            const std::size_t Next = (I + 1) % Edges.size();
            auto& Edge = Edges[I];
            Edge.X = X[I];
            Edge.Z = Z[I];
            const double DX = X[Next] - X[I], DZ = Z[Next] - Z[I];
            Edge.Length = std::hypot(DX, DZ);
            Edge.UX = DX / Edge.Length;
            Edge.UZ = DZ / Edge.Length;
            Edge.Angle = std::atan2(DZ, DX);
        }
        for (std::size_t I = 0; I < Edges.size(); ++I)
        {
            auto& Edge = Edges[I];
            const auto& Next = Edges[(I + 1) % Edges.size()];
            Edge.Arc = Radius * Wrap(Edge.Angle - Next.Angle, 2 * Pi);
            Perimeter += Edge.Length + Edge.Arc;
        }
    }

    double Length() const { return Perimeter; }

    TrackPath::ShoePose Sample(double Distance) const
    {
        double Phase = Wrap(Distance, Perimeter);
        for (std::size_t I = 0; I < Edges.size(); ++I)
        {
            const auto& Edge = Edges[I];
            if (Phase < Edge.Length)
                return {Edge.X + Phase * Edge.UX - Radius * Edge.UZ,
                    Edge.Z + Phase * Edge.UZ + Radius * Edge.UX, Edge.Angle * (180.0 / Pi)};
            Phase -= Edge.Length;
            if (Phase < Edge.Arc)
            {
                const auto& Next = Edges[(I + 1) % Edges.size()];
                const double Angle = Edge.Angle - Phase / Radius;
                return {Next.X - Radius * std::sin(Angle), Next.Z + Radius * std::cos(Angle), Angle * (180.0 / Pi)};
            }
            Phase -= Edge.Arc;
        }
        const auto& First = Edges.front();
        return {First.X - Radius * First.UZ, First.Z + Radius * First.UX, First.Angle * (180.0 / Pi)};
    }

private:
    struct Tangent
    {
        double X, Z, UX, UZ, Length, Angle, Arc;
    };
    std::array<Tangent, 3> Edges{};
    double Radius;
    double Perimeter = 0;
};
}
