#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "metrograph/graph.hpp"
#include "metrograph/network.hpp"

namespace metrograph::app {

// Resolves user input to a station: a numeric id, an exact name, or a unique name fragment.
// When the input is ambiguous, `id` is empty and `candidates` lists the matches.
struct StationMatch {
    std::optional<StationId> id;
    std::vector<StationId> candidates;
};
StationMatch match_station(const Network& network, std::string_view text);

// Interactive menu. Past queries are loaded from and appended to `history_path`.
int run_console(const Network& network, const Graph& graph, const std::string& history_path, std::istream& in,
                std::ostream& out);

}  // namespace metrograph::app
