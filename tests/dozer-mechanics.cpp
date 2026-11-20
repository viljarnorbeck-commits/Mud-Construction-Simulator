#include "DozerRig.h"
#include "TrackGround.h"
#include "RunningGear.h"
#include "TerrainPassage.h"
#include <cassert>
#include <iostream>
using namespace DozerRig;
static double Distance(Point A,Point B){return std::sqrt(std::pow(A.X-B.X,2)+std::pow(A.Y-B.Y,2)+std::pow(A.Z-B.Z,2));}
static Point Frame(double Lift,Point P)
{
    const auto N=Neutral(Lift);const double S=(N.Z+Pin.Z-Pivot.Z)/PushLength,C=std::sqrt(1-S*S);
    return {Pivot.X+C*P.X-S*P.Z,Pivot.Y+P.Y,Pivot.Z+S*P.X+C*P.Z};
}
static Point InBlade(Point P,double Lift,double Yaw,double Tilt)
{
    const auto O=Origin(Lift,Yaw,Tilt);P={P.X-O.X,P.Y-O.Y,P.Z-O.Z};
    P=Pitch(P,-FrameAngle(Lift));
    const double A=Yaw*Pi/180,B=Tilt*Pi/180;
    const double X=P.X*std::cos(A)+P.Y*std::sin(A),Y=-P.X*std::sin(A)+P.Y*std::cos(A);
    return {X,Y*std::cos(B)-P.Z*std::sin(B),Y*std::sin(B)+P.Z*std::cos(B)};
}
static double PanelBack(double Z,double Y)
{
    const double Xs[]{12,0,-9,-16,-15,-6,13},Zs[]{-26.5,-11,8,30,52,74,94};
    for(int I=1;I<7;++I)if(Z<=Zs[I])return Xs[I-1]+(Xs[I]-Xs[I-1])*(Z-Zs[I-1])/(Zs[I]-Zs[I-1])+Wing(Y)-8;
    return 0;
}
int main()
{
    int Poses=0;
    for(double Lift=MinLift;Lift<=MaxLift+.001;Lift+=.133)for(double Yaw=-25;Yaw<=25;Yaw+=5)for(double Tilt=-8;Tilt<=8;Tilt+=2)
    {
        const auto O=Origin(Lift,Yaw,Tilt),R=BladeRotate(Pin,Lift,Yaw,Tilt);
        assert(Distance({O.X+R.X,O.Y+R.Y,O.Z+R.Z},Frame(Lift,{PushLength,0,0}))<1e-8);
        // C-frame beam corners remain behind the moldboard, even at full yaw/tilt.
        for(double X:{0.,PushLength-100,PushLength-72})for(double Y:{-166.5,166.5})for(double Z:{-17.,20.})
        {
            const auto P=InBlade(Frame(Lift,{X,Y,Z}),Lift,Yaw,Tilt);
            if(P.Z>-26.5&&P.Z<94&&std::abs(P.Y)<172)assert(P.X<PanelBack(P.Z,P.Y)-1);
        }
        for(double Side:{-1.,1.})
        {
            const Point Base=Frame(Lift,{PushLength-90,Side*PushHalfWidth,20});
            const auto Q=BladeRotate({-50,Side*85,38},Lift,Yaw,Tilt);const Point End{O.X+Q.X,O.Y+Q.Y,O.Z+Q.Z};
            assert(Distance(Base,End)>86);assert(Distance(Base,End)<85*2.05);
            // Sample the cylinder axis with its barrel radius against the real curved panel.
            for(int I=0;I<=30;++I) {
                const double T=I/30.;const auto P=InBlade({Base.X+(End.X-Base.X)*T,Base.Y+(End.Y-Base.Y)*T,Base.Z+(End.Z-Base.Z)*T},Lift,Yaw,Tilt);
                if(P.Z>-26.5&&P.Z<94&&std::abs(P.Y)<172)assert(P.X+7<PanelBack(P.Z,P.Y));
            }
        }
        const auto N=Neutral(Lift),BaseOffset=BladeRotate({-50,-55,-10},Lift,Yaw,0),EndOffset=BladeRotate({-55,-60,76},Lift,Yaw,Tilt);
        const Point TiltBase{N.X+Pin.X+BaseOffset.X,N.Y+Pin.Y+BaseOffset.Y,N.Z+Pin.Z+BaseOffset.Z};
        const Point TiltEnd{O.X+EndOffset.X,O.Y+EndOffset.Y,O.Z+EndOffset.Z};
        assert(Distance(TiltBase,TiltEnd)>41&&Distance(TiltBase,TiltEnd)<40*2.05);
        for(double Side:{-1.,1.}) {
            const Point LiftEnd=Frame(Lift,{PushLength-40,Side*78,15});
            assert(Distance({150,Side*110,190},LiftEnd)>101&&Distance({150,Side*110,190},LiftEnd)<205);
            for(int I=0;I<=20;++I) {
                const double T=I/20.;const auto P=Frame(Lift,{PushLength-80+80*T,Side*PushHalfWidth*(1-T),0});
                assert(P.X-12>205||P.Z-20>90); // Fork is ahead of, or above, the front idler.
                const auto Q=InBlade(P,Lift,Yaw,Tilt);
                if(Q.Z>-26.5&&Q.Z<94&&std::abs(Q.Y)<172)assert(Q.X+9<PanelBack(Q.Z,Q.Y));
            }
        }
        for(int I=0;I<=30;++I) {
            const double T=I/30.;const auto P=InBlade({TiltBase.X+(TiltEnd.X-TiltBase.X)*T,TiltBase.Y+(TiltEnd.Y-TiltBase.Y)*T,TiltBase.Z+(TiltEnd.Z-TiltBase.Z)*T},Lift,Yaw,Tilt);
            if(P.Z>-26.5&&P.Z<94&&std::abs(P.Y)<172)assert(P.X+6.5<PanelBack(P.Z,P.Y));
        }
        ++Poses;
    }
    const auto Flat=[](double,double){return 0.;};
    assert(std::abs(TrackGround::Fit(Flat,1.65,.405,1.1,.305).Height)<1e-9);
    const auto Mound=[](double X,double Y){return .65*std::exp(-X*X/1.2)*std::exp(-std::pow(std::abs(Y)-1.1,2)/.1);};
    const auto S=TrackGround::Fit(Mound,1.65,.405,1.1,.305);assert(S.Height>.6);
    const auto Between=[](double X,double Y){return .9*std::exp(-X*X)*std::exp(-Y*Y/.02);};
    assert(TrackGround::Fit(Between,1.65,.405,1.1,.305).Height<.001); // Centre rock is still a belly obstacle.
    const auto Plane=[](double X,double Y){return .2*X+.1*Y;};
    assert(TerrainPassage::BellyPenetration(Plane,-2.235,2.6,1.14,.36,.86)==0);
    const double Light=DraftKN(Earthwork::Material::Soil,.1,3.44,.12,1,0);
    const double Deep=DraftKN(Earthwork::Material::Soil,.1,3.44,.6,1,0);
    const double Huge=DraftKN(Earthwork::Material::Soil,.1,3.44,1.5,12,0);
    assert(Light>0&&Deep>Light*2&&Huge>19*4.8);
    assert(DraftKN(Earthwork::Material::Rock,.1,3.44,.6,1,0)>Deep);
    RunningGear::SoilDrivetrain Small,Large,Dry;double SpeedSmall=0,SpeedLarge=0,SpeedDry=0;
    for(int I=0;I<600;++I) {
        SpeedSmall=Small.Advance(SpeedSmall,1,2.35,.85,0,.405,1,0,true,false,1./60,Light/19);
        SpeedLarge=Large.Advance(SpeedLarge,1,2.35,.25,0,.405,1,0,true,false,1./60,Huge/19);
        SpeedDry=Dry.Advance(SpeedDry,1,2.35,.85,0,.405,1,0,true,false,1./60,Huge/19);
    }
    assert(SpeedSmall>.5);assert(SpeedLarge<.01);assert(Large.WheelSpeed>SpeedLarge+1);
    assert(SpeedDry<.01&&Dry.WheelSpeed<1); // High grip can stall the drivetrain rather than spin.
    std::cout<<Poses<<" PAT poses: fixed hinge, frame/panel clearance and hydraulic envelope passed; distributed track support and centre rock passed; small draft "<<Light<<" kN, deep "<<Deep<<" kN, overload "<<Huge<<" kN with bandspin\n";
}
