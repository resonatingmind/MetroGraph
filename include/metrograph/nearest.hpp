#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include "metrograph/network.hpp"

namespace metrograph {

struct NearbyStation {
    StationId id = 0;
    double distance_m = 0.0;
};

// Stations within radius_m of (lat, lon), nearest first, at most `limit` results.
// Throws std::invalid_argument for a negative or non-finite radius.
std::vector<NearbyStation> stations_within(const Network& network, double lat, double lon, double radius_m,
                                           std::size_t limit = std::numeric_limits<std::size_t>::max());

}  // namespace metrograph
