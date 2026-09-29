#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "console.hpp"
#include "format.hpp"
#include "metrograph/graph.hpp"
#include "metrograph/nearest.hpp"
#include "metrograph/network.hpp"
#include "metrograph/routing.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

using namespace metrograph;

constexpr const char* kDefaultHistoryFile = "metrograph_history.tsv";

void print_usage(std::ostream& out) {
    out << "Usage:\n"
           "  metrograph <network-file> [--history <file>]          interactive console\n"
           "  metrograph <network-file> stats                        station, line and segment counts\n"
           "  metrograph <network-file> route <from> <to> [--fewest-stops]\n"
           "  metrograph <network-file> nearest <lat> <lon> [radius-m]\n"
           "\n"
           "Stations can be given by id, full name, or any unique part of the name.\n";
}

std::optional<double> parse_double(std::string_view s) {
    double value = 0.0;
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), last, value);
    if (s.empty() || ec != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return value;
}

std::optional<StationId> resolve_or_report(const Network& network, std::string_view text) {
    const app::StationMatch match = app::match_station(network, text);
    if (match.id) {
        return match.id;
    }
    if (match.candidates.empty()) {
        std::cerr << "error: no station matches '" << text << "'\n";
    } else {
        std::cerr << "error: '" << text << "' is ambiguous; it matches:\n";
        for (StationId id : match.candidates) {
            std::cerr << "  [" << id << "] " << app::station_name(network, id) << '\n';
        }
    }
    return std::nullopt;
}

int cmd_stats(const Network& network, const Graph& graph) {
    std::size_t interchanges = 0;
    for (const Station& s : network.stations()) {
        if (app::lines_at(network, s.id).size() > 1) {
            ++interchanges;
        }
    }
    std::cout << "Stations:     " << network.stations().size() << '\n'
              << "Lines:        " << network.lines().size() << '\n'
              << "Segments:     " << network.segments().size() << '\n'
              << "Walkways:     " << network.walkways().size() << '\n'
              << "Graph nodes:  " << graph.node_count() << '\n'
              << "Graph edges:  " << graph.edge_count() << '\n'
              << "Interchanges: " << interchanges << " (stations served by more than one line)\n";
    return 0;
}

int cmd_route(const Network& network, const Graph& graph, const std::vector<std::string_view>& args) {
    std::vector<std::string_view> positional;
    QueryMode mode = QueryMode::Fastest;
    for (std::string_view a : args) {
        if (a == "--fewest-stops") {
            mode = QueryMode::FewestStops;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() != 2) {
        print_usage(std::cerr);
        return 2;
    }
    const auto from = resolve_or_report(network, positional[0]);
    const auto to = resolve_or_report(network, positional[1]);
    if (!from || !to) {
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto route =
        mode == QueryMode::Fastest ? fastest_route(graph, *from, *to) : fewest_stops_route(graph, *from, *to);
    const auto elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start);

    if (!route) {
        std::cout << "No route from " << app::station_name(network, *from) << " to "
                  << app::station_name(network, *to) << ".\n";
        return 1;
    }
    std::cout << app::format_route(network, *route, mode) << "\n  (query took " << elapsed.count() << " us)\n";
    return 0;
}

int cmd_nearest(const Network& network, const std::vector<std::string_view>& args) {
    if (args.size() < 2 || args.size() > 3) {
        print_usage(std::cerr);
        return 2;
    }
    const auto lat = parse_double(args[0]);
    const auto lon = parse_double(args[1]);
    const auto radius = args.size() == 3 ? parse_double(args[2]) : std::optional<double>(1000.0);
    if (!lat || !lon || !radius || *radius < 0.0) {
        std::cerr << "error: latitude, longitude and radius must be numbers (radius >= 0)\n";
        return 2;
    }
    const auto found = stations_within(network, *lat, *lon, *radius, 10);
    if (found.empty()) {
        std::cout << "No stations within " << app::format_distance(*radius) << ".\n";
    }
    for (const NearbyStation& n : found) {
        std::cout << app::format_distance(n.distance_m) << "\t[" << n.id << "] " << app::station_name(network, n.id)
                  << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        print_usage(args.empty() ? std::cerr : std::cout);
        return args.empty() ? 2 : 0;
    }

    Network network;
    try {
        network = load_network_file(std::string(args[0]));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    const Graph graph = Graph::from_network(network);

    const std::string_view command = args.size() > 1 ? args[1] : "";
    const std::vector<std::string_view> rest(args.begin() + std::min<std::ptrdiff_t>(2, static_cast<std::ptrdiff_t>(args.size())), args.end());

    if (command.empty() || command == "--history") {
        std::string history = kDefaultHistoryFile;
        if (command == "--history") {
            if (rest.size() != 1) {
                print_usage(std::cerr);
                return 2;
            }
            history = std::string(rest[0]);
        }
        return app::run_console(network, graph, history, std::cin, std::cout);
    }
    if (command == "stats") {
        return cmd_stats(network, graph);
    }
    if (command == "route") {
        return cmd_route(network, graph, rest);
    }
    if (command == "nearest") {
        return cmd_nearest(network, rest);
    }
    std::cerr << "error: unknown command '" << command << "'\n\n";
    print_usage(std::cerr);
    return 2;
}
