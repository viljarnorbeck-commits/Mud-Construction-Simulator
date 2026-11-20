#pragma once
#include "SoilDraft.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace DozerRig
{
constexpr double Pi=3.14159265358979323846;
constexpr double PushLength=420,PushHalfWidth=156,HalfBlade=172,MaxYaw=25,MaxTilt=8;
constexpr double MinLift=-.28,MaxLift=1.05;
struct Point { double X,Y,Z; };
constexpr Point Pivot{-115,0,51},Pin{-40,0,55};
inline double FrameAngle(double Lift){return std::asin(std::clamp((Lift*100+Pin.Z-Pivot.Z)/PushLength,-1.,1.));}
inline Point Pitch(Point P,double Angle){return {P.X*std::cos(Angle)-P.Z*std::sin(Angle),P.Y,P.X*std::sin(Angle)+P.Z*std::cos(Angle)};}
inline Point Rotate(Point P,double Yaw,double Tilt)
{
    const double A=Yaw*Pi/180,B=Tilt*Pi/180;
    const double Y=P.Y*std::cos(B)+P.Z*std::sin(B),Z=-P.Y*std::sin(B)+P.Z*std::cos(B);
    return {P.X*std::cos(A)-Y*std::sin(A),P.X*std::sin(A)+Y*std::cos(A),Z};
}
inline Point Neutral(double Lift)
{
    const double Z=Lift*100,Rise=Z+Pin.Z-Pivot.Z;
    return {Pivot.X+std::sqrt(std::max(0.,PushLength*PushLength-Rise*Rise))-Pin.X,0,Z};
}
inline Point BladeRotate(Point P,double Lift,double Yaw,double Tilt){return Pitch(Rotate(P,Yaw,Tilt),FrameAngle(Lift));}
inline Point Origin(double Lift,double Yaw,double Tilt)
{
    const auto N=Neutral(Lift),R=BladeRotate(Pin,Lift,Yaw,Tilt);
    return {N.X+Pin.X-R.X,N.Y+Pin.Y-R.Y,N.Z+Pin.Z-R.Z};
}
inline double Wing(double Across) { return std::max(0.,std::abs(Across)-117.)*.5; }
inline Point Edge(double Across) { return {12+Wing(Across),Across,-26.5}; }
struct ProfilePoint {double X,Z;};
inline constexpr std::array<ProfilePoint,7> Profile{{{12,-26.5},{0,-11},{-9,8},{-16,30},{-15,52},{-6,74},{13,94}}};
inline double FaceX(double Z,double Y){
    for(size_t I=1;I<Profile.size();++I)if(Z<=Profile[I].Z)
        return Profile[I-1].X+(Profile[I].X-Profile[I-1].X)*std::clamp((Z-Profile[I-1].Z)/(Profile[I].Z-Profile[I-1].Z),0.,1.)+Wing(Y);
    return Profile.back().X+Wing(Y);
}
using SoilDraft::DraftKN;
}
