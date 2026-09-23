#include "metrograph/nearest.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "metrograph/geo.hpp"

namespace metrograph {

std::vector<NearbyStation> stations_within(const Network& network, double lat, double lon, double radius_m,
                                           std::size_t limit) {
    if (!std::isfinite(radius_m) || radius_m < 0.0) {
        throw std::invalid_argument("radius must be a non-negative number of metres");
    }
    // A linear scan is fine at metro scale (hundreds of stations); a spatial index would only pay off
    // for bus networks with tens of thousands of stops.
    std::vector<NearbyStation> found;
    for (const Station& s : network.stations()) {
        const double d = haversine_m(lat, lon, s.lat, s.lon);
        if (d <= radius_m) {
            found.push_back(NearbyStation{s.id, d});
        }
    }
    std::sort(found.begin(), found.end(), [](const NearbyStation& a, const NearbyStation& b) {
        return a.distance_m != b.distance_m ? a.distance_m < b.distance_m : a.id < b.id;
    });
    if (found.size() > limit) {
        found.resize(limit);
    }
    return found;
}

}  // namespace metrograph
