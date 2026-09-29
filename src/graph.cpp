#include "metrograph/graph.hpp"

#include <stdexcept>

namespace metrograph {

Graph::Graph(std::size_t node_count) : adjacency_(node_count) {}

Graph Graph::from_network(const Network& network) {
    Graph graph(network.stations().size());
    for (const Segment& s : network.segments()) {
        graph.add_edge(s.from, s.to, s.seconds, s.line);
    }
    for (const Walkway& w : network.walkways()) {
        graph.add_edge(w.a, w.b, w.seconds, kWalkway);
        graph.add_edge(w.b, w.a, w.seconds, kWalkway);
    }
    return graph;
}

void Graph::add_edge(NodeId from, NodeId to, std::uint32_t seconds, LineId line) {
    if (from >= adjacency_.size() || to >= adjacency_.size()) {
        throw std::out_of_range("edge references an unknown node");
    }
    adjacency_[from].push_back(Edge{to, seconds, line});
    ++edge_count_;
}

}  // namespace metrograph
