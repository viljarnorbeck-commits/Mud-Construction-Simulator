#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>

namespace Earthwork
{
enum class Material : uint8_t { Soil, Gravel, Rock, Sand, Clay, Count };
constexpr auto MaterialCount = static_cast<size_t>(Material::Count);
constexpr double Epsilon = 1e-6;
constexpr std::array<double, MaterialCount> TonnesPerM3{1.65, 1.8, 2.1, 1.55, 1.7};

struct MaterialProperties
{
    double DryFriction, WetFriction;
    double DryRepose, WetRepose;
    double Cohesion;
    double Bearing;
    double MaxCompression;
    double DigResistance;
};

inline constexpr std::array<MaterialProperties, MaterialCount> DefaultMaterialTable{{
    {0.82, 0.12, 38, 30, 0.12, 0.35, 0.22, 1.0},
    {1.00, 0.74, 36, 34, 0.02, 0.85, 0.13, 1.3},
    {0.93, 0.67, 48, 48, 0.15, 1.00, 0.04, 3.2},
    {0.67, 0.43, 31, 28, 0.00, 0.45, 0.18, 0.7},
    {0.72, 0.12, 42, 27, 0.30, 0.22, 0.26, 1.8}
}};
inline auto MaterialTable = DefaultMaterialTable;

inline const MaterialProperties& Properties(Material Type) { return MaterialTable[static_cast<size_t>(Type)]; }
inline bool IsAggregate(Material Type) { return Type == Material::Gravel || Type == Material::Rock; }

struct Load
{
    std::array<double, MaterialCount> Volumes{};

    double Volume() const
    {
        double Total = 0;
        for (double Value : Volumes) Total += Value;
        return Total;
    }
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
    // Reference loose volume determines mass. Packing changes occupied volume only.
    double Volume;
    double Packing = 0;
    bool Loose = true; // Excavated/deposited rock is rubble; original bedrock is not.
    double BulkFactor() const { return 1.0 - Properties(Type).MaxCompression * Packing; }
    double Height() const { return Volume * BulkFactor(); }
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
            Height += Entry.Height();
        return Height;
    }

    Material Surface() const { return Layers.empty() ? Material::Rock : Layers.back().Type; }

    void Add(Material Type, double Volume)
    {
        if (Volume <= 0)
            return;
        if (!Layers.empty() && Layers.back().Type == Type && Layers.back().Packing == 0 && Layers.back().Loose)
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

    // The tool plane is geometric; capacity and transported material use reference loose volume.
    Load CutToHeight(double Floor, double ReferenceLimit)
    {
        double RemainingDepth = std::max(0.0, Height() - Floor);
        double Amount = 0;
        for (auto It = Layers.rbegin(); It != Layers.rend() && RemainingDepth > 0; ++It)
        {
            const double Depth = std::min(RemainingDepth, It->Height());
            Amount += Depth / It->BulkFactor();
            RemainingDepth -= Depth;
        }
        return Cut(std::min(Amount, std::max(0.0, ReferenceLimit)));
    }

    void Fill(const Load& Source)
    {
        // Rock first, then soil and a gravel wearing course.
        Add(Material::Rock, Source.Volumes[2]);
        Add(Material::Soil, Source.Volumes[0]);
        Add(Material::Clay, Source.Volumes[4]);
        Add(Material::Sand, Source.Volumes[3]);
        Add(Material::Gravel, Source.Volumes[1]);
    }

    double Mass() const
    {
        double Result = 0;
        for (const auto& Entry : Layers) Result += Entry.Volume * TonnesPerM3[static_cast<size_t>(Entry.Type)];
        return Result;
    }

    double Compact(double Pressure, double Moisture)
    {
        const double Before = Height();
        double Depth = 0;
        size_t I = Layers.size();
        while (I > 0 && Depth < 1.5)
        {
            --I;
            const double ActiveVolume = std::min(Layers[I].Volume, 1.5 - Depth);
            size_t ActiveIndex = I;
            if (Layers[I].Volume > ActiveVolume + Epsilon)
            {
                Layer Active = Layers[I];
                Active.Volume = ActiveVolume;
                Layers[I].Volume -= ActiveVolume;
                Layers.insert(Layers.begin() + I + 1, Active);
                ActiveIndex = I + 1;
            }
            auto& Active = Layers[ActiveIndex];
            if(Active.Type==Material::Rock&&!Active.Loose){Depth+=ActiveVolume;continue;}
            const double Attenuation = ActiveVolume > Epsilon ?
                (std::exp(-Depth * 2.0) - std::exp(-(Depth + ActiveVolume) * 2.0)) / (2.0 * ActiveVolume) : 0;
            Active.Packing += (1.0 - Active.Packing) * (1.0 - std::exp(-std::max(0.0, Pressure) *
                (1.0 - 0.65 * std::clamp(Moisture, 0.0, 1.0)) * Attenuation));
            Depth += ActiveVolume;
        }
        return Before - Height();
    }
};

// Overdamped finite-volume surface transport. Stable cells leave the queue until disturbed.
class ActiveSoil
{
public:
    explicit ActiveSoil(int Size = 0) : Size(Size) {}
    void Reset(int NewSize) { Size = NewSize; Queue.clear(); Pending.clear(); LastVisit.clear(); Clock=0; }
    size_t ActiveCells() const { return Queue.size(); }
    void Wake(int Cell)
    {
        if (Cell >= 0 && Cell < Size * Size && Pending.insert(Cell).second) {
            Queue.push_back(Cell);LastVisit.try_emplace(Cell,Clock);
        }
    }
    void WakeArea(int Cell)
    {
        const int X = Cell % Size, Y = Cell / Size;
        for (int DY = -1; DY <= 1; ++DY)
            for (int DX = -1; DX <= 1; ++DX)
                if (X + DX >= 0 && X + DX < Size && Y + DY >= 0 && Y + DY < Size)
                    Wake(Cell + DY * Size + DX);
    }

