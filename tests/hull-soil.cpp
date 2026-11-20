#include "SoilDraft.h"
#include <cassert>
#include <iostream>
using namespace Earthwork;
int main()
{
    Column Soil{0,{{Material::Soil,2,.6}}};
    const double Initial=Soil.Mass(),Height=Soil.Height();double Work=0;
    assert(SoilDraft::DisplaceBelow(Soil,0,0,Work).Volume()==0);
    Work=1;const auto Small=SoilDraft::DisplaceBelow(Soil,0,0,Work);
    assert(Small.Volume()>0&&Small.Volume()<.1&&Soil.Height()<Height);
    assert(std::abs(Initial-Soil.Mass()-Small.Mass())<1e-9&&Work<1e-9);
    Column Fixed{0,{{Material::Rock,2,0,false},{Material::Soil,1}}};
    Work=1000;const auto Top=SoilDraft::DisplaceBelow(Fixed,0,1,Work);
    assert(Top.Volumes[0]==1&&Top.Volumes[2]==0&&Fixed.Height()==2);
    Column Loose{0,{{Material::Rock,2,0,true}}};
    Work=1000;assert(SoilDraft::DisplaceBelow(Loose,0,1,Work).Volume()==2);
    Column Dry{0,{{Material::Clay,2}}},Wet=Dry,Packed{0,{{Material::Clay,2,1}}};
    double A=2,B=2,C=2;
    const auto D=SoilDraft::DisplaceBelow(Dry,0,0,A),W=SoilDraft::DisplaceBelow(Wet,0,1,B),P=SoilDraft::DisplaceBelow(Packed,0,0,C);
    assert(W.Volume()>D.Volume()&&P.Volume()<D.Volume());
    std::cout<<"Hull shear: bounded work, mass, packing, moisture and intact rock passed\n";
}
