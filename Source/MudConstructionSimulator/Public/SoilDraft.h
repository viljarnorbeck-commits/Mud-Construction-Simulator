#pragma once
#include "Earthwork.h"
#include <algorithm>
#include <cmath>

namespace SoilDraft
{
// Plastic work needed to shear one reference cubic metre under a blunt hull.
// kN/m2 * m3 = kJ. Fixed rock is deliberately excluded by DisplaceBelow.
inline double ShearWork(const Earthwork::Layer& Layer,double Wet)
{
    const auto& P=Earthwork::Properties(Layer.Type);Wet=std::clamp(Wet,0.,1.);
    const double Mu=P.DryFriction+(P.WetFriction-P.DryFriction)*Wet;
    return Earthwork::TonnesPerM3[static_cast<size_t>(Layer.Type)]*9.81*
        (.35*Mu+2*P.Cohesion*(1-.8*Wet)+.25*P.DigResistance)*(1+2*Layer.Packing);
}
inline Earthwork::Load DisplaceBelow(Earthwork::Column& Column,double Floor,double Wet,double& WorkKJ)
{
    Earthwork::Load Result;
    while(WorkKJ>Earthwork::Epsilon&&!Column.Layers.empty()&&Column.Height()>Floor+Earthwork::Epsilon)
    {
        const auto Top=Column.Layers.back();
        if(Top.Type==Earthwork::Material::Rock&&!Top.Loose)break;
        const double Cost=ShearWork(Top,Wet);
        const double Volume=std::min({Top.Volume,(Column.Height()-Floor)/Top.BulkFactor(),WorkKJ/Cost});
        const auto Cut=Column.Cut(Volume);Result.Receive(Cut);WorkKJ=std::max(0.,WorkKJ-Cut.Volume()*Cost);
        if(Cut.Volume()<Earthwork::Epsilon)break;
    }
    return Result;
}
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
