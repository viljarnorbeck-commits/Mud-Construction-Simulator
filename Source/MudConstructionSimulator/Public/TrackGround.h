#pragma once
#include <algorithm>
#include <cmath>

namespace TrackGround
{
struct Support
{
    double Height, Pitch, Crossfall;
    double At(double X,double Y) const { return Height+Pitch*X+Crossfall*Y; }
};
// A rigid track frame rests on its whole footprint, including the rounded idler
// approach. A crest lifts the supported frame instead of becoming a phantom wall
// between two axle endpoints. The centre gap is deliberately NOT a support patch.
template<class Ground>
Support Fit(Ground&& Height,double HalfLength,double Radius,double Gauge,double HalfWidth)
{
    double Front=0,Rear=0;
    for(double Side:{-1.,1.}) {
        Front+=Height(HalfLength,Side*Gauge)*.5;
        Rear+=Height(-HalfLength,Side*Gauge)*.5;
    }
    const double Pitch=std::clamp((Front-Rear)/(2*HalfLength),-1.5,1.5);
    double Levels[2]{-1e30,-1e30};
    const double Extent=HalfLength+Radius;
    const int Steps=static_cast<int>(std::ceil(2*Extent/.16));
    for(int S=0;S<2;++S)for(int I=0;I<=Steps;++I)for(double Lateral:{-.8,0.,.8}) {
        const double X=-Extent+2*Extent*I/Steps;
        const double Nose=std::max(0.,std::abs(X)-HalfLength);
        const double Rise=Radius-std::sqrt(std::max(0.,Radius*Radius-Nose*Nose));
        const double Y=(S?1:-1)*Gauge+Lateral*HalfWidth;
        Levels[S]=std::max(Levels[S],Height(X,Y)-Pitch*X-Rise);
    }
    return {(Levels[0]+Levels[1])*.5,Pitch,(Levels[1]-Levels[0])/(2*Gauge)};
}
}
