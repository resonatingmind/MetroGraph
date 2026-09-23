#include "metrograph/geo.hpp"

#include <algorithm>
#include <cmath>

namespace metrograph {

double haversine_m(double lat1, double lon1, double lat2, double lon2) noexcept {
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * kDegToRad;
    const double dlon = (lon2 - lon1) * kDegToRad;
    const double s_lat = std::sin(dlat / 2.0);
    const double s_lon = std::sin(dlon / 2.0);
    const double h = s_lat * s_lat + std::cos(lat1 * kDegToRad) * std::cos(lat2 * kDegToRad) * s_lon * s_lon;
    // Clamp guards against h drifting fractionally above 1 for antipodal points.
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(std::min(1.0, h)));
}

}  // namespace metrograph
