#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>

#include "metrograph/graph.hpp"
#include "metrograph/osm_import.hpp"

using namespace metrograph;
using namespace metrograph::osm;
using Catch::Matchers::ContainsSubstring;

namespace {

ImportResult import_file(const std::string& path, const ImportConfig& config) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return import_overpass(in, config);
}

ImportConfig mini_config() {
    std::istringstream in(
        "speed_kmh\t40\n"
        "dwell_seconds\t25\n"
        "line_speed_kmh\tAirport Line\t80\n"
        "alias\tOmega Stn\tOmega\n"
        "walkway\tGamma\tZeta Terminal\n");
    return load_config(in);
}

bool any_contains(const std::vector<std::string>& lines, const std::string& needle) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& l) { return l.find(needle) != std::string::npos; });
}

std::vector<std::string> lines_at(const Network& n, StationId s) {
    std::vector<std::string> names;
    for (const Segment& seg : n.segments()) {
        if (seg.from == s || seg.to == s) {
            names.push_back(n.line(seg.line)->name);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

const Segment* find_segment(const Network& n, const std::string& from, const std::string& to) {
    const auto a = n.find_exact(from);
    const auto b = n.find_exact(to);
    for (const Segment& s : n.segments()) {
        if (a && b && s.from == *a && s.to == *b) {
            return &s;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("stop names are normalised", "[osm]") {
    CHECK(normalise_stop_name("Anand Vihar (Pink Line)") == "Anand Vihar");
    CHECK(normalise_stop_name("Mayur Vihar - I (Blue Line)") == "Mayur Vihar - I");
    CHECK(normalise_stop_name("Delta Metro Station") == "Delta");
    CHECK(normalise_stop_name("  Rajiv Chowk ") == "Rajiv Chowk");
    CHECK(normalise_stop_name("Depot Station") == "Depot Station");  // a real name, not a suffix
    CHECK(normalise_stop_name("Sector 18 (East)") == "Sector 18 (East)");
}

TEST_CASE("settings file parses and rejects mistakes", "[osm]") {
    const ImportConfig c = mini_config();
    CHECK(c.speed_kmh == 40.0);
    CHECK(c.line_speed_kmh.at("Airport Line") == 80.0);
    CHECK(c.aliases.at("Omega Stn") == "Omega");
    REQUIRE(c.walkways.size() == 1);
    CHECK_FALSE(c.walkways[0].seconds);

    std::istringstream timed("walkway\tA\tB\t300\n");
    CHECK(load_config(timed).walkways.at(0).seconds == 300u);

    std::istringstream unknown("# fine\nspeed\t40\n");
    try {
        load_config(unknown);
        FAIL("expected a ParseError");
    } catch (const ParseError& e) {
        CHECK(e.line() == 2);
    }
    std::istringstream zero("speed_kmh\t0\n");
    CHECK_THROWS_AS(load_config(zero), ParseError);
}

TEST_CASE("fixture: stations are merged, split and aliased", "[osm]") {
    const ImportResult r = import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", mini_config());
    const Network& n = r.network;

    std::vector<std::string> names;
    for (const Station& s : n.stations()) {
        names.push_back(s.name);
    }
    CHECK(names == std::vector<std::string>{"Alpha (1)", "Alpha (2)", "Beta", "Delta", "Epsilon", "Gamma", "Omega",
                                            "Zeta Terminal"});
    // Beta's four stop positions (two lines, two directions) became one interchange station.
    CHECK(lines_at(n, *n.find_exact("Beta")) == std::vector<std::string>{"Blue Line", "Red Line"});
    CHECK(any_contains(r.warnings, "'Alpha' names 2 places"));
    CHECK(any_contains(r.notes, "alias: 'Omega Stn' -> 'Omega'"));
}

TEST_CASE("fixture: directions pair into lines, branches get termini names", "[osm]") {
    const ImportResult r = import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", mini_config());
    const Network& n = r.network;

    REQUIRE(n.lines().size() == 4);
    CHECK(n.lines()[0].name == "Airport Line");
    CHECK(n.lines()[0].colour == "#808080");  // CSS "gray"
    CHECK(n.lines()[1].name == "Blue Line");
    CHECK(n.lines()[2].name == "Blue Line (Delta–Epsilon)");
    CHECK(n.lines()[3].name == "Red Line");
    CHECK(n.lines()[3].colour == "#FF4040");  // upper-cased

    CHECK(n.segments().size() == 11);
    CHECK(find_segment(n, "Delta", "Epsilon"));
    CHECK_FALSE(find_segment(n, "Epsilon", "Delta"));
    CHECK(any_contains(r.warnings, "one-way segment Delta -> Epsilon"));
    CHECK(r.warnings.size() == 2);  // the one-way branch and the split "Alpha"; nothing else
}

TEST_CASE("fixture: segment times follow the configured model", "[osm]") {
    const ImportResult r = import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", mini_config());
    const Segment* ab = find_segment(r.network, "Alpha (1)", "Beta");
    REQUIRE(ab);
    CHECK(ab->seconds == 125);  // 25 s dwell + 1112 m at 40 km/h

    // Airport Line uses its own speed: Zeta Terminal -> Omega is ~1.47 km at 80 km/h.
    const Segment* zo = find_segment(r.network, "Zeta Terminal", "Omega");
    REQUIRE(zo);
    CHECK(zo->seconds >= 25 + 60);
    CHECK(zo->seconds <= 25 + 75);
}

TEST_CASE("fixture: walkways link otherwise separate lines", "[osm]") {
    const ImportResult linked = import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", mini_config());
    REQUIRE(linked.network.walkways().size() == 1);
    const Walkway& w = linked.network.walkways()[0];
    CHECK(w.seconds >= 70);  // ~70 m * 1.3 / 1.2 m/s
    CHECK(w.seconds <= 90);

    ImportConfig no_walkway = mini_config();
    no_walkway.walkways.clear();
    const ImportResult split = import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", no_walkway);
    CHECK(any_contains(split.warnings, "disconnected group of 3 station(s)"));
    CHECK(any_contains(split.warnings, "Gamma and Zeta Terminal are only"));
}

TEST_CASE("unusable input is rejected", "[osm]") {
    ImportConfig bad = mini_config();
    bad.walkways.push_back(WalkwaySpec{"Gamma", "Atlantis", std::nullopt});
    CHECK_THROWS_WITH(import_file(METROGRAPH_TEST_DATA_DIR "/osm_mini.json", bad), ContainsSubstring("Atlantis"));

    std::istringstream not_json("<html>504 Gateway Timeout</html>");
    CHECK_THROWS_WITH(import_overpass(not_json, ImportConfig{}), ContainsSubstring("not valid Overpass JSON"));

    std::istringstream unnamed(R"({"elements":[
        {"type":"relation","id":1,"tags":{"name":"X"},"members":[{"type":"node","ref":7,"role":"stop"}]},
        {"type":"node","id":7,"lat":1,"lon":1}]})");
    CHECK_THROWS_WITH(import_overpass(unnamed, ImportConfig{}), ContainsSubstring("has no name"));
}

// ---- the real Delhi extract ---------------------------------------------------------------------

namespace {

ImportResult import_delhi() {
    std::ifstream settings(METROGRAPH_SOURCE_DIR "/data/delhi_import.tsv", std::ios::binary);
    REQUIRE(settings);
    return import_file(METROGRAPH_SOURCE_DIR "/data/raw/delhi_overpass.json", load_config(settings));
}

}  // namespace

TEST_CASE("Delhi: imports cleanly into one connected network", "[osm][delhi]") {
    const ImportResult r = import_delhi();
    const Network& n = r.network;
    INFO("first warning: " << (r.warnings.empty() ? "" : r.warnings.front()));
    CHECK(r.warnings.empty());
    CHECK(n.stations().size() > 250);
    CHECK(n.lines().size() >= 10);

    // Every station reachable from every other: BFS over the directed graph from one station.
    const Graph g = Graph::from_network(n);
    std::vector<bool> seen(g.node_count(), false);
    std::queue<NodeId> q;
    q.push(0);
    seen[0] = true;
    while (!q.empty()) {
        const NodeId u = q.front();
        q.pop();
        for (const Edge& e : g.neighbours(u)) {
            if (!seen[e.to]) {
                seen[e.to] = true;
                q.push(e.to);
            }
        }
    }
    CHECK(std::count(seen.begin(), seen.end(), true) == static_cast<long>(g.node_count()));
}

TEST_CASE("Delhi: well-known interchanges are single stations", "[osm][delhi]") {
    const Network n = import_delhi().network;
    const auto lines_of = [&](const char* name) {
        const auto id = n.find_exact(name);
        REQUIRE(id);
        return lines_at(n, *id);
    };
    CHECK(lines_of("Kashmere Gate") == std::vector<std::string>{"Red Line", "Violet Line", "Yellow Line"});
    CHECK(lines_of("Rajiv Chowk") == std::vector<std::string>{"Blue Line", "Yellow Line"});
    CHECK(lines_of("Anand Vihar").size() == 2);  // OSM spells it "Anand Vihar (Pink Line)" on one line
    CHECK(lines_of("Hauz Khas") == std::vector<std::string>{"Magenta Line", "Yellow Line"});
    CHECK(n.find_exact("Noida Sector 51"));  // Aqua Line present
}

TEST_CASE("Delhi: committed data/delhi.graph is up to date", "[osm][delhi]") {
    std::ostringstream fresh;
    save_network(import_delhi().network, fresh);

    std::ifstream committed(METROGRAPH_SOURCE_DIR "/data/delhi.graph", std::ios::binary);
    REQUIRE(committed);
    std::string body;
    for (std::string line; std::getline(committed, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind('#', 0) != 0) {  // skip the generated header comment
            body += line + '\n';
        }
    }
    INFO("regenerate with: cmake --build --preset debug --target import-delhi");
    CHECK(body == fresh.str());
}
