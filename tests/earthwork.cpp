#include "Earthwork.h"

#include <cassert>

using namespace Earthwork;

static void Near(double Actual, double Expect)
{
    assert(std::abs(Actual - Expect) < Epsilon);
}

int main()
{
    Column Source{11.5, {{Material::Soil, 5.0}, {Material::Gravel, 2.1}}};
    Column Road{8.0, {{Material::Soil, 0.4}}};
    const double Before = Source.Height() + Road.Height();

    Load Bucket = Source.Cut(3.0);
    Near(Bucket.Volume(), 3.0);
    Near(Bucket.Volumes[1], 2.1);
    Near(Bucket.Volumes[0], 0.9);
    assert(Bucket.DominantMaterial() == Material::Gravel);
    Near(Bucket.Mass(), 2.1 * 1.8 + 0.9 * 1.65);
    assert(Source.Surface() == Material::Soil);

    Load Truck;
    Truck.Receive(Bucket.Take(1.2));
    Near(Bucket.Volume(), 1.8);
    Road.Fill(Truck.Take(1.0));
    assert(Road.Surface() == Material::Gravel);
    Near(Truck.Volume(), 0.2);
    Near(Source.Height() + Road.Height() + Bucket.Volume() + Truck.Volume(), Before);

    Road.Fill(Bucket.Take(10.0));
    Road.Fill(Truck.Take(10.0));
    Near(Source.Height() + Road.Height(), Before);
    Near(Bucket.Volume() + Truck.Volume(), 0);

    auto Depleted = Source.Cut(100);
    Near(Depleted.Volume(), 4.1);
    Near(Source.Height(), Source.Bedrock);
    assert(Source.Surface() == Material::Rock);
    Near(Source.Cut(1).Volume(), 0);
    assert(SurfaceGrip(Material::Gravel, 0.8, 0) > SurfaceGrip(Material::Soil, 0.8, 0));
    assert(SurfaceGrip(Material::Gravel, 0.8, 1.1) < SurfaceGrip(Material::Gravel, 0.8, 0));
    assert(FordingLimit(1.05, 0.75) < 0.95 && FordingLimit(1.05, 0) > 0.95);
}
