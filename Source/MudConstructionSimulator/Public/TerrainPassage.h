#pragma once
#include <algorithm>
#include <cmath>

namespace TerrainPassage
{
// Sample the underside between the actual support rows. A convex crest can ground
// the chassis even when its average longitudinal grade is small. Five lateral rows
// also account for the width needed by a vehicle, rather than testing its centre only.
template<class Height>
double BellyPenetration(Height&& Ground, double Rear, double Front, double HalfWidth, double Clearance, double BodyWidth=-1)
{
    double Penetration=0;
    // Fit one axle plane across both sides: do not let each side independently float
    // over a narrow ridge underneath the vehicle.
    const double RL=Ground(Rear,-HalfWidth),RR=Ground(Rear,HalfWidth);
    const double FL=Ground(Front,-HalfWidth),FR=Ground(Front,HalfWidth);
    if(BodyWidth<0)BodyWidth=HalfWidth;
    const int Steps=std::max(8,static_cast<int>(std::ceil((Front-Rear)/.25)));
    for(int X=0;X<=Steps;++X)for(int Y=0;Y<=4;++Y)
    {
        const double U=X/static_cast<double>(Steps),Lateral=(Y/4.0*2-1)*BodyWidth,V=(Lateral/HalfWidth+1)*.5;
        const double Left=RL+(FL-RL)*U,Right=RR+(FR-RR)*U;
        const double Plane=Left+(Right-Left)*V;
        Penetration=std::max(Penetration,Ground(Rear+(Front-Rear)*U,Lateral)-Plane-Clearance);
    }
    return Penetration;
}
inline bool Grounds(double Before,double After)
{
    // Allow reversing out of a pre-existing grounding and minor numerical changes.
    return After>0.025&&After>Before+0.00001;
}
}
