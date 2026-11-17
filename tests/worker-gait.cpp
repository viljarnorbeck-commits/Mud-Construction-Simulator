#include "WorkerGait.h"

#include <cassert>

int main()
{
    WorkerGait::Cycle Stopped;
    for (int Frame = 0; Frame < 120; ++Frame)
        assert(!Stopped.Advance(0, 1.0 / 120, 1));
    assert(Stopped.Phase == 0.90);

    WorkerGait::Cycle Walking;
    assert(!Walking.Advance(0.08, 0.05, 1));
    assert(Walking.Advance(0.12, 0.07, 1));
    assert(Walking.Phase < 0.05 && Walking.RunWeight == 0);

    const auto Run = [](int Fps)
    {
        WorkerGait::Cycle Gait;
        Gait.RunWeight = 1;
        const double Dt = 1.0 / Fps;
        int Contacts = 0;
        for (int Frame = 0; Frame < 3 * Fps; ++Frame)
            Contacts += Gait.Advance(WorkerGait::SprintSpeed * Dt, Dt, 1);
        assert(Contacts == 12);
        return Gait.Phase;
    };
    assert(std::abs(Run(30) - Run(120)) < 1e-9);

    WorkerGait::Cycle Sprint;
    for (int Frame = 0; Frame < 30; ++Frame)
        Sprint.Advance(WorkerGait::SprintSpeed / 60, 1.0 / 60, 1);
    assert(Sprint.RunWeight > 0.99);
    const double StopPhase = Sprint.Phase;
    for (int Frame = 0; Frame < 30; ++Frame)
        assert(!Sprint.Advance(0, 1.0 / 60, 1));
    assert(Sprint.Phase == StopPhase && Sprint.RunWeight < 0.01);
}
