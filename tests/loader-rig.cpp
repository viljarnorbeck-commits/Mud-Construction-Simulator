#include "LoaderRig.h"

#include <cassert>
#include <limits>

using namespace LoaderRig;

double Length(Point Vector) { return std::hypot(Vector.X, Vector.Z); }
double Dot(Point A, Point B) { return A.X * B.X + A.Z * B.Z; }

template <std::size_t N, std::size_t M>
bool SeparatedOnEdges(const std::array<Point, N>& A, const std::array<Point, M>& B, double Radius)
{
    for (std::size_t I = 0; I < N; ++I)
    {
        const auto Edge = A[(I + 1) % N] - A[I];
        if (Length(Edge) < 1e-8)
            continue;
        const auto Normal = Point{-Edge.Z, Edge.X} * (1 / Length(Edge));
        const auto Interval = [Normal](const auto& Profile)
        {
            double Low = std::numeric_limits<double>::infinity(), High = -Low;
            for (auto Vertex : Profile)
            {
                const double Coordinate = Dot(Normal, Vertex);
                Low = std::min(Low, Coordinate);
                High = std::max(High, Coordinate);
            }
            return std::array<double, 2>{Low, High};
        };
        const auto X = Interval(A), Y = Interval(B);
        if (X[1] + Radius < Y[0] || Y[1] + Radius < X[0])
            return true;
    }
    return false;
}

template <std::size_t N, std::size_t M>
bool HasClearance(const std::array<Point, N>& A, const std::array<Point, M>& B, double Radius = 0)
{
    return SeparatedOnEdges(A, B, Radius) || SeparatedOnEdges(B, A, Radius);
}

int main()
{
    const Point Dig = CuttingEdge(MinBoom, 0);
    const Point Dump = CuttingEdge(MaxBoom, -45);
    assert(std::abs(Dig.Z) < 12); // normal scraping depth, no buried shell
    assert(Dump.Z > 300 && Dump.Z < 340); // both production bed rims, including clearance
    assert(Dump.X > 285 && Dump.X < 340); // over the side with the tyres clear
    constexpr double ToRadians = 3.14159265358979323846 / 180;
    for (double Boom = MinBoom; Boom <= MaxBoom; Boom += 0.5)
    {
        for (double Curl = MinCurl; Curl <= MaxCurl; Curl += 0.5)
        {
            const double Pitch = BucketPitch(Boom, Curl) * ToRadians;
            const Point Ear = Coupler + Rotate(BucketEar, Pitch);
            const Point Joint = LinkPin(Ear);
            assert(std::abs(Length(Joint - RockerPin) - RockerLength) < 1e-7);
            assert(std::abs(Length(Ear - Joint) - LinkLength) < 1e-7);

            auto Bucket = BucketProfile;
            auto Back = BucketProfile;
            for (std::size_t I = 0; I < Bucket.size(); ++I)
            {
                Bucket[I] = Coupler + Rotate(Bucket[I], Pitch);
                Back[I] = Coupler + Rotate(OuterBucketPoint(I), Pitch);
            }
            const auto Clear = [&](const auto& Part, double Radius = 0)
            {
                return HasClearance(Bucket, Part, Radius) && HasClearance(Back, Part, Radius);
            };
            assert(Clear(ArmProfile));
            assert(Clear(std::array<Point, 1>{Coupler}, 14.5));
            const Point Rocker = Joint - RockerPin;
            const double RockerAngle = std::atan2(Rocker.Z, Rocker.X);
            auto RockerShape = RockerProfile;
            for (auto& Vertex : RockerShape)
                Vertex = RockerPin + Rotate(Vertex, RockerAngle);
            assert(Clear(RockerShape));
            assert(Clear(std::array<Point, 2>{Joint, Ear}, 8.5));
            const Point CylinderBase = TiltBase;
            const Point CylinderRod = RockerPin + Rotate(TiltRod, RockerAngle);
            const double TiltLength = Length(CylinderRod - CylinderBase);
            assert(TiltLength > 148 && TiltLength < 240);
            const Point LiftEnd = Mount + Rotate(LiftRod, Boom * ToRadians);
            const double LiftLength = Length(LiftEnd - LiftBase);
            assert(LiftLength > 170 && LiftLength < 300);
            assert(Clear(std::array<Point, 2>{CylinderBase, CylinderRod}, 9.5));
        }
    }
}
