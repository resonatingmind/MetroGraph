#pragma once

namespace metrograph {

// Mean Earth radius in metres (IUGG).
inline constexpr double kEarthRadiusM = 6371008.8;

// Great-circle distance in metres between two WGS84 points given in degrees.
double haversine_m(double lat1, double lon1, double lat2, double lon2) noexcept;

}  // namespace metrograph
