#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Earthwork
{
enum class Material : uint8_t { Soil, Gravel, Rock, Count };
constexpr auto MaterialCount = static_cast<size_t>(Material::Count);
constexpr double Epsilon = 1e-6;
constexpr std::array<double, MaterialCount> TonnesPerM3{1.65, 1.8, 2.1};

struct Load
{
    std::array<double, MaterialCount> Volumes{};

    double Volume() const { return Volumes[0] + Volumes[1] + Volumes[2]; }
    Material DominantMaterial() const
    {
        return static_cast<Material>(std::max_element(Volumes.begin(), Volumes.end()) - Volumes.begin());
    }
    double Mass() const
    {
        double Total = 0;
        for (size_t Index = 0; Index < MaterialCount; ++Index)
            Total += Volumes[Index] * TonnesPerM3[Index];
        return Total;
    }

    void Add(Material Type, double Amount) { Volumes[static_cast<size_t>(Type)] += Amount; }

    Load Take(double Limit)
    {
        Load Result;
        const double Total = Volume();
        if (Limit <= 0 || Total <= 0)
            return Result;
        const double Fraction = std::min(Limit / Total, 1.0);
        for (size_t Index = 0; Index < MaterialCount; ++Index)
        {
            Result.Volumes[Index] = Volumes[Index] * Fraction;
            Volumes[Index] -= Result.Volumes[Index];
        }
        return Result;
    }

    void Receive(const Load& Other)
    {
        for (size_t Index = 0; Index < MaterialCount; ++Index)
            Volumes[Index] += Other.Volumes[Index];
    }
};

struct Layer
{
    Material Type;
    double Volume;
};

// Each height sample represents a one-square-metre column.
struct Column
{
    double Bedrock = 0;
    std::vector<Layer> Layers;

    double Height() const
    {
        double Height = Bedrock;
        for (const Layer& Entry : Layers)
            Height += Entry.Volume;
        return Height;
    }

    Material Surface() const { return Layers.empty() ? Material::Rock : Layers.back().Type; }

    void Add(Material Type, double Volume)
    {
        if (Volume <= 0)
            return;
        if (!Layers.empty() && Layers.back().Type == Type)
            Layers.back().Volume += Volume;
        else
            Layers.push_back({Type, Volume});
    }

    Load Cut(double Volume)
    {
        Load Result;
        while (Volume > 0 && !Layers.empty())
        {
            Layer& Top = Layers.back();
            const double Taken = std::min(Volume, Top.Volume);
            Result.Add(Top.Type, Taken);
            Top.Volume -= Taken;
            Volume -= Taken;
            if (Top.Volume <= 0)
                Layers.pop_back();
        }
        return Result;
    }

    void Fill(const Load& Source)
    {
        // Rock first, then soil and a gravel wearing course.
        Add(Material::Rock, Source.Volumes[2]);
        Add(Material::Soil, Source.Volumes[0]);
        Add(Material::Gravel, Source.Volumes[1]);
    }
};

inline double SurfaceGrip(Material Type, double Wetness, double Submersion)
{
    constexpr std::array<double, MaterialCount> DryGrip{0.82, 1.0, 0.93};
    const double Drainage = Type == Material::Soil ? 0.82 : 0.26;
    const double Grip = DryGrip[static_cast<size_t>(Type)] - Wetness * Drainage - Submersion * 0.44;
    return std::clamp(Grip, 0.12, 1.0);
}

inline double FordingLimit(double Clearance, double LoadFraction)
{
    return Clearance - 0.43 * std::clamp(LoadFraction, 0.0, 1.0);
}
}
