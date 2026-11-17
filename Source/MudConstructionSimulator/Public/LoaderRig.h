#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace LoaderRig
{
// Points are side profiles in centimetres in their respective component's local space.
struct Point
{
    double X, Z;
    Point operator+(Point Other) const { return {X + Other.X, Z + Other.Z}; }
    Point operator-(Point Other) const { return {X - Other.X, Z - Other.Z}; }
    Point operator*(double Scale) const { return {X * Scale, Z * Scale}; }
};

inline Point Rotate(Point Value, double Radians)
{
    const double Cos = std::cos(Radians), Sin = std::sin(Radians);
    return {Value.X * Cos - Value.Z * Sin, Value.X * Sin + Value.Z * Cos};
}

constexpr Point Mount{63, 134};
constexpr Point Coupler{262, 0};
constexpr Point RockerPin{180, 29};
constexpr Point BucketEar{-2, 48};
constexpr Point Lip{172, -49};
constexpr Point TiltBase{82, 151};
constexpr Point TiltRod{-19, 23};
constexpr double LiftArmAcross = 48;
constexpr double ShellThickness = 4;
constexpr double ShellUnderhang = 5;
constexpr double RockerLength = 57;
constexpr double LinkLength = 83;
constexpr double MinBoom = -22, MaxBoom = 64;
constexpr double MinCurl = -55, MaxCurl = 38;

constexpr std::array<Point, 7> BucketProfile{{{36, 67}, {30, 42}, {32, 7}, {43, -24}, {62, -44}, {85, Lip.Z}, Lip}};
constexpr std::array<Point, 10> ArmProfile{{{-22, -17}, {85, -25}, {177, -23}, {248, -13}, {267, -10},
    {267, 10}, {248, 13}, {177, 27}, {85, 34}, {-22, 18}}};
constexpr std::array<Point, 6> RockerProfile{{{-30, 16}, {-14, -16}, {50, -13}, {63, -1}, {53, 11}, {-15, 33}}};

inline Point OuterBucketPoint(std::size_t Index)
{
    const double Offset = Index == BucketProfile.size() - 1 ? ShellThickness * 2 : ShellThickness;
    return BucketProfile[Index] - Point{Offset, ShellUnderhang};
}

inline Point LinkPin(Point Ear)
{
    const Point Span = Ear - RockerPin;
    const double Distance = std::hypot(Span.X, Span.Z);
    const double Run = (RockerLength * RockerLength - LinkLength * LinkLength + Distance * Distance) /
        (2 * std::max(Distance, 0.001));
    const double Rise = std::sqrt(std::max(0.0, RockerLength * RockerLength - Run * Run));
    const double Angle = std::atan2(Span.Z, Span.X);
    return RockerPin + Rotate({Run, Rise}, Angle);
}
}
