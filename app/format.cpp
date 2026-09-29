#include "format.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace metrograph::app {

namespace {

bool is_utf8_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// Display width in code points; good enough for the Latin names in metro data (no wide CJK glyphs).
std::size_t display_width(const std::string& s) {
    return static_cast<std::size_t>(std::count_if(s.begin(), s.end(), [](char c) { return !is_utf8_continuation(c); }));
}

// Byte offset just past the first `n` code points.
std::size_t prefix_bytes(const std::string& s, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t seen = 0; i < s.size(); ++i) {
        if (!is_utf8_continuation(s[i]) && seen++ == n) {
            break;
        }
    }
    return i;
}

// Pads or truncates to exactly `width` display columns without splitting a UTF-8 sequence.
std::string fit(const std::string& s, std::size_t width) {
    const std::size_t w = display_width(s);
    if (w > width) {
        return width > 3 ? s.substr(0, prefix_bytes(s, width - 3)) + "..." : s.substr(0, prefix_bytes(s, width));
    }
    return s + std::string(width - w, ' ');
}

std::string line_name(const Network& network, LineId id) {
    if (id == kWalkway) {
        return "Walk";
    }
    const Line* line = network.line(id);
    return line ? line->name : "?";
}

std::string plural(std::size_t n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

}  // namespace

std::string format_duration(std::uint64_t seconds) {
    const std::uint64_t h = seconds / 3600;
    const std::uint64_t m = (seconds % 3600) / 60;
    const std::uint64_t s = seconds % 60;
    std::ostringstream out;
    if (h > 0) {
        out << h << " h " << std::setw(2) << std::setfill('0') << m << " min";
    } else if (m > 0) {
        out << m << " min " << std::setw(2) << std::setfill('0') << s << " s";
    } else {
        out << s << " s";
    }
    return out.str();
}

std::string format_distance(double metres) {
    std::ostringstream out;
    if (metres < 1000.0) {
        out << static_cast<long long>(metres + 0.5) << " m";
    } else {
        out << std::fixed << std::setprecision(1) << metres / 1000.0 << " km";
    }
    return out.str();
}

std::string format_timestamp(std::int64_t unix_seconds) {
    const auto t = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

std::string station_name(const Network& network, StationId id) {
    const Station* s = network.station(id);
    return s ? s->name : "?";
}

std::vector<LineId> lines_at(const Network& network, StationId id) {
    std::vector<LineId> lines;
    for (const Segment& seg : network.segments()) {
        if (seg.from == id || seg.to == id) {
            lines.push_back(seg.line);
        }
    }
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    return lines;
}

std::string line_names(const Network& network, const std::vector<LineId>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += (i ? ", " : "") + line_name(network, lines[i]);
    }
    return out.empty() ? "(no lines)" : out;
}

std::string format_route(const Network& network, const Route& route, QueryMode mode) {
    std::ostringstream out;
    const char* title = mode == QueryMode::Fastest ? "Fastest route" : "Fewest-stops route";
    out << title << ": " << station_name(network, route.nodes.front()) << " -> "
        << station_name(network, route.nodes.back()) << '\n';

    if (route.steps.empty()) {
        out << "  Origin and destination are the same station.\n";
        return out.str();
    }

    // A leg is a maximal run of consecutive steps on the same line.
    struct Leg {
        LineId line;
        StationId from;
        StationId to;
        std::size_t stops;
        std::uint64_t seconds;
    };
    std::vector<Leg> legs;
    for (const Step& step : route.steps) {
        if (legs.empty() || legs.back().line != step.line) {
            legs.push_back(Leg{step.line, step.from, step.to, 0, 0});
        }
        Leg& leg = legs.back();
        leg.to = step.to;
        leg.stops += 1;
        leg.seconds += step.seconds;
    }

    // A walk between stations is part of an interchange, not a change of its own.
    const auto train_legs = static_cast<std::size_t>(
        std::count_if(legs.begin(), legs.end(), [](const Leg& leg) { return leg.line != kWalkway; }));
    const std::size_t train_stops = static_cast<std::size_t>(
        std::count_if(route.steps.begin(), route.steps.end(), [](const Step& s) { return s.line != kWalkway; }));
    out << "  " << format_duration(route.total_seconds) << ", " << plural(train_stops, "stop") << ", "
        << plural(train_legs > 0 ? train_legs - 1 : 0, "line change") << "\n\n";
    for (const Leg& leg : legs) {
        const bool walk = leg.line == kWalkway;
        out << "  " << fit(line_name(network, leg.line), 22)
            << fit(station_name(network, leg.from) + " -> " + station_name(network, leg.to), 52)
            << fit(walk ? "" : plural(leg.stops, "stop"), 10) << format_duration(leg.seconds) << '\n';
    }
    return out.str();
}

std::string format_comparison(const Network& network, const HistoryEntry& a, std::size_t a_index,
                              const HistoryEntry& b, std::size_t b_index) {
    constexpr std::size_t kLabel = 10;
    constexpr std::size_t kColumn = 36;
    std::ostringstream out;
    const auto row = [&](const std::string& label, const std::string& left, const std::string& right) {
        out << fit(label, kLabel) << fit(left, kColumn) << "  " << right << '\n';
    };

    row("", "#" + std::to_string(a_index), "#" + std::to_string(b_index));
    row("Query", station_name(network, a.from) + " -> " + station_name(network, a.to),
        station_name(network, b.from) + " -> " + station_name(network, b.to));
    row("Mode", std::string(to_string(a.mode)), std::string(to_string(b.mode)));
    row("Asked", format_timestamp(a.timestamp), format_timestamp(b.timestamp));
    row("Time", format_duration(a.total_seconds), format_duration(b.total_seconds));
    row("Stops", std::to_string(a.stops()), std::to_string(b.stops()));
    out << '\n';

    const std::size_t rows = std::max(a.path.size(), b.path.size());
    for (std::size_t i = 0; i < rows; ++i) {
        const std::string left = i < a.path.size() ? station_name(network, a.path[i]) : "";
        const std::string right = i < b.path.size() ? station_name(network, b.path[i]) : "";
        const std::string marker = (i < a.path.size() && i < b.path.size() && a.path[i] == b.path[i]) ? "=" : " ";
        row(std::to_string(i + 1) + " " + marker, left, right);
    }
    return out.str();
}

}  // namespace metrograph::app
