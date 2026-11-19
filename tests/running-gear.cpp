#include "RunningGear.h"

#include <cassert>

using namespace RunningGear;

static void Near(double Actual, double Expected, double Tolerance = 1e-9)
{
    assert(std::abs(Actual - Expected) < Tolerance);
}

int main()
{
    constexpr double Radius = 0.65;
    Near(RollDegrees(2 * Pi * Radius, Radius), -360);
    Near(RollDegrees(-2 * Pi * Radius, Radius), 360);
    Near(RollDegrees(0, Radius), 0);
    Near(Wrap(-30, 360), 330);

    assert(SideDistance(3, 0.5, -1.2) > SideDistance(3, 0.5, 1.2));
    Near(SideDistance(0, 0.5, -1.2), 0.6);
    Near(SideDistance(0, 0.5, 1.2), -0.6);
    Near(SteeringDegrees(0, 4.16, 1.22), 0);
    assert(SteeringDegrees(30, 4.16, 1.22) > 30);
    assert(SteeringDegrees(30, 4.16, -1.22) < 30);
    Near(SteeringDegrees(-30, 4.16, -1.22), -SteeringDegrees(30, 4.16, 1.22));

    constexpr double FrontToJoint = 1.21, JointToBogie = 2.145;
    const auto StraightHauler = MoveArticulated(0.4, FrontToJoint, JointToBogie, 0, 0);
    Near(StraightHauler.FrontYaw, 0);
    Near(StraightHauler.RearYaw, 0);
    Near(StraightHauler.RearTravel, 0.4);
    const auto SteeringInPlace = MoveArticulated(0, FrontToJoint, JointToBogie, 0, 0.25);
    assert(SteeringInPlace.FrontYaw > -SteeringInPlace.RearYaw);
    assert(SteeringInPlace.RearYaw < 0);
    Near(SteeringInPlace.FrontYaw - SteeringInPlace.RearYaw, 0.25);
    const auto DrivingTurn = MoveArticulated(0.4, FrontToJoint, JointToBogie, 0.35, 0.35);
    const auto BackingTurn = MoveArticulated(-0.4, FrontToJoint, JointToBogie, 0.35, 0.35);
    assert(DrivingTurn.FrontYaw > 0);
    Near(DrivingTurn.FrontYaw, DrivingTurn.RearYaw);
    Near(BackingTurn.FrontYaw, -DrivingTurn.FrontYaw);
    Near(BackingTurn.RearTravel, -DrivingTurn.RearTravel);

    constexpr double LoaderAxleToJoint = 1.65;
    const auto LoaderSteer = MoveArticulated(0, LoaderAxleToJoint, LoaderAxleToJoint, 0, 0.02);
    assert(LoaderSteer.FrontYaw > 0 && LoaderSteer.RearYaw < 0);
    Near(LoaderSteer.FrontYaw, -LoaderSteer.RearYaw, 1e-6);
    const auto LoaderTurn = MoveArticulated(0.4, LoaderAxleToJoint, LoaderAxleToJoint, 0.4, 0.4);
    Near(LoaderTurn.FrontYaw, LoaderTurn.RearYaw);
    Near(LoaderTurn.RearTravel, 0.4);

    constexpr TrackPath Belt{1.70, 0.47};
    constexpr double Straight = 2 * Belt.HalfStraight;
    constexpr double Arc = Pi * Belt.Radius;
    const auto Front = Belt.Sample(Straight + Arc / 2);
    Near(Front.Ahead, Belt.HalfStraight + Belt.Radius);
    Near(Front.Height, 0);
    Near(Front.Pitch, -90);
    assert(Belt.Sample(0.12).Ahead > Belt.Sample(0).Ahead);
    assert(Belt.Sample(Straight + Arc + 0.12).Ahead < Belt.Sample(Straight + Arc).Ahead);
    Near(Belt.Sample(-0.12).Ahead, Belt.Sample(Belt.Length() - 0.12).Ahead);

    const double Joins[] = {0, Straight, Straight + Arc, 2 * Straight + Arc};
    for (const double Join : Joins)
    {
        constexpr double Step = 1e-6;
        const auto Before = Belt.Sample(Join - Step);
        const auto After = Belt.Sample(Join + Step);
        Near(std::hypot(After.Ahead - Before.Ahead, After.Height - Before.Height), 2 * Step);
        Near(std::remainder(After.Pitch - Before.Pitch, 360), 0, 0.001);
    }

    const auto StraightPress = PressGround(0, 1, 1, 0.25, 0, true);
    Near(ContactSink(1, 0.1, 1, false), 0);
    Near(ContactSink(0, 1.0, 1, false), 0);
    Near(ContactSink(1, 0.1, 1, false, 1), 0);
    assert(ContactSink(1, 1.0, 1, false) > ContactSink(1, 1.0, 0, false) * 2);
    assert(ContactSink(1, 0.95, 1, true) < ContactSink(1, 0.95, 1, false));
    const double FreshMud = ContactSink(1, 0.95, 0, false);
    const double ChurnedMud = ContactSink(1, 0.95, 0, false, 0.85);
    assert(ChurnedMud > FreshMud * 2);
    Near(MudMobility(0, 0.85, 1, false, false), 1);
    assert(MudMobility(FreshMud, 0.25, 0, false, false) > 0.40);
    Near(MudMobility(ChurnedMud, 0.12, 0, false, false), 0);
    assert(MudMobility(0.31, 0.18, 0, false, true) > MudMobility(0.31, 0.18, 0, false, false));
    assert(MudMobility(ContactSink(1, 0.95, 1, true, 0.85), 0.48, 1, true, false) > 0.30);
    assert(PressGround(0, 1, 1, 0.95, 0.6, false, 1.5).Depth > PressGround(0, 1, 1, 0.95, 0, false).Depth * 2);
    const auto TurningPress = PressGround(0, 1, 1, 0.25, 1, true);
    const auto MudPress = PressGround(0, 1, 1, 0.95, 1, true);
    assert(TurningPress.Depth > StraightPress.Depth * 2);
    assert(MudPress.Depth > TurningPress.Depth);
    assert(PressGround(0, 1, 1, 0.25, 0, false).Depth < StraightPress.Depth);
    Near(PressGround(0, 1, 0, 0.95, 1, true).Depth, 0);
    Near(PressGround(0, 0, 1, 0.95, 1, true).Depth, 0);
    assert(PressGround(150, 1, 1, 0.95, 0, true).Wear == 150);
    assert(PressGround(150, 1, 1, 0.95, 1, true).Wear > 150);
    assert(PressGround(255, 1, 1, 0.95, 1, true).Wear == 255);
    Near(PressGround(255, 1, 1, 0.95, 1, true).Depth, 0);
}
