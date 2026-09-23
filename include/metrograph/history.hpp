#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>
#include <vector>

#include "metrograph/network.hpp"

namespace metrograph {

enum class QueryMode : std::uint8_t { Fastest, FewestStops };

std::string_view to_string(QueryMode mode) noexcept;
std::optional<QueryMode> parse_query_mode(std::string_view text) noexcept;

// One answered route query. Station ids refer to the network file the query ran against.
struct HistoryEntry {
    std::int64_t timestamp = 0;  // seconds since the Unix epoch
    QueryMode mode = QueryMode::Fastest;
    StationId from = 0;
    StationId to = 0;
    std::uint64_t total_seconds = 0;
    std::vector<StationId> path;

    std::size_t stops() const noexcept { return path.empty() ? 0 : path.size() - 1; }
};

// Serialised one entry per line so new queries can be appended without rewriting the file:
//   H <TAB> timestamp <TAB> mode <TAB> from <TAB> to <TAB> total_seconds <TAB> id,id,...
void write_history_entry(std::ostream& out, const HistoryEntry& entry);
// Throws ParseError on a malformed line.
std::vector<HistoryEntry> read_history(std::istream& in);

}  // namespace metrograph
