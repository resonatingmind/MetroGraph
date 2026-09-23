#include "metrograph/history.hpp"

#include <charconv>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

#include "text.hpp"

namespace metrograph {

std::string_view to_string(QueryMode mode) noexcept {
    switch (mode) {
        case QueryMode::Fastest:
            return "fastest";
        case QueryMode::FewestStops:
            return "fewest-stops";
    }
    return "unknown";
}

std::optional<QueryMode> parse_query_mode(std::string_view text) noexcept {
    if (text == "fastest") {
        return QueryMode::Fastest;
    }
    if (text == "fewest-stops") {
        return QueryMode::FewestStops;
    }
    return std::nullopt;
}

void write_history_entry(std::ostream& out, const HistoryEntry& entry) {
    std::ostringstream buf;
    buf << "H\t" << entry.timestamp << '\t' << to_string(entry.mode) << '\t' << entry.from << '\t' << entry.to << '\t'
        << entry.total_seconds << '\t';
    for (std::size_t i = 0; i < entry.path.size(); ++i) {
        buf << (i ? "," : "") << entry.path[i];
    }
    buf << '\n';
    out << buf.str();
}

namespace {

template <typename T>
T parse_field(std::string_view field, std::size_t line_no) {
    T value{};
    const char* last = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(field.data(), last, value);
    if (ec != std::errc{} || ptr != last) {
        throw ParseError(line_no, "invalid number '" + std::string(field) + "' in history");
    }
    return value;
}

}  // namespace

std::vector<HistoryEntry> read_history(std::istream& in) {
    std::vector<HistoryEntry> entries;
    std::string raw;
    std::size_t line_no = 0;
    while (std::getline(in, raw)) {
        if (++line_no == 1) {
            detail::strip_utf8_bom(raw);
        }
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        if (raw.empty()) {
            continue;
        }
        const auto f = detail::split(raw, '\t');
        if (f.size() != 7 || f[0] != "H") {
            throw ParseError(line_no, "malformed history record");
        }
        HistoryEntry e;
        e.timestamp = parse_field<std::int64_t>(f[1], line_no);
        const auto mode = parse_query_mode(f[2]);
        if (!mode) {
            throw ParseError(line_no, "unknown query mode '" + std::string(f[2]) + "'");
        }
        e.mode = *mode;
        e.from = parse_field<StationId>(f[3], line_no);
        e.to = parse_field<StationId>(f[4], line_no);
        e.total_seconds = parse_field<std::uint64_t>(f[5], line_no);
        if (!f[6].empty()) {
            for (std::string_view id : detail::split(f[6], ',')) {
                e.path.push_back(parse_field<StationId>(id, line_no));
            }
        }
        entries.push_back(std::move(e));
    }
    return entries;
}

}  // namespace metrograph
