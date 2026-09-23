#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "helpers.hpp"
#include "metrograph/network.hpp"
#include "metrograph/routing.hpp"

using namespace metrograph;
using test::route_problem;

namespace {

// Same shape as tests/data/mini.graph: North-Central-South on Red, West-Central-East-Harbour on Blue,
// plus a slow direct Green segment North-South.
struct Mini {
    Network network;
    StationId north, central, south, west, east, harbour, island;
    Mini() {
        const LineId red = network.add_line("Red Line", "#FF4040");
        const LineId blue = network.add_line("Blue Line", "#4169E1");
        const LineId green = network.add_line("Green Line", "#008000");
        north = network.add_station("North", 10.02, 10.0);
        central = network.add_station("Central", 10.0, 10.0);
        south = network.add_station("South", 9.98, 10.0);
        west = network.add_station("West", 10.0, 9.98);
        east = network.add_station("East", 10.0, 10.02);
        harbour = network.add_station("Harbour", 10.0, 10.04);
        island = network.add_station("Island", 11.0, 11.0);  // no segments: unreachable
        const auto both = [&](StationId a, StationId b, LineId line, std::uint32_t s) {
            network.add_segment(a, b, line, s);
            network.add_segment(b, a, line, s);
        };
        both(north, central, red, 120);
        both(central, south, red, 120);
        both(west, central, blue, 90);
        both(central, east, blue, 90);
        both(east, harbour, blue, 100);
        both(north, south, green, 600);
    }
};

// All-pairs shortest distances by Floyd-Warshall: an independent reference for Dijkstra.
std::vector<std::vector<std::uint64_t>> floyd_warshall(const Graph& g) {
    constexpr auto inf = std::numeric_limits<std::uint64_t>::max() / 4;
    const std::size_t n = g.node_count();
    std::vector<std::vector<std::uint64_t>> d(n, std::vector<std::uint64_t>(n, inf));
    for (std::size_t u = 0; u < n; ++u) {
        d[u][u] = 0;
        for (const Edge& e : g.neighbours(static_cast<NodeId>(u))) {
            d[u][e.to] = std::min<std::uint64_t>(d[u][e.to], e.seconds);
        }
    }
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                d[i][j] = std::min(d[i][j], d[i][k] + d[k][j]);
            }
        }
    }
    for (auto& row : d) {
        for (auto& x : row) {
            if (x >= inf) {
                x = std::numeric_limits<std::uint64_t>::max();
            }
        }
    }
    return d;
}

}  // namespace

TEST_CASE("fastest and fewest-stops disagree when the direct link is slow", "[routing]") {
    const Mini m;
    const Graph g = Graph::from_network(m.network);

    const auto fast = fastest_route(g, m.north, m.south);
    REQUIRE(fast);
    CHECK(fast->total_seconds == 240);
    CHECK(fast->nodes == std::vector<NodeId>{m.north, m.central, m.south});
    CHECK(route_problem(g, *fast, m.north, m.south).empty());

    const auto few = fewest_stops_route(g, m.north, m.south);
    REQUIRE(few);
    CHECK(few->stops() == 1);
    CHECK(few->total_seconds == 600);
    CHECK(route_problem(g, *few, m.north, m.south).empty());
}

TEST_CASE("route across two lines", "[routing]") {
    const Mini m;
    const Graph g = Graph::from_network(m.network);
    const auto r = fastest_route(g, m.north, m.harbour);
    REQUIRE(r);
    CHECK(r->total_seconds == 120 + 90 + 100);
    CHECK(r->nodes == std::vector<NodeId>{m.north, m.central, m.east, m.harbour});
    CHECK(r->steps.front().line != r->steps.back().line);
}

TEST_CASE("route to self is empty but valid", "[routing]") {
    const Mini m;
    const Graph g = Graph::from_network(m.network);
    for (const auto& route : {fastest_route(g, m.central, m.central), fewest_stops_route(g, m.central, m.central)}) {
        REQUIRE(route);
        CHECK(route->nodes == std::vector<NodeId>{m.central});
        CHECK(route->steps.empty());
        CHECK(route->total_seconds == 0);
    }
}

TEST_CASE("unreachable destination yields no route", "[routing]") {
    const Mini m;
    const Graph g = Graph::from_network(m.network);
    CHECK_FALSE(fastest_route(g, m.north, m.island));
    CHECK_FALSE(fewest_stops_route(g, m.north, m.island));
}

TEST_CASE("unknown node ids throw", "[routing]") {
    const Graph g(3);
    CHECK_THROWS_AS(fastest_route(g, 0, 3), std::out_of_range);
    CHECK_THROWS_AS(fewest_stops_route(g, 7, 0), std::out_of_range);
}

TEST_CASE("parallel edges: Dijkstra takes the cheaper one", "[routing]") {
    Graph g(2);
    g.add_edge(0, 1, 500, 0);
    g.add_edge(0, 1, 200, 1);
    const auto r = fastest_route(g, 0, 1);
    REQUIRE(r);
    CHECK(r->total_seconds == 200);
    CHECK(r->steps.front().line == 1);
}

TEST_CASE("property: on unit-weight graphs Dijkstra equals BFS", "[routing][property]") {
    std::mt19937 rng(20260924);
    for (int trial = 0; trial < 300; ++trial) {
        const std::size_t n = 2 + rng() % 60;
        const Graph g = test::random_graph(rng, n, rng() % (3 * n + 1), 1);
        for (int q = 0; q < 20; ++q) {
            const auto s = static_cast<NodeId>(rng() % n);
            const auto t = static_cast<NodeId>(rng() % n);
            const auto d = fastest_route(g, s, t);
            const auto b = fewest_stops_route(g, s, t);
            REQUIRE(d.has_value() == b.has_value());
            if (d) {
                CHECK(d->total_seconds == b->stops());
                CHECK(b->total_seconds == b->stops());
                CHECK(route_problem(g, *d, s, t).empty());
                CHECK(route_problem(g, *b, s, t).empty());
            }
        }
    }
}

TEST_CASE("property: weighted routes are valid walks and Dijkstra is optimal", "[routing][property]") {
    std::mt19937 rng(42);
    for (int trial = 0; trial < 150; ++trial) {
        const std::size_t n = 2 + rng() % 30;
        const Graph g = test::random_graph(rng, n, rng() % (4 * n + 1), 900);
        const auto reference = floyd_warshall(g);
        for (NodeId s = 0; s < n; ++s) {
            for (NodeId t = 0; t < n; ++t) {
                const auto d = fastest_route(g, s, t);
                const auto b = fewest_stops_route(g, s, t);
                REQUIRE(d.has_value() == (reference[s][t] != std::numeric_limits<std::uint64_t>::max()));
                REQUIRE(d.has_value() == b.has_value());
                if (d) {
                    CHECK(d->total_seconds == reference[s][t]);
                    CHECK(d->total_seconds <= b->total_seconds);
                    CHECK(b->stops() <= d->stops());
                    CHECK(route_problem(g, *d, s, t).empty());
                    CHECK(route_problem(g, *b, s, t).empty());
                }
            }
        }
    }
}
