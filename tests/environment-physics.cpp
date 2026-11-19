#include "TreePhysics.h"
#include "Earthwork.h"
#include "RunningGear.h"
#include <cassert>
#include <iostream>
int main()
{
    TreePhysics::Root Sapling;Sapling.Diameter=.14;Sapling.Height=6;
    auto Large=Sapling;Large.Diameter=.75;Large.Height=12;
    for(int I=0;I<180;++I){Sapling.Push(35000,1,.4,1.0/60);Large.Push(35000,1,.4,1.0/60);}
    assert(Sapling.Broken&&!Large.Broken);
    assert(!Large.Unsupported(.2)&&Large.Unsupported(1.2));
    auto Strong=Large;Strong.Angle=Strong.Rate=0;
    for(int I=0;I<300;++I)Strong.Push(600000,1,.4,1.0/60);
    assert(Strong.Broken);
    auto Recoil=Large;
    for(int I=0;I<3600;++I)Recoil.Push(0,1,.4,1.0/60);
    assert(Recoil.Angle<1e-6&&!Recoil.Broken);
    auto Wet=Large;assert(Wet.Capacity(.95)<Wet.Capacity(.1));
    assert(TreePhysics::SegmentDistance(4,1,0,0,8,0)==1);
    assert(TreePhysics::SegmentDistance(4,0,0,0,8,0)==0);
    std::cout<<"Small tree falls; large tree resists; sufficient torque uproots; roots settle; fallen trunk spans road\n";

    const auto Run=[](double Grade,double Grip,double Sink,bool Tracks){
        RunningGear::SoilDrivetrain D;double Speed=0;
        for(int I=0;I<600;++I)Speed=D.Advance(Speed,1,8,Grip,Sink,.65,.5,Grade,Tracks,false,1.0/60);
        return Speed;
    };
    assert(Run(.85,.8,0,false)<.1);
    assert(Run(.14,.8,0,false)>1);
    const double Mud=Run(0,.13,.47,false),Road=Run(0,.82,.01,false);
    assert(Mud<.1&&Road>2);
    assert(Run(0,.50,.12,true)>Mud+1);
    std::cout<<"Loaded truck fails steep ascent; graded route works; mud bogs wheels; aggregate restores traction; tracks differ\n";

    Earthwork::Column Ditch{0,{{Earthwork::Material::Clay,1}}};
    const double Water=2.7,Before=Water-Ditch.Height();
    Earthwork::Load Gravel;Gravel.Add(Earthwork::Material::Gravel,2.0);
    const double Total=Ditch.Mass()+Gravel.Mass();Ditch.Fill(Gravel);
    assert(Before>1.5&&Water-Ditch.Height()<0);
    assert(std::abs(Ditch.Mass()-Total)<1e-9);
    const auto Saved=Ditch;assert(Saved.Surface()==Earthwork::Material::Gravel&&Saved.Height()==Ditch.Height());
    std::cout<<"Actual material fill raises a water crossing, exposes gravel, and preserves mass\n";
}
