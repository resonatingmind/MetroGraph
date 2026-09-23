#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "metrograph/history.hpp"

using namespace metrograph;

TEST_CASE("query modes round-trip through text", "[history]") {
    CHECK(parse_query_mode(to_string(QueryMode::Fastest)) == QueryMode::Fastest);
    CHECK(parse_query_mode(to_string(QueryMode::FewestStops)) == QueryMode::FewestStops);
    CHECK_FALSE(parse_query_mode("quickest"));
}

TEST_CASE("history entries round-trip", "[history]") {
    HistoryEntry a;
    a.timestamp = 1790000000;
    a.mode = QueryMode::Fastest;
    a.from = 3;
    a.to = 9;
    a.total_seconds = 1290;
    a.path = {3, 4, 7, 9};

    HistoryEntry b;  // same-station query: single-node path
    b.timestamp = 1790000100;
    b.mode = QueryMode::FewestStops;
    b.from = 5;
    b.to = 5;
    b.path = {5};

    std::stringstream buf;
    write_history_entry(buf, a);
    write_history_entry(buf, b);
    const auto entries = read_history(buf);

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].timestamp == a.timestamp);
    CHECK(entries[0].mode == a.mode);
    CHECK(entries[0].total_seconds == 1290);
    CHECK(entries[0].path == a.path);
    CHECK(entries[0].stops() == 3);
    CHECK(entries[1].mode == QueryMode::FewestStops);
    CHECK(entries[1].path == b.path);
    CHECK(entries[1].stops() == 0);
}

TEST_CASE("history accepts a UTF-8 byte-order mark", "[history]") {
    std::istringstream in("\xEF\xBB\xBFH\t1\tfastest\t0\t1\t60\t0,1\n");
    CHECK(read_history(in).size() == 1);
}

TEST_CASE("malformed history is rejected with a line number", "[history]") {
    std::istringstream in("H\t1\tfastest\t0\t1\t60\t0,1\nH\t2\tfastest\t0\n");
    try {
        read_history(in);
        FAIL("expected a ParseError");
    } catch (const ParseError& e) {
        CHECK(e.line() == 2);
    }
}
