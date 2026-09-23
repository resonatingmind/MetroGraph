#include "console.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ctime>
#include <fstream>
#include <iostream>

#include "format.hpp"
#include "metrograph/history.hpp"
#include "metrograph/nearest.hpp"
#include "metrograph/routing.hpp"

namespace metrograph::app {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

template <typename T>
std::optional<T> parse(std::string_view text) {
    text = trim(text);
    T value{};
    const char* last = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), last, value);
    if (text.empty() || ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

// std::nullopt on end of input.
std::optional<std::string> prompt(std::istream& in, std::ostream& out, std::string_view question) {
    out << question << std::flush;
    std::string line;
    if (!std::getline(in, line)) {
        return std::nullopt;
    }
    // Piped input on Windows may start with a UTF-8 byte-order mark.
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        line.erase(0, 3);
    }
    return std::string(trim(line));
}

// Asks until the user names exactly one station. Empty input or end of input cancels.
std::optional<StationId> prompt_station(const Network& network, std::istream& in, std::ostream& out,
                                        std::string_view label) {
    while (true) {
        const auto text = prompt(in, out, std::string(label) + " (name or id, blank to cancel): ");
        if (!text || text->empty()) {
            return std::nullopt;
        }
        StationMatch match = match_station(network, *text);
        if (match.id) {
            return match.id;
        }
        if (match.candidates.empty()) {
            out << "  No station matches '" << *text << "'.\n";
            continue;
        }
        constexpr std::size_t kMaxShown = 15;
        out << "  '" << *text << "' matches " << match.candidates.size() << " stations:\n";
        for (std::size_t i = 0; i < match.candidates.size() && i < kMaxShown; ++i) {
            out << "    " << (i + 1) << ". " << station_name(network, match.candidates[i]) << '\n';
        }
        if (match.candidates.size() > kMaxShown) {
            out << "    ... type more of the name to narrow it down\n";
            continue;
        }
        const auto pick = prompt(in, out, "  Choose a number: ");
        if (!pick) {
            return std::nullopt;
        }
        const auto n = parse<std::size_t>(*pick);
        if (n && *n >= 1 && *n <= match.candidates.size()) {
            return match.candidates[*n - 1];
        }
        out << "  Not a valid choice.\n";
    }
}

class Console {
public:
    Console(const Network& network, const Graph& graph, std::string history_path, std::istream& in, std::ostream& out)
        : network_(network), graph_(graph), history_path_(std::move(history_path)), in_(in), out_(out) {}

    int run() {
        load_history();
        out_ << "MetroGraph: " << network_.stations().size() << " stations, " << network_.lines().size()
             << " lines, " << network_.segments().size() << " segments\n";
        while (true) {
            out_ << "\n"
                    "  1  Find a station\n"
                    "  2  Nearest stations to a location\n"
                    "  3  Fastest route\n"
                    "  4  Fewest-stops route\n"
                    "  5  Route history\n"
                    "  6  Compare two past routes\n"
                    "  0  Quit\n";
            const auto choice = prompt(in_, out_, "> ");
            if (!choice || *choice == "0" || *choice == "q") {
                return 0;
            }
            out_ << '\n';
            if (*choice == "1") {
                find_station();
            } else if (*choice == "2") {
                nearest();
            } else if (*choice == "3") {
                route(QueryMode::Fastest);
            } else if (*choice == "4") {
                route(QueryMode::FewestStops);
            } else if (*choice == "5") {
                show_history();
            } else if (*choice == "6") {
                compare();
            } else {
                out_ << "Unknown option '" << *choice << "'.\n";
            }
        }
    }

private:
    void load_history() {
        std::ifstream file(history_path_, std::ios::binary);
        if (!file) {
            return;  // no history yet
        }
        try {
            history_ = read_history(file);
        } catch (const ParseError& e) {
            out_ << "warning: ignoring unreadable history file " << history_path_ << " (" << e.what() << ")\n";
            history_.clear();
        }
        // Entries written against a different network file would name the wrong stations.
        const auto stale = [&](const HistoryEntry& e) {
            return std::any_of(e.path.begin(), e.path.end(),
                               [&](StationId id) { return network_.station(id) == nullptr; });
        };
        history_.erase(std::remove_if(history_.begin(), history_.end(), stale), history_.end());
    }

    void find_station() {
        const auto text = prompt(in_, out_, "Search for (name fragment or id): ");
        if (!text || text->empty()) {
            return;
        }
        std::vector<StationId> ids;
        if (const auto id = parse<StationId>(*text); id && network_.station(*id)) {
            ids.push_back(*id);
        } else {
            ids = network_.search(*text);
        }
        if (ids.empty()) {
            out_ << "No station matches '" << *text << "'.\n";
            return;
        }
        for (StationId id : ids) {
            const Station& s = *network_.station(id);
            out_ << "  [" << id << "] " << s.name << "  (" << s.lat << ", " << s.lon << ")  "
                 << line_names(network_, lines_at(network_, id)) << '\n';
        }
    }

