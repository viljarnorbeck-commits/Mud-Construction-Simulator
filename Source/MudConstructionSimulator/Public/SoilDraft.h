#pragma once
#include "Earthwork.h"
#include <algorithm>
#include <cmath>

namespace SoilDraft
{
// Passive earth pressure plus a ground-supported sliding wedge, in kN.
// Normalized cohesion uses unit weight over a one-metre reference depth.
inline double DraftKN(Earthwork::Material Type,double Wet,double Width,double Depth,double WedgeTonnes,double Yaw)
{
    const auto& P=Earthwork::Properties(Type);Wet=std::clamp(Wet,0.,1.);
    const double Angle=(P.DryRepose+(P.WetRepose-P.DryRepose)*Wet)*3.14159265358979323846/180;
    const double S=std::sin(Angle),Passive=(1+S)/(1-S);
    const double UnitWeight=Earthwork::TonnesPerM3[static_cast<size_t>(Type)]*9.81;
    const double Cohesion=P.Cohesion*UnitWeight;
    const double Mu=P.DryFriction+(P.WetFriction-P.DryFriction)*Wet;
    const double D=std::max(0.,Depth);
    return (.5*UnitWeight*Passive*D*D+2*Cohesion*std::sqrt(Passive)*D)*Width*
        std::cos(Yaw*3.14159265358979323846/180)+std::max(0.,WedgeTonnes)*9.81*Mu;
}
}
