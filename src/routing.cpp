#include "metrograph/routing.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace metrograph {

namespace {

constexpr std::uint64_t kUnreached = std::numeric_limits<std::uint64_t>::max();
constexpr NodeId kNoNode = std::numeric_limits<NodeId>::max();

// How a node was reached: the predecessor and the index of the edge taken in its adjacency list.
struct Parent {
    NodeId node = kNoNode;
    std::uint32_t edge_index = 0;
};

void check_node(const Graph& graph, NodeId node) {
    if (node >= graph.node_count()) {
        throw std::out_of_range("node id " + std::to_string(node) + " is out of range");
    }
}

Route build_route(const Graph& graph, const std::vector<Parent>& parent, NodeId source, NodeId destination) {
    Route route;
    for (NodeId current = destination; current != source;) {
        const Parent& p = parent[current];
        const Edge& edge = graph.neighbours(p.node)[p.edge_index];
        route.steps.push_back(Step{p.node, current, edge.line, edge.seconds});
        current = p.node;
    }
    std::reverse(route.steps.begin(), route.steps.end());

    route.nodes.reserve(route.steps.size() + 1);
    route.nodes.push_back(source);
    for (const Step& step : route.steps) {
        route.nodes.push_back(step.to);
        route.total_seconds += step.seconds;
    }
    return route;
}

}  // namespace

std::optional<Route> fastest_route(const Graph& graph, NodeId source, NodeId destination) {
    check_node(graph, source);
    check_node(graph, destination);

    std::vector<std::uint64_t> dist(graph.node_count(), kUnreached);
    std::vector<Parent> parent(graph.node_count());

    using Item = std::pair<std::uint64_t, NodeId>;  // (distance, node), smallest first
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> frontier;
    dist[source] = 0;
    frontier.push({0, source});

    while (!frontier.empty()) {
        const auto [d, u] = frontier.top();
        frontier.pop();
        if (d != dist[u]) {
            continue;  // stale entry; u was already settled with a shorter distance
        }
        if (u == destination) {
            break;  // settled nodes never improve, so we can stop early
        }
        const auto& edges = graph.neighbours(u);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const Edge& e = edges[i];
            const std::uint64_t candidate = d + e.seconds;
            if (candidate < dist[e.to]) {
                dist[e.to] = candidate;
                parent[e.to] = Parent{u, static_cast<std::uint32_t>(i)};
                frontier.push({candidate, e.to});
            }
        }
    }

    if (dist[destination] == kUnreached) {
        return std::nullopt;
    }
    return build_route(graph, parent, source, destination);
}

std::optional<Route> fewest_stops_route(const Graph& graph, NodeId source, NodeId destination) {
    check_node(graph, source);
    check_node(graph, destination);

    std::vector<bool> seen(graph.node_count(), false);
    std::vector<Parent> parent(graph.node_count());
    std::queue<NodeId> frontier;
    seen[source] = true;
    frontier.push(source);

    while (!frontier.empty() && !seen[destination]) {
        const NodeId u = frontier.front();
        frontier.pop();
        const auto& edges = graph.neighbours(u);
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const NodeId v = edges[i].to;
            if (!seen[v]) {
                seen[v] = true;
                parent[v] = Parent{u, static_cast<std::uint32_t>(i)};
                frontier.push(v);
            }
        }
    }

    if (!seen[destination]) {
        return std::nullopt;
    }
    return build_route(graph, parent, source, destination);
}

}  // namespace metrograph
