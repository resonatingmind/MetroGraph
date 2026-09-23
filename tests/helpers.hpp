#pragma once

#include <cstdint>
#include <random>
#include <string>

#include "metrograph/graph.hpp"
#include "metrograph/routing.hpp"

namespace metrograph::test {

// Checks, independently of the routing code, that `route` is a real walk through `graph`
// from source to destination. Returns an empty string when valid, otherwise what is wrong.
inline std::string route_problem(const Graph& graph, const Route& route, NodeId source, NodeId destination) {
    if (route.nodes.empty()) {
        return "route has no nodes";
    }
    if (route.nodes.size() != route.steps.size() + 1) {
        return "nodes/steps size mismatch";
    }
    if (route.nodes.front() != source) {
        return "route does not start at the source";
    }
    if (route.nodes.back() != destination) {
        return "route does not end at the destination";
    }
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < route.steps.size(); ++i) {
        const Step& step = route.steps[i];
        if (step.from != route.nodes[i] || step.to != route.nodes[i + 1]) {
            return "step " + std::to_string(i) + " does not join consecutive nodes";
        }
        bool found = false;
        for (const Edge& e : graph.neighbours(step.from)) {
            if (e.to == step.to && e.line == step.line && e.seconds == step.seconds) {
                found = true;
                break;
            }
        }
        if (!found) {
            return "step " + std::to_string(i) + " uses an edge that is not in the graph";
        }
        total += step.seconds;
    }
    if (total != route.total_seconds) {
        return "total_seconds does not equal the sum of its steps";
    }
    return "";
}

// Random directed multigraph; weight 1 everywhere when max_weight == 1.
inline Graph random_graph(std::mt19937& rng, std::size_t nodes, std::size_t edges, std::uint32_t max_weight) {
    Graph graph(nodes);
    std::uniform_int_distribution<NodeId> pick(0, static_cast<NodeId>(nodes - 1));
    std::uniform_int_distribution<std::uint32_t> weight(1, max_weight);
    std::uniform_int_distribution<LineId> line(0, 3);
    for (std::size_t i = 0; i < edges; ++i) {
        const NodeId a = pick(rng);
        const NodeId b = pick(rng);
        if (a != b) {
            graph.add_edge(a, b, weight(rng), line(rng));
        }
    }
    return graph;
}

}  // namespace metrograph::test
