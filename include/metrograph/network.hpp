#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace metrograph {

// Ids are dense indices: the n-th station added has id n.
using StationId = std::uint32_t;
using LineId = std::uint32_t;

struct Station {
    StationId id = 0;
    std::string name;
    double lat = 0.0;
    double lon = 0.0;
};

struct Line {
    LineId id = 0;
    std::string name;
    std::string colour;  // "#RRGGBB", may be empty
};

// A directed track segment between two adjacent stations on one line.
struct Segment {
    StationId from = 0;
    StationId to = 0;
    LineId line = 0;
    std::uint32_t seconds = 0;
};

// The transit network as data: stations, lines and the segments joining them.
// Routing graphs are built from this (see graph.hpp); the network itself knows nothing about algorithms.
class Network {
public:
    StationId add_station(std::string name, double lat, double lon);
    LineId add_line(std::string name, std::string colour);
    void add_segment(StationId from, StationId to, LineId line, std::uint32_t seconds);

    const std::vector<Station>& stations() const noexcept { return stations_; }
    const std::vector<Line>& lines() const noexcept { return lines_; }
    const std::vector<Segment>& segments() const noexcept { return segments_; }

    // nullptr when the id is out of range.
    const Station* station(StationId id) const noexcept;
    const Line* line(LineId id) const noexcept;

    // Case-insensitive whole-name match.
    std::optional<StationId> find_exact(std::string_view name) const;
    // Case-insensitive substring match, in id order.
    std::vector<StationId> search(std::string_view query) const;

private:
    std::vector<Station> stations_;
    std::vector<std::string> lowered_names_;
    std::vector<Line> lines_;
    std::vector<Segment> segments_;
};

class ParseError : public std::runtime_error {
public:
    ParseError(std::size_t line, const std::string& message);
    std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_;
};

// Tab-separated text format, one record per line:
//   metrograph <TAB> 1
//   L <TAB> id <TAB> colour <TAB> name
//   S <TAB> id <TAB> lat <TAB> lon <TAB> name
//   E <TAB> from <TAB> to <TAB> line <TAB> seconds
// Ids must be dense and in order. Blank lines and lines starting with '#' are ignored.
Network load_network(std::istream& in);
Network load_network_file(const std::string& path);
void save_network(const Network& network, std::ostream& out);

}  // namespace metrograph
