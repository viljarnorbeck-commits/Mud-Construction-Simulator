#include "Earthwork.h"
#include "RunningGear.h"
#include "TerrainPassage.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
using namespace Earthwork;
int main()
{
    std::ifstream File("world/42/acceptance-profile.csv");assert(File.good());
    std::map<std::pair<int,int>,Column> Ground;
    std::string Line;std::getline(File,Line);
    while(std::getline(File,Line)) {
        std::replace(Line.begin(),Line.end(),',',' ');std::istringstream Row(Line);
        int X,Y;double Z;assert(Row>>X>>Y>>Z);
        Ground[{X,Y}]={Z-60,{{Material::Rock,8},{Material::Clay,52}}};
    }
    const auto Height=[&](double X,double Y) {
        const int IX=static_cast<int>(std::floor(X)),IY=static_cast<int>(std::floor(Y));
        const double U=X-IX,V=Y-IY;
        const auto H=[&](int A,int B){return Ground.at({std::clamp(A,190,686),std::clamp(B,1635,1645)}).Height();};
        return (H(IX,IY)*(1-U)+H(IX+1,IY)*U)*(1-V)+(H(IX,IY+1)*(1-U)+H(IX+1,IY+1)*U)*V;
    };
    const auto Travel=[&]() {
        RunningGear::SoilDrivetrain Drive;double X=208,Speed=0;
        for(int I=0;I<12000&&X<674;++I) {
            const double Grade=(Height(X+2,1640)-Height(X-2,1640))*.25;
            const double MassRatio=15./(15.+24*.8*1.8); // FMX, 80% gravel load.
            Speed=Drive.Advance(Speed,1,13.5*std::sqrt(MassRatio),.8,0,.56,MassRatio,Grade,false,false,1./60);
            const double Next=std::clamp(X+Speed/60,195.,678.);
            const auto Belly=[&](double P){return TerrainPassage::BellyPenetration([&](double A,double B){return Height(P+A,1640+B);},-2.235,2.6,1.46,.312);};
            if(TerrainPassage::Grounds(Belly(X),Belly(Next)))Speed=0;else X=Next;
        }
        return X-208;
    };
    const double Before=Travel();assert(Before<35);
    double Initial=0;for(const auto& Entry:Ground)Initial+=Entry.second.Mass();
    Load Spoil;
    const double A=Height(200,1640),B=Height(680,1640);
    const auto Target=[&](int X){return A+(B-A)*(X-200)/480.;};
    // Cut first; all fill comes from the actual excavated material. No road objects or
    // replacement terrain heights. Surplus remains in a conserved spoil load.
    for(auto& [XY,C]:Ground)Spoil.Receive(C.CutToHeight(Target(XY.first)-.35,1000));
    double Moved=Spoil.Volume();assert(Moved>1000);
    for(auto& [XY,C]:Ground) {
        const double Need=std::max(0.,Target(XY.first)-.35-C.Height());
        assert(Spoil.Volume()+1e-6>=Need);C.Fill(Spoil.Take(Need));
    }
    // A finite external stockpile is also part of the mass ledger.
    Load Stock;Stock.Add(Material::Gravel,2000);Initial+=Stock.Mass();
    for(auto& [XY,C]:Ground) {
        C.Fill(Stock.Take(.35));
        for(int Pass=0;Pass<20;++Pass)C.Compact(.3,.1);
    }
    const double After=Travel();assert(After>450);
    double Final=Stock.Mass()+Spoil.Mass();for(const auto& Entry:Ground)Final+=Entry.second.Mass();
    assert(std::abs(Final-Initial)<1e-6);
    const auto Flat=[](double,double){return 0.;};
    assert(TerrainPassage::BellyPenetration(Flat,-2.5,2.6,1.46,.31)==0);
    const auto Ridge=[](double X,double){return std::max(0.,.9-std::abs(X));};
    assert(TerrainPassage::BellyPenetration(Ridge,-2.5,2.6,1.46,.31)>.5);
    assert(TerrainPassage::BellyPenetration(Ridge,-1.6,1.6,1.45,.46)<
        TerrainPassage::BellyPenetration(Ridge,-2.235,2.6,1.46,.312));
    const auto SideRidge=[](double X,double Y){return std::max(0.,.8-std::abs(X))*(1-std::abs(Y)/2);};
    assert(TerrainPassage::BellyPenetration(SideRidge,-2.235,2.6,1.46,.312)>.4);
    const auto Slope=[](double X,double Y){return X*.2+Y*.1;};
    assert(TerrainPassage::BellyPenetration(Slope,-2.235,2.6,1.46,.312)==0);
    assert(!TerrainPassage::Grounds(.3,.2)); // Can reverse off a grounding.
    std::cout<<"Generated terrain loaded truck: "<<Before<<" m before, "<<After<<" m after cut/fill/compaction; "<<Moved<<" m3 cut; mass error "<<Final-Initial<<" t\n";
}
