#pragma once

// Builds a Network from an OpenStreetMap Overpass JSON extract of public-transport route relations
// (`out body;` for the relations followed by `node(r); out body;` for their member nodes).
//
// OSM models a metro as one route relation per direction and service pattern, whose "stop" members
// are stop positions: typically one node per line per direction at every station. The importer
//   1. normalises stop names (drops "(Pink Line)"-style suffixes, applies configured aliases),
//   2. merges stop positions with the same name that lie within `merge_radius_m` into one station,
//   3. pairs the two directions of each service pattern into one line,
//   4. turns consecutive stops into directed segments, deduplicated across relations,
//   5. estimates segment times (OSM carries no timetables) and adds configured walkways.

#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "metrograph/network.hpp"

namespace metrograph::osm {

struct WalkwaySpec {
    std::string a;
    std::string b;
    std::optional<std::uint32_t> seconds;  // estimated from distance when absent
};

struct ImportConfig {
    // Segment time = dwell_seconds + straight-line distance / speed. Assumed, not measured.
    double speed_kmh = 40.0;
    std::uint32_t dwell_seconds = 25;
    std::map<std::string, double> line_speed_kmh;  // keyed by line name, e.g. "Airport Express Line"

    // Stop positions with the same (normalised) name closer than this are one station.
    double merge_radius_m = 300.0;

    // Walkway time = walk_detour * straight-line distance / walk_speed_mps, unless given explicitly.
    double walk_speed_mps = 1.2;
    double walk_detour = 1.3;

    std::map<std::string, std::string> aliases;  // normalised OSM name -> station name
    std::vector<WalkwaySpec> walkways;
};

// Tab-separated settings, one per line; '#' starts a comment:
//   speed_kmh <v> | dwell_seconds <v> | merge_radius_m <v> | walk_speed_mps <v> | walk_detour <v>
//   line_speed_kmh <line name> <v>
//   alias <osm name> <station name>
//   walkway <station> <station> [seconds]
// Throws ParseError.
ImportConfig load_config(std::istream& in);

struct ImportResult {
    Network network;
    std::vector<std::string> notes;     // what was merged and derived, for the import log
    std::vector<std::string> warnings;  // data that looks wrong and deserves a human look
};

// Throws std::runtime_error when the extract cannot be used (malformed JSON, unnamed stops,
// a configured walkway naming an unknown station).
ImportResult import_overpass(std::istream& json, const ImportConfig& config);

// Exposed for testing: "Mayur Vihar - I (Pink Line)" -> "Mayur Vihar - I".
std::string normalise_stop_name(const std::string& name);

}  // namespace metrograph::osm