    template<class CellAt, class MoistureAt, class Changed>
    double Step(double Dt, size_t Budget, CellAt&& At, MoistureAt&& Moisture, Changed&& Update)
    {
        if(Dt<=0)return 0;
        Clock+=Dt;
        const size_t Visits = std::min(Budget, Queue.size());
        double MovedMass = 0;
        for (size_t Visit = 0; Visit < Visits; ++Visit)
        {
            const int From = Queue.front();
            Queue.pop_front(); Pending.erase(From);
            const int X = From % Size, Y = From / Size;
            const double Elapsed=std::clamp(Clock-LastVisit[From],Dt,std::max(Dt,.15));LastVisit[From]=Clock;
            if(At(From).Layers.empty() || (At(From).Surface()==Material::Rock&&!At(From).Layers.back().Loose)) {
                LastVisit.erase(From);continue;
            }
            const auto Type=At(From).Surface();const auto P=Properties(Type);
            const double Wet=std::clamp(Moisture(From),0.,1.),Packing=At(From).Layers.back().Packing;
            const double Angle=(P.DryRepose+(P.WetRepose-P.DryRepose)*Wet+Packing*4)*.017453292519943295;
            const double Cohesion=P.Cohesion*(1+.6*Packing)*(1-.8*Wet),Grade=std::tan(Angle);
            struct Exit { int To; double Limit,Excess; };
            std::array<Exit,8> Exits{};int Count=0;double Weight=0,Maximum=0;
            for(int DY=-1;DY<=1;++DY)for(int DX=-1;DX<=1;++DX) {
                if((!DX&&!DY)||X+DX<0||X+DX>=Size||Y+DY<0||Y+DY>=Size)continue;
                const int To=From+DY*Size+DX;
                const double Limit=Grade*(DX&&DY?1.4142135623730951:1.)+Cohesion;
                const double Excess=At(From).Height()-At(To).Height()-Limit;
                if(Excess>.004){Exits[Count++]={To,Limit,Excess};Weight+=Excess;Maximum=std::max(Maximum,Excess);}
            }
            const double Mobility=Type==Material::Sand?1.25:Type==Material::Clay?.25+1.15*Wet:Type==Material::Rock?.45:.9;
            const double Available=std::min(At(From).Layers.back().Volume,Elapsed*8*Mobility*std::sqrt(Maximum));
            for(int I=0;I<Count;++I) {
                const auto E=Exits[(I+Visit)%Count];
                if(At(From).Layers.empty()||At(From).Surface()!=Type)break;
                const double Excess=At(From).Height()-At(E.To).Height()-E.Limit;
                const double Bulk=At(From).Layers.back().BulkFactor();
                const double Transfer=std::min({Available*E.Excess/Weight,std::max(0.,Excess)*.48/(Bulk+1),At(From).Layers.back().Volume});
                if(Transfer<Epsilon)continue;
                const Load Flow=At(From).Cut(Transfer);At(E.To).Fill(Flow);MovedMass+=Flow.Mass();
                Update(From);Update(E.To);WakeArea(From);WakeArea(E.To);
            }
            if(!Pending.count(From))LastVisit.erase(From);
        }
        return MovedMass;
    }
private:
    int Size;
    std::deque<int> Queue;
    std::unordered_set<int> Pending;
    std::unordered_map<int,double> LastVisit;
    double Clock=0;
};

inline double SurfaceGrip(Material Type, double Wetness, double Submersion)
{
    const auto& P = Properties(Type);
    const double Grip = P.DryFriction + (P.WetFriction - P.DryFriction) * std::clamp(Wetness, 0.0, 1.0) - Submersion * 0.44;
    return std::clamp(Grip, 0.12, 1.0);
}

inline double FordingLimit(double Clearance, double LoadFraction)
{
    return Clearance - 0.43 * std::clamp(LoadFraction, 0.0, 1.0);
}
}
