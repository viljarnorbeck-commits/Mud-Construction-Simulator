#include "TerrainSurface.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace TerrainSurface;
    for (double A : {-8.0,0.0,4.0}) for (double B : {-3.0,0.0,7.0})
        for (double C : {-4.0,0.0,9.0}) for (double D : {-8.0,2.0,5.0})
            for (int I=0; I<=100; ++I)
            {
                const double H=Curve(A,B,C,D,I/100.0);
                assert(H>=std::min(B,C)-1e-9 && H<=std::max(B,C)+1e-9);
            }
    const auto Plane=[](int X,int Y){return 12.0+X*.2-Y*.17;};
    for (double X=2; X<7; X+=.071) for (double Y=2; Y<7; Y+=.093)
        assert(std::abs(Reconstruct(X,Y,Plane)-(12+X*.2-Y*.17))<1e-9);
    const auto Cut=[](int X,int Y){return X<0 ? 6.0 : (Y<0 ? 1.0 : 2.0);};
    for (double X=-2; X<=2; X+=.04) for (double Y=-2; Y<=2; Y+=.04)
    {
        const double H=Reconstruct(X,Y,Cut);
        assert(H>=1 && H<=6); // no ringing above a berm or below a trench
    }
    for (int X=-5; X<=5; ++X) for (int Y=-5; Y<=5; ++Y)
        assert(Reconstruct(X,Y,Cut)==Cut(X,Y)); // physical column datums stay exact
    assert(std::abs(Triangle(0,1,2,4,.3,.2)-.7)<1e-9);
    assert(std::abs(Triangle(0,1,2,4,.8,.7)-2.7)<1e-9);
    std::cout<<"Monotone surface: planes, no overshoot, excavation edges, column datums and contact triangles passed\n";
}
