#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>

#include "metrograph/graph.hpp"
#include "metrograph/network.hpp"

using namespace metrograph;
using Catch::Approx;

namespace {

Network sample() {
    Network n;
    const LineId yellow = n.add_line("Yellow Line", "#FFDF00");
    const StationId a = n.add_station("Rajiv Chowk", 28.6328, 77.2197);
    const StationId b = n.add_station("Patel Chowk", 28.6231, 77.2141);
    n.add_station("Chandni Chowk", 28.6580, 77.2301);
    n.add_segment(a, b, yellow, 110);
    return n;
}

void expect_parse_error_on_line(const std::string& text, std::size_t line) {
    std::istringstream in(text);
    try {
        load_network(in);
        FAIL("expected a ParseError");
    } catch (const ParseError& e) {
        CHECK(e.line() == line);
    }
}

}  // namespace

TEST_CASE("ids are dense and lookups work", "[network]") {
    const Network n = sample();
    REQUIRE(n.stations().size() == 3);
    CHECK(n.station(1)->name == "Patel Chowk");
    CHECK(n.station(3) == nullptr);
    CHECK(n.line(0)->colour == "#FFDF00");
    CHECK(n.line(1) == nullptr);
}

TEST_CASE("exact lookup is case-insensitive and ignores surrounding spaces", "[network]") {
    const Network n = sample();
    CHECK(n.find_exact("rajiv chowk") == StationId{0});
    CHECK(n.find_exact("  PATEL CHOWK ") == StationId{1});
    CHECK_FALSE(n.find_exact("Rajiv"));
}

TEST_CASE("search matches substrings in id order", "[network]") {
    const Network n = sample();
    CHECK(n.search("chowk") == std::vector<StationId>{0, 1, 2});
    CHECK(n.search("CHANDNI") == std::vector<StationId>{2});
    CHECK(n.search("airport").empty());
    CHECK(n.search("   ").empty());
}

TEST_CASE("invalid input is rejected", "[network]") {
    Network n = sample();
    CHECK_THROWS_AS(n.add_station("Bad\tName", 0, 0), std::invalid_argument);
    CHECK_THROWS_AS(n.add_station("", 0, 0), std::invalid_argument);
    CHECK_THROWS_AS(n.add_station("Nowhere", 91.0, 0), std::invalid_argument);
    CHECK_THROWS_AS(n.add_segment(0, 9, 0, 60), std::out_of_range);
    CHECK_THROWS_AS(n.add_segment(0, 1, 5, 60), std::out_of_range);
    CHECK_THROWS_AS(n.add_segment(1, 1, 0, 60), std::invalid_argument);
}

TEST_CASE("save then load round-trips", "[network][io]") {
    const Network original = sample();
    std::stringstream buf;
    save_network(original, buf);
    const Network loaded = load_network(buf);

    REQUIRE(loaded.stations().size() == original.stations().size());
    REQUIRE(loaded.lines().size() == original.lines().size());
    REQUIRE(loaded.segments().size() == original.segments().size());
    for (std::size_t i = 0; i < original.stations().size(); ++i) {
        CHECK(loaded.stations()[i].name == original.stations()[i].name);
        CHECK(loaded.stations()[i].lat == Approx(original.stations()[i].lat).margin(1e-7));
        CHECK(loaded.stations()[i].lon == Approx(original.stations()[i].lon).margin(1e-7));
    }
    CHECK(loaded.lines()[0].name == "Yellow Line");
    CHECK(loaded.segments()[0].seconds == 110);
}

TEST_CASE("walkways round-trip and become two-way graph edges", "[network][io]") {
    Network n = sample();
    n.add_walkway(0, 2, 240);
    CHECK_THROWS_AS(n.add_walkway(0, 0, 60), std::invalid_argument);
    CHECK_THROWS_AS(n.add_walkway(0, 9, 60), std::out_of_range);

    std::stringstream buf;
    save_network(n, buf);
    const Network loaded = load_network(buf);
    REQUIRE(loaded.walkways().size() == 1);
    CHECK(loaded.walkways()[0].b == 2);
    CHECK(loaded.walkways()[0].seconds == 240);

    const Graph g = Graph::from_network(loaded);
    CHECK(g.edge_count() == loaded.segments().size() + 2);
    const auto has_walk = [&](NodeId from, NodeId to) {
        const auto& edges = g.neighbours(from);
        return std::any_of(edges.begin(), edges.end(),
                           [&](const Edge& e) { return e.to == to && e.line == kWalkway && e.seconds == 240; });
    };
    CHECK(has_walk(0, 2));
    CHECK(has_walk(2, 0));
}

TEST_CASE("loader tolerates comments, blank lines and CRLF", "[network][io]") {
    std::istringstream in("# comment\r\n\r\nmetrograph\t1\r\nS\t0\t1.5\t2.5\tSolo\r\n");
    const Network n = load_network(in);
    REQUIRE(n.stations().size() == 1);
    CHECK(n.stations()[0].name == "Solo");
}

TEST_CASE("loader accepts a UTF-8 byte-order mark", "[network][io]") {
    std::istringstream in("\xEF\xBB\xBFmetrograph\t1\nS\t0\t1.5\t2.5\tSolo\n");
    CHECK(load_network(in).stations().size() == 1);
}

TEST_CASE("loader reports the offending line", "[network][io]") {
    expect_parse_error_on_line("", 0);
    expect_parse_error_on_line("not a header\n", 1);
    expect_parse_error_on_line("metrograph\t1\nS\t1\t0\t0\tSkipsId0\n", 2);
    expect_parse_error_on_line("metrograph\t1\nS\t0\t0\t0\tA\nX\t0\n", 3);
    expect_parse_error_on_line("metrograph\t1\nS\t0\tnorth\t0\tA\n", 2);
    expect_parse_error_on_line("metrograph\t1\nL\t0\t\tRed\nS\t0\t0\t0\tA\nE\t0\t4\t0\t60\n", 4);
    expect_parse_error_on_line("metrograph\t1\nS\t0\t0\t0\n", 2);
}

TEST_CASE("fixture file loads", "[network][io]") {
    const Network n = load_network_file(METROGRAPH_TEST_DATA_DIR "/mini.graph");
    CHECK(n.stations().size() == 6);
    CHECK(n.lines().size() == 3);
    CHECK(n.segments().size() == 12);
    CHECK_THROWS(load_network_file(METROGRAPH_TEST_DATA_DIR "/missing.graph"));
}
