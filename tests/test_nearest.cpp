#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "metrograph/nearest.hpp"

using namespace metrograph;
using Catch::Approx;

namespace {

// Stations due north of the origin at roughly 1.1 km, 111 m and 0 m (added out of distance order).
Network line_of_stations() {
    Network n;
    n.add_station("Far", 0.01, 0.0);
    n.add_station("Near", 0.001, 0.0);
    n.add_station("Here", 0.0, 0.0);
    return n;
}

}  // namespace

TEST_CASE("results are filtered by radius and sorted nearest first", "[nearest]") {
    const Network n = line_of_stations();
    const auto r = stations_within(n, 0.0, 0.0, 200.0);
    REQUIRE(r.size() == 2);
    CHECK(r[0].id == 2);
    CHECK(r[0].distance_m == Approx(0.0).margin(1e-9));
    CHECK(r[1].id == 1);
    CHECK(r[1].distance_m == Approx(111.2).margin(0.5));
}

TEST_CASE("limit caps the result count", "[nearest]") {
    const Network n = line_of_stations();
    const auto r = stations_within(n, 0.0, 0.0, 5000.0, 2);
    REQUIRE(r.size() == 2);
    CHECK(r[0].id == 2);
    CHECK(r[1].id == 1);
}

TEST_CASE("zero radius finds only coincident stations; negative radius throws", "[nearest]") {
    const Network n = line_of_stations();
    CHECK(stations_within(n, 0.0, 0.0, 0.0).size() == 1);
    CHECK(stations_within(n, 5.0, 5.0, 100.0).empty());
    CHECK_THROWS_AS(stations_within(n, 0.0, 0.0, -1.0), std::invalid_argument);
}
