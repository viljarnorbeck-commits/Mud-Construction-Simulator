#include "Earthwork.h"
#include "RunningGear.h"
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace Earthwork;

struct Grid
{
    static constexpr int Size = 64;
    std::vector<Column> Cells = std::vector<Column>(Size * Size, Column{0, {{Material::Soil, 2}}});
    ActiveSoil Flow{Size};
    double Wet = 0;
    double Mass() const
    {
        double Result = 0;
        for (const auto& Cell : Cells) Result += Cell.Mass();
        return Result;
    }
    double Step()
    {
        return Flow.Step(1.0 / 60, 768, [this](int I) -> Column& { return Cells[I]; },
            [this](int) { return Wet; }, [](int) {});
    }
    int Settle()
    {
        int Steps = 0;
        while (Flow.ActiveCells() && Steps < 20000) { Step(); ++Steps; }
        assert(Flow.ActiveCells() == 0);
        return Steps;
    }
};

void Near(double A, double B) { assert(std::abs(A - B) <= 1e-8 * std::max(1.0, std::abs(B))); }

void Benchmark(const char* Name, int Areas, double Amount, bool Repeated)
{
    Grid G;
    const double Before = G.Mass();
    double AddedMass = 0;
    for (int I = 0; I < Areas; ++I)
    {
        const int Cell = (12 + (I / 3) * 20) * Grid::Size + 12 + (I % 3) * 20;
        Load Added; Added.Add(Material::Sand, Amount);
        G.Cells[Cell].Fill(Added); G.Flow.WakeArea(Cell);
        AddedMass += Added.Mass();
    }
    std::vector<double> Times;
    size_t PeakActive = 0;
    for (int Frame = 0; Frame < 600; ++Frame)
    {
        if (Repeated)
        {
            const int From = 32 * Grid::Size + 32, To = From + 1;
            const auto Cut = G.Cells[Frame % 2 ? From : To].Cut(0.08);
            G.Cells[Frame % 2 ? To : From].Fill(Cut);
            G.Flow.WakeArea(From); G.Flow.WakeArea(To);
        }
        PeakActive = std::max(PeakActive, G.Flow.ActiveCells());
        const auto Start = std::chrono::steady_clock::now();
        G.Step();
        Times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count());
    }
    Near(G.Mass(), Before + AddedMass);
    double Sum = 0;
    for (double T : Times) Sum += T;
    std::sort(Times.begin(), Times.end());
    std::cout << "H " << Name << ": mean " << Sum / Times.size() << " ms, p99 " << Times[593]
        << " ms, peak active " << PeakActive << '\n';
}

