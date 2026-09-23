#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "metrograph/graph.hpp"

namespace metrograph {

struct Step {
    NodeId from = 0;
    NodeId to = 0;
    LineId line = 0;
    std::uint32_t seconds = 0;
};

// nodes = {source, ..., destination}; steps[i] joins nodes[i] to nodes[i + 1].
struct Route {
    std::vector<NodeId> nodes;
    std::vector<Step> steps;
    std::uint64_t total_seconds = 0;

    std::size_t stops() const noexcept { return steps.size(); }
};

// Dijkstra: minimises total edge weight. std::nullopt if the destination is unreachable.
// Throws std::out_of_range for unknown node ids.
std::optional<Route> fastest_route(const Graph& graph, NodeId source, NodeId destination);

// BFS: minimises the number of edges (stops), ignoring weights. total_seconds is still
// filled in from the edges it happens to use.
std::optional<Route> fewest_stops_route(const Graph& graph, NodeId source, NodeId destination);

}  // namespace metrograph
