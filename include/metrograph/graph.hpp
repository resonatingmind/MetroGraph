#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "metrograph/network.hpp"

namespace metrograph {

using NodeId = std::uint32_t;

struct Edge {
    NodeId to = 0;
    std::uint32_t seconds = 0;
    LineId line = 0;
};

// Directed, weighted adjacency-list graph. Routing algorithms run on this, not on Network,
// so the node model can change (e.g. one node per station per line) without touching them.
class Graph {
public:
    explicit Graph(std::size_t node_count = 0);

    // One node per station, one edge per segment, two edges (line kWalkway) per walkway.
    static Graph from_network(const Network& network);

    void add_edge(NodeId from, NodeId to, std::uint32_t seconds, LineId line);

    std::size_t node_count() const noexcept { return adjacency_.size(); }
    std::size_t edge_count() const noexcept { return edge_count_; }
    const std::vector<Edge>& neighbours(NodeId node) const { return adjacency_.at(node); }

private:
    std::vector<std::vector<Edge>> adjacency_;
    std::size_t edge_count_ = 0;
};

}  // namespace metrograph