    void nearest() {
        const auto lat = prompt(in_, out_, "Latitude: ");
        const auto lon = lat ? prompt(in_, out_, "Longitude: ") : std::nullopt;
        const auto radius = lon ? prompt(in_, out_, "Radius in metres [1000]: ") : std::nullopt;
        if (!radius) {
            return;
        }
        const auto la = parse<double>(*lat);
        const auto lo = parse<double>(*lon);
        const auto r = radius->empty() ? std::optional<double>(1000.0) : parse<double>(*radius);
        if (!la || !lo || !r || *r < 0.0) {
            out_ << "Please enter numbers, e.g. 28.6328 and 77.2197.\n";
            return;
        }
        const auto found = stations_within(network_, *la, *lo, *r, 10);
        if (found.empty()) {
            out_ << "No stations within " << format_distance(*r) << ".\n";
            return;
        }
        for (const NearbyStation& n : found) {
            out_ << "  " << format_distance(n.distance_m) << "\t[" << n.id << "] " << station_name(network_, n.id)
                 << '\n';
        }
    }

    void route(QueryMode mode) {
        const auto from = prompt_station(network_, in_, out_, "From");
        const auto to = from ? prompt_station(network_, in_, out_, "To") : std::nullopt;
        if (!to) {
            return;
        }
        const auto result =
            mode == QueryMode::Fastest ? fastest_route(graph_, *from, *to) : fewest_stops_route(graph_, *from, *to);
        if (!result) {
            out_ << "No route from " << station_name(network_, *from) << " to " << station_name(network_, *to)
                 << ".\n";
            return;
        }
        out_ << '\n' << format_route(network_, *result, mode);

        HistoryEntry entry;
        entry.timestamp = static_cast<std::int64_t>(std::time(nullptr));
        entry.mode = mode;
        entry.from = *from;
        entry.to = *to;
        entry.total_seconds = result->total_seconds;
        entry.path.assign(result->nodes.begin(), result->nodes.end());
        history_.push_back(entry);

        std::ofstream file(history_path_, std::ios::app | std::ios::binary);
        if (file) {
            write_history_entry(file, entry);
        } else {
            out_ << "warning: could not write to history file " << history_path_ << '\n';
        }
        out_ << "\nSaved as history entry #" << history_.size() << ".\n";
    }

    void show_history() {
        if (history_.empty()) {
            out_ << "No routes yet. Past queries are kept in " << history_path_ << ".\n";
            return;
        }
        for (std::size_t i = 0; i < history_.size(); ++i) {
            const HistoryEntry& e = history_[i];
            out_ << "  #" << (i + 1) << "  " << format_timestamp(e.timestamp) << "  " << to_string(e.mode) << "  "
                 << station_name(network_, e.from) << " -> " << station_name(network_, e.to) << "  "
                 << format_duration(e.total_seconds) << ", " << e.stops() << (e.stops() == 1 ? " stop\n" : " stops\n");
        }
    }

    void compare() {
        if (history_.size() < 2) {
            out_ << "Need at least two past routes to compare.\n";
            return;
        }
        show_history();
        const auto first = prompt(in_, out_, "First entry #: ");
        const auto second = first ? prompt(in_, out_, "Second entry #: ") : std::nullopt;
        if (!second) {
            return;
        }
        const auto valid = [&](const std::optional<std::size_t>& n) { return n && *n >= 1 && *n <= history_.size(); };
        const auto a = parse<std::size_t>(*first);
        const auto b = parse<std::size_t>(*second);
        if (!valid(a) || !valid(b)) {
            out_ << "Entry numbers must be between 1 and " << history_.size() << ".\n";
            return;
        }
        out_ << '\n' << format_comparison(network_, history_[*a - 1], *a, history_[*b - 1], *b);
    }

    const Network& network_;
    const Graph& graph_;
    std::string history_path_;
    std::istream& in_;
    std::ostream& out_;
    std::vector<HistoryEntry> history_;
};

}  // namespace

StationMatch match_station(const Network& network, std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return {};
    }
    if (const auto id = parse<StationId>(text)) {
        return network.station(*id) ? StationMatch{id, {}} : StationMatch{};
    }
    if (const auto exact = network.find_exact(text)) {
        return StationMatch{exact, {}};
    }
    auto candidates = network.search(text);
    if (candidates.size() == 1) {
        return StationMatch{candidates.front(), {}};
    }
    return StationMatch{std::nullopt, std::move(candidates)};
}

int run_console(const Network& network, const Graph& graph, const std::string& history_path, std::istream& in,
                std::ostream& out) {
    return Console(network, graph, history_path, in, out).run();
}

}  // namespace metrograph::app
