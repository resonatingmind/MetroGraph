#include "metrograph/network.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <istream>
#include <locale>
#include <ostream>
#include <sstream>

#include "text.hpp"

namespace metrograph {

namespace {

bool has_control_separator(std::string_view s) {
    return s.find_first_of("\t\r\n") != std::string_view::npos;
}

}  // namespace

StationId Network::add_station(std::string name, double lat, double lon) {
    if (name.empty() || has_control_separator(name)) {
        throw std::invalid_argument("station name must be non-empty and contain no tabs or newlines");
    }
    if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        throw std::invalid_argument("station '" + name + "' has invalid coordinates");
    }
    const auto id = static_cast<StationId>(stations_.size());
    lowered_names_.push_back(detail::to_lower(name));
    stations_.push_back(Station{id, std::move(name), lat, lon});
    return id;
}

LineId Network::add_line(std::string name, std::string colour) {
    if (name.empty() || has_control_separator(name) || has_control_separator(colour)) {
        throw std::invalid_argument("line name must be non-empty and contain no tabs or newlines");
    }
    const auto id = static_cast<LineId>(lines_.size());
    lines_.push_back(Line{id, std::move(name), std::move(colour)});
    return id;
}

void Network::add_segment(StationId from, StationId to, LineId line, std::uint32_t seconds) {
    if (from >= stations_.size() || to >= stations_.size()) {
        throw std::out_of_range("segment references an unknown station");
    }
    if (line >= lines_.size()) {
        throw std::out_of_range("segment references an unknown line");
    }
    if (from == to) {
        throw std::invalid_argument("segment must join two different stations");
    }
    segments_.push_back(Segment{from, to, line, seconds});
}

const Station* Network::station(StationId id) const noexcept {
    return id < stations_.size() ? &stations_[id] : nullptr;
}

const Line* Network::line(LineId id) const noexcept {
    return id < lines_.size() ? &lines_[id] : nullptr;
}

std::optional<StationId> Network::find_exact(std::string_view name) const {
    const std::string needle = detail::to_lower(detail::trim(name));
    for (std::size_t i = 0; i < lowered_names_.size(); ++i) {
        if (lowered_names_[i] == needle) {
            return static_cast<StationId>(i);
        }
    }
    return std::nullopt;
}

std::vector<StationId> Network::search(std::string_view query) const {
    const std::string needle = detail::to_lower(detail::trim(query));
    std::vector<StationId> matches;
    if (needle.empty()) {
        return matches;
    }
    for (std::size_t i = 0; i < lowered_names_.size(); ++i) {
        if (lowered_names_[i].find(needle) != std::string::npos) {
            matches.push_back(static_cast<StationId>(i));
        }
    }
    return matches;
}

ParseError::ParseError(std::size_t line, const std::string& message)
    : std::runtime_error("line " + std::to_string(line) + ": " + message), line_(line) {}

namespace {

template <typename T>
T parse_number(std::string_view field, std::size_t line_no, const char* what) {
    T value{};
    const char* first = field.data();
    const char* last = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last) {
        throw ParseError(line_no, std::string("invalid ") + what + " '" + std::string(field) + "'");
    }
    return value;
}

void expect_fields(const std::vector<std::string_view>& fields, std::size_t count, std::size_t line_no) {
    if (fields.size() != count) {
        throw ParseError(line_no, "expected " + std::to_string(count) + " tab-separated fields, found " +
                                      std::to_string(fields.size()));
    }
}

void expect_next_id(std::uint32_t id, std::size_t expected, std::size_t line_no, const char* kind) {
    if (id != expected) {
        throw ParseError(line_no, std::string(kind) + " id " + std::to_string(id) + " out of order (expected " +
                                      std::to_string(expected) + ")");
    }
}

}  // namespace

Network load_network(std::istream& in) {
    Network network;
    std::string raw;
    std::size_t line_no = 0;
    bool seen_header = false;

    while (std::getline(in, raw)) {
        if (++line_no == 1) {
            detail::strip_utf8_bom(raw);
        }
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        if (raw.empty() || raw.front() == '#') {
            continue;
        }
        const auto fields = detail::split(raw, '\t');

        if (!seen_header) {
            if (fields.size() != 2 || fields[0] != "metrograph" || fields[1] != "1") {
                throw ParseError(line_no, "expected header 'metrograph<TAB>1'");
            }
            seen_header = true;
            continue;
        }

        // Semantic errors from Network (bad coordinates, unknown ids) are reported with the line number too.
        try {
            const std::string_view kind = fields[0];
            if (kind == "L") {
                expect_fields(fields, 4, line_no);
                expect_next_id(parse_number<LineId>(fields[1], line_no, "line id"), network.lines().size(), line_no,
                               "line");
                network.add_line(std::string(fields[3]), std::string(fields[2]));
            } else if (kind == "S") {
                expect_fields(fields, 5, line_no);
                expect_next_id(parse_number<StationId>(fields[1], line_no, "station id"), network.stations().size(),
                               line_no, "station");
                network.add_station(std::string(fields[4]), parse_number<double>(fields[2], line_no, "latitude"),
                                    parse_number<double>(fields[3], line_no, "longitude"));
            } else if (kind == "E") {
                expect_fields(fields, 5, line_no);
                network.add_segment(parse_number<StationId>(fields[1], line_no, "station id"),
                                    parse_number<StationId>(fields[2], line_no, "station id"),
                                    parse_number<LineId>(fields[3], line_no, "line id"),
                                    parse_number<std::uint32_t>(fields[4], line_no, "duration"));
            } else {
                throw ParseError(line_no, "unknown record type '" + std::string(kind) + "'");
            }
        } catch (const ParseError&) {
            throw;
        } catch (const std::logic_error& e) {
            throw ParseError(line_no, e.what());
        }
    }

    if (!seen_header) {
        throw ParseError(line_no, "file is empty or has no header");
    }
    return network;
}

Network load_network_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open network file '" + path + "'");
    }
    try {
        return load_network(in);
    } catch (const ParseError& e) {
        throw ParseError(e.line(), path + ": " + e.what());
    }
}

void save_network(const Network& network, std::ostream& out) {
    // Format into a classic-locale buffer so the caller's stream locale cannot change the decimal separator.
    std::ostringstream buf;
    buf.imbue(std::locale::classic());
    buf << std::fixed << std::setprecision(7);

    buf << "metrograph\t1\n";
    for (const Line& l : network.lines()) {
        buf << "L\t" << l.id << '\t' << l.colour << '\t' << l.name << '\n';
    }
    for (const Station& s : network.stations()) {
        buf << "S\t" << s.id << '\t' << s.lat << '\t' << s.lon << '\t' << s.name << '\n';
    }
    for (const Segment& e : network.segments()) {
        buf << "E\t" << e.from << '\t' << e.to << '\t' << e.line << '\t' << e.seconds << '\n';
    }
    out << buf.str();
}

}  // namespace metrograph
