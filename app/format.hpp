#pragma once

// Human-readable rendering of routes, history and measurements for the console front end.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "metrograph/history.hpp"
#include "metrograph/network.hpp"
#include "metrograph/routing.hpp"

namespace metrograph::app {

std::string format_duration(std::uint64_t seconds);  // "12 min 30 s", "1 h 04 min"
std::string format_distance(double metres);          // "850 m", "2.4 km"
std::string format_timestamp(std::int64_t unix_seconds);

std::string station_name(const Network& network, StationId id);  // "?" for an unknown id
std::vector<LineId> lines_at(const Network& network, StationId id);
std::string line_names(const Network& network, const std::vector<LineId>& lines);

// Route summary grouped into legs, one per line ridden.
std::string format_route(const Network& network, const Route& route, QueryMode mode);

// Two history entries side by side. Indices are the 1-based numbers shown to the user.
std::string format_comparison(const Network& network, const HistoryEntry& a, std::size_t a_index,
                              const HistoryEntry& b, std::size_t b_index);

}  // namespace metrograph::app
