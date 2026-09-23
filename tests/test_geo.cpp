#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "metrograph/geo.hpp"

using metrograph::haversine_m;
using metrograph::kEarthRadiusM;
using Catch::Approx;

TEST_CASE("haversine basics", "[geo]") {
    constexpr double kPi = 3.14159265358979323846;
    CHECK(haversine_m(28.6, 77.2, 28.6, 77.2) == 0.0);
    // One degree along a meridian is R * pi / 180.
    CHECK(haversine_m(0, 0, 1, 0) == Approx(kEarthRadiusM * kPi / 180.0).margin(0.01));
    // One degree of longitude at 60 degrees latitude is roughly half that at the equator.
    CHECK(haversine_m(60, 0, 60, 1) == Approx(haversine_m(0, 0, 0, 1) / 2.0).epsilon(0.001));
    // Antipodal points are half a circumference apart.
    CHECK(haversine_m(0, 0, 0, 180) == Approx(kEarthRadiusM * kPi).epsilon(1e-9));
}

TEST_CASE("haversine is symmetric", "[geo]") {
    const double ab = haversine_m(28.6328, 77.2197, 28.5494, 77.2001);
    const double ba = haversine_m(28.5494, 77.2001, 28.6328, 77.2197);
    CHECK(ab == Approx(ba));
}
