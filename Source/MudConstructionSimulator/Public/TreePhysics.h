#pragma once
#include <algorithm>
#include <cmath>

namespace TreePhysics
{
struct Root
{
    double Diameter = 0.3, Height = 8;
    double Angle = 0, Rate = 0;
    bool Broken = false;
    double MassKg() const { return 520 * 3.141592653589793 * Diameter * Diameter * Height * 0.25 * 1.2; }
    bool Unsupported(double RemovedDepth) const { return RemovedDepth > 0.35 + Diameter; }
    double Capacity(double Moisture) const
    {
        return 55000 * std::pow(Diameter / 0.4, 2.8) * (1 - 0.45 * std::clamp(Moisture,0.0,1.0));
    }
    bool Push(double ForceN, double ContactHeight, double Moisture, double Dt)
    {
        if (Broken || Dt <= 0) return Broken;
        const double Limit = Capacity(Moisture);
        const double Moment = std::max(0.0,ForceN) * std::clamp(ContactHeight,0.1,Height);
        // Implicit critically-damped root rotation, stable at fixed 60 Hz.
        const double Target = Moment / (Limit / 0.30);
        const double Omega = 8;
        Rate = (Rate + Dt * Omega * Omega * (Target-Angle)) / (1+2*Omega*Dt+Omega*Omega*Dt*Dt);
        Angle = std::max(0.0,Angle+Rate*Dt);
        Broken = Angle >= 0.30;
        return Broken;
    }
};

inline double SegmentDistance(double X,double Y,double AX,double AY,double BX,double BY)
{
    const double DX=BX-AX,DY=BY-AY;
    const double T=std::clamp(((X-AX)*DX+(Y-AY)*DY)/std::max(1e-12,DX*DX+DY*DY),0.0,1.0);
    return std::hypot(X-AX-T*DX,Y-AY-T*DY);
}
}