int main()
{
    const int Center = 32 * Grid::Size + 32;
    Grid G;
    const double OriginalMass = G.Mass();
    Load Added;
    Added.Add(Material::Sand, 24);
    G.Cells[Center].Fill(Added);
    G.Flow.WakeArea(Center);
    const auto Begin = std::chrono::steady_clock::now();
    const int SettlingSteps = G.Settle();
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Begin).count();
    Near(G.Mass(), OriginalMass + Added.Mass());
    assert(G.Cells[Center].Height() < 8 && G.Cells[Center].Height() > 2.1);
    assert(G.Cells[Center + 1].Height() > 2);
    for (int Y = 1; Y < Grid::Size - 1; ++Y)
        for (int X = 1; X < Grid::Size - 1; ++X)
        {
            const int I = Y * Grid::Size + X;
            if (G.Cells[I].Surface() != Material::Sand) continue;
            const double Limit = std::tan(Properties(Material::Sand).DryRepose * RunningGear::Pi / 180) + 0.005;
            for (int J : {I - 1, I + 1, I - Grid::Size, I + Grid::Size})
                assert(G.Cells[I].Height() - G.Cells[J].Height() <= Limit);
        }
    const auto Still = G.Cells;
    for (int I = 0; I < 36000; ++I) Near(G.Step(), 0);
    for (size_t I = 0; I < Still.size(); ++I) Near(G.Cells[I].Height(), Still[I].Height());
    std::cout << "B/F sand heap and 10-minute sleeping terrain passed\n";

    // Removing the toe must wake the same solver and produce a new stable surface.
    const int Toe = Center + 1;
    const double OldToe = G.Cells[Toe].Height();
    const Load ToeCut = G.Cells[Toe].Cut(1.2);
    const double ExcavatedToe = G.Cells[Toe].Height();
    G.Flow.WakeArea(Toe); G.Settle();
    assert(G.Cells[Toe].Height() > ExcavatedToe && ExcavatedToe < OldToe);
    Near(G.Mass() + ToeCut.Mass(), OriginalMass + Added.Mass());
    G.Cells[Center + 14].Fill(ToeCut); G.Flow.WakeArea(Center + 14); G.Settle();
    std::cout << "B toe excavation triggers a mass-conserving collapse\n";

    Load Bucket = G.Cells[Center].Cut(0.8);
    G.Flow.WakeArea(Center);
    Load Truck;
    Truck.Receive(Bucket.Take(0.5));
    Near(G.Mass() + Bucket.Mass() + Truck.Mass(), OriginalMass + Added.Mass());
    const int Destination = Center + 12;
    G.Cells[Destination].Fill(Truck.Take(100));
    G.Cells[Destination].Fill(Bucket.Take(100));
    G.Flow.WakeArea(Destination);
    G.Settle();
    Near(G.Mass(), OriginalMass + Added.Mass());
    std::cout << "A/D dig, transport, tip and recapture conserve mass\n";

    Column Loose{0, {{Material::Sand, 1.5}, {Material::Clay, 1}}};
    const double Before = Loose.Mass(), Height = Loose.Height();
    for (int I = 0; I < 10; ++I) Loose.Compact(0.3, 0.4);
    assert(Loose.Height() < Height);
    Near(Loose.Mass(), Before);
    Column LoadedAgain = Loose;
    Near(LoadedAgain.Height(), Loose.Height());
    Near(LoadedAgain.Mass(), Before);
    const Load Recovered = Loose.Cut(100);
    Near(Recovered.Mass(), Before);
    Near(Recovered.Volumes[3], 1.5);
    Near(Recovered.Volumes[4], 1);
    Near(Loose.Height(), 0);
    std::cout << "E/G compaction, copied layers and excavation conserve mass\n";

    Column Deep{0, {{Material::Sand, 10}}};
    for (int I = 0; I < 10000; ++I) Deep.Compact(0.3, 0.4);
    assert(Deep.Layers.size() == 2 && Deep.Layers[0].Packing == 0);
    const double Floor = Deep.Height() - 0.4;
    const double PackedMass = Deep.Mass();
    const Load PlaneCut = Deep.CutToHeight(Floor, 100);
    Near(Deep.Height(), Floor);
    Near(Deep.Mass() + PlaneCut.Mass(), PackedMass);
    const Load CapacityCut = Deep.CutToHeight(-100, 0.1);
    Near(CapacityCut.Volume(), 0.1);
    std::cout << "E deep compaction stays local; packed cuts respect plane and bucket capacity\n";

    const auto Rut = [](Material Type, double Wet, double Pressure)
    {
        Column Contact{0, {{Type, 5}}};
        const double Mass = Contact.Mass(), Height = Contact.Height();
        Contact.Compact(0.8 * Pressure * 0.9 * 0.1, Wet);
        const double Softness = (1 - Properties(Type).Bearing) * (1 - Contact.Layers.back().Packing * 0.65);
        const auto Ground = RunningGear::PressGround(0, 0.8, Softness, Wet, 0.1, false, Pressure);
        const Load Displaced = Contact.CutToHeight(Contact.Height() - Ground.Depth, 100);
        Near(Contact.Mass() + Displaced.Mass(), Mass);
        return Height - Contact.Height();
    };
    assert(Rut(Material::Clay, 0.9, 2) > Rut(Material::Clay, 0.9, 0.5));
    assert(Rut(Material::Clay, 0.9, 1) > Rut(Material::Rock, 0.9, 1));
    assert(Rut(Material::Clay, 0.9, 1) > Rut(Material::Clay, 0.1, 1));
    for (size_t M = 0; M < MaterialCount; ++M)
    {
        Grid MaterialGrid;
        MaterialGrid.Cells[Center].Add(static_cast<Material>(M), 24);
        const double Mass = MaterialGrid.Mass();
        MaterialGrid.Flow.WakeArea(Center); MaterialGrid.Settle();
        Near(MaterialGrid.Mass(), Mass);
        assert(MaterialGrid.Cells[Center].Height() < 26);
    }
    std::cout << "B/E all five materials stabilize; rut depth follows load, bearing and moisture\n";

    for (double Angle : {-25.0, 0.0, 25.0})
    {
        Grid BladeGrid;
        const double Mass = BladeGrid.Mass();
        Load Blade;
        const int Side = Angle > 0 ? 1 : -1;
        for (int X = 12; X < 22; ++X)
        {
            Blade.Receive(BladeGrid.Cells[32 * Grid::Size + X].Cut(0.25));
            auto Spill = Blade.Take(Blade.Volume() * std::abs(Angle) / 100);
            const int To = (32 + Side * 2) * Grid::Size + X;
            BladeGrid.Cells[To].Fill(Spill);
            BladeGrid.Flow.WakeArea(To);
        }
        BladeGrid.Cells[32 * Grid::Size + 22].Fill(Blade.Take(100));
        BladeGrid.Flow.WakeArea(32 * Grid::Size + 22);
        BladeGrid.Settle();
        Near(BladeGrid.Mass(), Mass);
    }
    std::cout << "C angle-dependent transfers conserve mass (column API)\n";

    const double Light = RunningGear::ContactSink(0.8, 0.9, 0, false);
    const double Heavy = RunningGear::ContactSink(0.8, 0.9, 1, false);
    assert(Heavy > Light);
    assert(RunningGear::ContactSink(0.1, 0.9, 1, false) < Heavy);
    assert(RunningGear::ContactSink(0.8, 0.2, 1, false) < Light);
    RunningGear::SoilDrivetrain Drive;
    double Speed = 0;
    for (int I = 0; I < 600; ++I)
        Speed = Drive.Advance(Speed, 1, 8, 0.13, 0.45, 0.62, 0.6, 0, false, false, 1.0 / 60);
    assert(std::abs(Speed) < 0.1 && Drive.WheelSpeed > 1);
    std::cout << "E load, moisture, bearing and physical wheelspin passed\n";
    std::cout << std::fixed << std::setprecision(5) << "H heap steps " << SettlingSteps
        << ", total " << Seconds * 1000 << " ms, average active step " << Seconds * 1000 / SettlingSteps << " ms\n";
    Benchmark("idle", 0, 0, false);
    Benchmark("one active pile", 1, 24, false);
    Benchmark("six work areas", 6, 24, false);
    Benchmark("large transfer", 1, 240, false);
    Benchmark("repeated deformation", 1, 24, true);
}
