#pragma once
#include <algorithm>
#include <cmath>

namespace TerrainSurface
{
// Monotone cubic reconstruction: retains column heights and never invents extrema.
inline double Tangent(double A, double B)
{
    return A * B <= 0 ? 0 : 2 * A * B / (A + B);
}
inline double Curve(double A, double B, double C, double D, double T)
{
    const double M = Tangent(B-A, C-B), N = Tangent(C-B, D-C);
    const double T2 = T*T, T3 = T2*T;
    return std::clamp((2*T3-3*T2+1)*B + (T3-2*T2+T)*M +
        (-2*T3+3*T2)*C + (T3-T2)*N, std::min(B,C), std::max(B,C));
}
template<class Read> double Reconstruct(double X, double Y, Read Height)
{
    const int I = static_cast<int>(std::floor(X)), J = static_cast<int>(std::floor(Y));
    double Rows[4];
    for (int R = 0; R < 4; ++R)
        Rows[R] = Curve(Height(I-1,J+R-1), Height(I,J+R-1), Height(I+1,J+R-1), Height(I+2,J+R-1), X-I);
    return Curve(Rows[0],Rows[1],Rows[2],Rows[3],Y-J);
}
// The renderer and contact queries use these same half-metre triangles.
inline double Triangle(double SW, double SE, double NW, double NE, double U, double V)
{
    return U+V <= 1 ? SW+(SE-SW)*U+(NW-SW)*V : NE+(NW-NE)*(1-U)+(SE-NE)*(1-V);
}
}
