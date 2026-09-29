#include "metrograph/osm_import.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <istream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../text.hpp"
#include "metrograph/geo.hpp"

namespace metrograph::osm {

namespace {

using OsmId = std::int64_t;

struct OsmNode {
    double lat = 0.0;
    double lon = 0.0;
    std::string name;
};

struct OsmRelation {
    OsmId id = 0;
    std::string name;
    std::string colour;
    std::vector<OsmId> stops;  // stop-role node members, in route order
};

struct DisjointSet {
    std::vector<std::size_t> parent;
    explicit DisjointSet(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), std::size_t{0}); }
    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            x = parent[x] = parent[parent[x]];
        }
        return x;
    }
    void unite(std::size_t a, std::size_t b) { parent[find(a)] = find(b); }
};

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && detail::to_lower(s.substr(s.size() - suffix.size())) == suffix;
}

std::string format_metres(double m) {
    std::ostringstream out;
    out << static_cast<long long>(std::lround(m)) << " m";
    return out.str();
}

// Relation names look like "Blue Line (Dwarka Sector 21 → Noida Electronic City)" or
// "Rapid MetroRail Gurgaon: S55 → Sikandarpur"; the part before the direction is the line.
std::string relation_base_name(const std::string& name) {
    const std::size_t cut = name.find_first_of(":(");
    return std::string(detail::trim(std::string_view(name).substr(0, cut)));
}

// OSM colour tags are usually "#RRGGBB" but sometimes CSS names.
std::string normalise_colour(const std::string& raw, std::vector<std::string>& warnings, const std::string& line) {
    static const std::map<std::string, std::string> kNamed = {
        {"aqua", "#00FFFF"},  {"black", "#000000"}, {"blue", "#0000FF"},   {"cyan", "#00FFFF"},
        {"gray", "#808080"},  {"green", "#008000"}, {"grey", "#808080"},   {"magenta", "#FF00FF"},
        {"orange", "#FFA500"}, {"pink", "#FFC0CB"}, {"purple", "#800080"}, {"red", "#FF0000"},
        {"violet", "#EE82EE"}, {"yellow", "#FFFF00"},
    };
    std::string c = std::string(detail::trim(raw));
    if (c.empty()) {
        warnings.push_back("line '" + line + "' has no colour tag");
        return c;
    }
    if (c.front() == '#' && c.size() == 7) {
        std::transform(c.begin(), c.end(), c.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        return c;
    }
    if (const auto it = kNamed.find(detail::to_lower(c)); it != kNamed.end()) {
        return it->second;
    }
    warnings.push_back("line '" + line + "' has unrecognised colour '" + c + "'");
    return c;
}

void parse_overpass(std::istream& in, std::unordered_map<OsmId, OsmNode>& nodes, std::vector<OsmRelation>& relations) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("not valid Overpass JSON: ") + e.what());
    }
    if (!doc.contains("elements") || !doc["elements"].is_array()) {
        throw std::runtime_error("Overpass JSON has no 'elements' array");
    }
    for (const auto& e : doc["elements"]) {
        const std::string type = e.value("type", "");
        const auto& tags = e.contains("tags") ? e["tags"] : nlohmann::json::object();
        if (type == "node") {
            nodes[e.at("id").get<OsmId>()] =
                OsmNode{e.at("lat").get<double>(), e.at("lon").get<double>(), tags.value("name", "")};
        } else if (type == "relation") {
            OsmRelation r;
            r.id = e.at("id").get<OsmId>();
            r.name = tags.value("name", "relation " + std::to_string(r.id));
            r.colour = tags.value("colour", "");
            for (const auto& m : e.value("members", nlohmann::json::array())) {
                if (m.value("type", "") == "node" && m.value("role", "").rfind("stop", 0) == 0) {
                    r.stops.push_back(m.at("ref").get<OsmId>());
                }
            }
            relations.push_back(std::move(r));
        }
    }
    std::sort(relations.begin(), relations.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
}

template <typename T>
T parse_value(std::string_view field, std::size_t line_no) {
    T value{};
    const char* last = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(field.data(), last, value);
    if (field.empty() || ec != std::errc{} || ptr != last) {
        throw ParseError(line_no, "invalid number '" + std::string(field) + "'");
    }
    return value;
}

}  // namespace

std::string normalise_stop_name(const std::string& name) {
    std::string_view s = detail::trim(name);
    // "Anand Vihar (Pink Line)" -> "Anand Vihar"; keep other parentheticals such as "(East)".
    if (!s.empty() && s.back() == ')') {
        const std::size_t open = s.rfind('(');
        if (open != std::string_view::npos && ends_with_ci(s.substr(0, s.size() - 1), "line")) {
            s = detail::trim(s.substr(0, open));
        }
    }
    if (ends_with_ci(s, " metro station")) {
        s = detail::trim(s.substr(0, s.size() - std::string_view(" metro station").size()));
    }
    return std::string(s);
}

ImportConfig load_config(std::istream& in) {
    ImportConfig config;
    std::string raw;
    std::size_t line_no = 0;
    while (std::getline(in, raw)) {
        if (++line_no == 1) {
            detail::strip_utf8_bom(raw);
        }
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        const std::string_view line = detail::trim(raw);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::vector<std::string_view> f;
        for (std::string_view field : detail::split(line, '\t')) {
            f.push_back(detail::trim(field));
        }
        const std::string_view key = f[0];
        const auto need = [&](std::size_t n) {
            if (f.size() != n) {
                throw ParseError(line_no, "'" + std::string(key) + "' takes " + std::to_string(n - 1) + " value(s)");
            }
        };
        if (key == "speed_kmh") {
            need(2);
            config.speed_kmh = parse_value<double>(f[1], line_no);
        } else if (key == "dwell_seconds") {
            need(2);
            config.dwell_seconds = parse_value<std::uint32_t>(f[1], line_no);
        } else if (key == "merge_radius_m") {
            need(2);
            config.merge_radius_m = parse_value<double>(f[1], line_no);
        } else if (key == "walk_speed_mps") {
            need(2);
            config.walk_speed_mps = parse_value<double>(f[1], line_no);
        } else if (key == "walk_detour") {
            need(2);
            config.walk_detour = parse_value<double>(f[1], line_no);
        } else if (key == "line_speed_kmh") {
            need(3);
            config.line_speed_kmh[std::string(f[1])] = parse_value<double>(f[2], line_no);
        } else if (key == "alias") {
            need(3);
            config.aliases[std::string(f[1])] = std::string(f[2]);
        } else if (key == "walkway") {
            if (f.size() != 3 && f.size() != 4) {
                throw ParseError(line_no, "'walkway' takes two station names and optional seconds");
            }
            WalkwaySpec w{std::string(f[1]), std::string(f[2]), std::nullopt};
            if (f.size() == 4) {
                w.seconds = parse_value<std::uint32_t>(f[3], line_no);
            }
            config.walkways.push_back(std::move(w));
        } else {
            throw ParseError(line_no, "unknown setting '" + std::string(key) + "'");
        }
    }
    if (!(config.speed_kmh > 0.0) || !(config.walk_speed_mps > 0.0) || !(config.walk_detour >= 1.0)) {
        throw ParseError(line_no, "speeds must be positive and walk_detour at least 1");
    }
    for (const auto& [line, speed] : config.line_speed_kmh) {
        if (!(speed > 0.0)) {
            throw ParseError(line_no, "line_speed_kmh for '" + line + "' must be positive");
        }
    }
    return config;
}

ImportResult import_overpass(std::istream& json, const ImportConfig& config) {
    ImportResult result;
    auto& notes = result.notes;
    auto& warnings = result.warnings;

    std::unordered_map<OsmId, OsmNode> nodes;
    std::vector<OsmRelation> relations;
    parse_overpass(json, nodes, relations);

    // ---- 1. normalised name for every stop node --------------------------------------------
    std::map<OsmId, std::string> stop_name;  // ordered: keeps everything below deterministic
    std::size_t stripped = 0;
    for (const OsmRelation& r : relations) {
        for (OsmId id : r.stops) {
            const auto it = nodes.find(id);
            if (it == nodes.end()) {
                throw std::runtime_error("relation " + std::to_string(r.id) + " references node " +
                                         std::to_string(id) + ", which is missing from the extract");
            }
            if (it->second.name.empty()) {
                throw std::runtime_error("stop node " + std::to_string(id) + " in relation '" + r.name +
                                         "' has no name");
            }
            std::string name = normalise_stop_name(it->second.name);
            if (name != it->second.name) {
                ++stripped;
            }
            if (const auto alias = config.aliases.find(name); alias != config.aliases.end()) {
                notes.push_back("alias: '" + name + "' -> '" + alias->second + "'");
                name = alias->second;
            }
            stop_name[id] = std::move(name);
        }
    }
    notes.push_back("normalised " + std::to_string(stripped) + " stop names (line suffixes, 'Metro Station')");

    // ---- 2. merge same-name stop positions into stations -------------------------------------
    std::map<std::string, std::vector<OsmId>> by_name;
    for (const auto& [id, name] : stop_name) {
        by_name[name].push_back(id);
    }

    struct StationDraft {
        std::string name;
        double lat = 0.0;
        double lon = 0.0;
        std::vector<OsmId> members;
    };
    std::vector<StationDraft> drafts;
    for (const auto& [name, ids] : by_name) {
        DisjointSet sets(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) {
            for (std::size_t j = i + 1; j < ids.size(); ++j) {
                const OsmNode& a = nodes.at(ids[i]);
                const OsmNode& b = nodes.at(ids[j]);
                if (haversine_m(a.lat, a.lon, b.lat, b.lon) <= config.merge_radius_m) {
                    sets.unite(i, j);
                }
            }
        }
        std::map<std::size_t, std::vector<OsmId>> clusters;  // keyed by root; members stay in id order
        for (std::size_t i = 0; i < ids.size(); ++i) {
            clusters[sets.find(i)].push_back(ids[i]);
        }
        std::vector<std::vector<OsmId>> ordered;
        for (auto& [root, members] : clusters) {
            ordered.push_back(std::move(members));
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.front() < b.front(); });
        if (ordered.size() > 1) {
            warnings.push_back("'" + name + "' names " + std::to_string(ordered.size()) +
                               " places more than merge_radius_m apart; imported as separate numbered stations");
        }
        for (std::size_t k = 0; k < ordered.size(); ++k) {
            StationDraft d;
            d.name = ordered.size() > 1 ? name + " (" + std::to_string(k + 1) + ")" : name;
            for (OsmId id : ordered[k]) {
                d.lat += nodes.at(id).lat;
                d.lon += nodes.at(id).lon;
            }
            d.lat /= static_cast<double>(ordered[k].size());
            d.lon /= static_cast<double>(ordered[k].size());
            d.members = std::move(ordered[k]);
            drafts.push_back(std::move(d));
        }
    }
    std::sort(drafts.begin(), drafts.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    Network& network = result.network;
    std::unordered_map<OsmId, StationId> station_of;
    for (const StationDraft& d : drafts) {
        const StationId sid = network.add_station(d.name, d.lat, d.lon);
        for (OsmId id : d.members) {
            station_of[id] = sid;
        }
    }
    notes.push_back("merged " + std::to_string(stop_name.size()) + " stop positions into " +
                    std::to_string(network.stations().size()) + " stations");

    // ---- 3. group relations into lines ----------------------------------------------------------
    // Both directions of a service pattern share a base name and the same pair of termini.
    struct LineDraft {
        std::string base;
        std::string colour;
        std::vector<const OsmRelation*> relations;
        std::vector<std::vector<StationId>> sequences;
        std::set<StationId> stations;
        std::string name;
    };
    std::map<std::tuple<std::string, StationId, StationId>, LineDraft> groups;
    for (const OsmRelation& r : relations) {
        std::vector<StationId> seq;
        for (OsmId id : r.stops) {
            const StationId s = station_of.at(id);
            if (!seq.empty() && seq.back() == s) {
                warnings.push_back("relation '" + r.name + "' lists " + network.station(s)->name +
                                   " twice in a row; collapsed");
                continue;
            }
            seq.push_back(s);
        }
        if (seq.size() < 2) {
            warnings.push_back("relation '" + r.name + "' has fewer than two stations; skipped");
            continue;
        }
        const std::string base = relation_base_name(r.name);
        const auto key = std::make_tuple(base, std::min(seq.front(), seq.back()), std::max(seq.front(), seq.back()));
        LineDraft& g = groups[key];
        g.base = base;
        if (g.colour.empty()) {
            g.colour = r.colour;
        }
        g.relations.push_back(&r);
        g.stations.insert(seq.begin(), seq.end());
        g.sequences.push_back(std::move(seq));
    }

    // The largest pattern of each base name keeps the plain name; the others are labelled by termini.
    std::map<std::string, std::vector<LineDraft*>> by_base;
    for (auto& [key, g] : groups) {
        by_base[g.base].push_back(&g);
    }
    std::vector<LineDraft*> ordered_lines;
    for (auto& [base, patterns] : by_base) {
        std::stable_sort(patterns.begin(), patterns.end(), [](const LineDraft* a, const LineDraft* b) {
            if (a->stations.size() != b->stations.size()) {
                return a->stations.size() > b->stations.size();
            }
            return a->relations.size() > b->relations.size();
        });
        for (std::size_t i = 0; i < patterns.size(); ++i) {
            LineDraft& g = *patterns[i];
            const auto& first = g.sequences.front();
            g.name = i == 0 ? base
                            : base + " (" + network.station(first.front())->name + "–" +
                                  network.station(first.back())->name + ")";
            ordered_lines.push_back(&g);
        }
    }

    // ---- 4. segments ---------------------------------------------------------------------------
    std::set<std::tuple<StationId, StationId, LineId>> seen;
    for (const LineDraft* g : ordered_lines) {
        const LineId lid = network.add_line(g->name, normalise_colour(g->colour, warnings, g->name));
        double speed_kmh = config.speed_kmh;
        if (const auto it = config.line_speed_kmh.find(g->name); it != config.line_speed_kmh.end()) {
            speed_kmh = it->second;
        } else if (const auto base_it = config.line_speed_kmh.find(g->base); base_it != config.line_speed_kmh.end()) {
            speed_kmh = base_it->second;
        }
        const double speed_mps = speed_kmh / 3.6;
        for (const auto& seq : g->sequences) {
            for (std::size_t i = 1; i < seq.size(); ++i) {
                const StationId a = seq[i - 1];
                const StationId b = seq[i];
                if (!seen.insert({a, b, lid}).second) {
                    continue;
                }
                const Station& sa = *network.station(a);
                const Station& sb = *network.station(b);
                const double metres = haversine_m(sa.lat, sa.lon, sb.lat, sb.lon);
                const auto seconds = config.dwell_seconds + static_cast<std::uint32_t>(std::lround(metres / speed_mps));
                network.add_segment(a, b, lid, seconds);
            }
        }
        std::ostringstream note;
        note << "line '" << g->name << "': " << g->stations.size() << " stations from " << g->relations.size()
             << " relation(s), " << speed_kmh << " km/h";
        notes.push_back(note.str());
    }
    for (const auto& [a, b, line] : seen) {
        if (seen.count({b, a, line}) == 0) {
            warnings.push_back("one-way segment " + network.station(a)->name + " -> " + network.station(b)->name +
                               " on " + network.line(line)->name);
        }
    }

    // ---- 5. walkways ---------------------------------------------------------------------------
    std::set<std::pair<StationId, StationId>> walk_pairs;
    for (const WalkwaySpec& w : config.walkways) {
        const auto a = network.find_exact(w.a);
        const auto b = network.find_exact(w.b);
        if (!a || !b) {
            throw std::runtime_error("walkway '" + w.a + "' <-> '" + w.b + "' names a station not in the extract: '" +
                                     (!a ? w.a : w.b) + "'");
        }
        const Station& sa = *network.station(*a);
        const Station& sb = *network.station(*b);
        const double metres = haversine_m(sa.lat, sa.lon, sb.lat, sb.lon);
        const auto seconds =
            w.seconds.value_or(static_cast<std::uint32_t>(std::lround(config.walk_detour * metres / config.walk_speed_mps)));
        network.add_walkway(*a, *b, seconds);
        walk_pairs.insert({std::min(*a, *b), std::max(*a, *b)});
        notes.push_back("walkway " + sa.name + " <-> " + sb.name + ": " + format_metres(metres) + ", " +
                        std::to_string(seconds) + " s" + (w.seconds ? " (configured)" : " (estimated)"));
    }

    // ---- 6. sanity checks ----------------------------------------------------------------------
    const std::size_t n = network.stations().size();
    DisjointSet components(n);
    for (const Segment& s : network.segments()) {
        components.unite(s.from, s.to);
    }
    for (const Walkway& w : network.walkways()) {
        components.unite(w.a, w.b);
    }
    std::map<std::size_t, std::vector<StationId>> parts;
    for (StationId s = 0; s < n; ++s) {
        parts[components.find(s)].push_back(s);
    }
    if (parts.size() > 1) {
        for (const auto& [root, members] : parts) {
            if (members.size() * 2 > n) {
                continue;  // the main network
            }
            std::string sample;
            for (std::size_t i = 0; i < members.size() && i < 4; ++i) {
                sample += (i ? ", " : "") + network.station(members[i])->name;
            }
            warnings.push_back("disconnected group of " + std::to_string(members.size()) + " station(s): " + sample +
                               (members.size() > 4 ? ", ..." : ""));
        }
    }

    // Different stations this close are often an interchange that needs a walkway (or an alias).
    constexpr double kSuspiciousM = 150.0;
    for (StationId i = 0; i < n; ++i) {
        for (StationId j = i + 1; j < n; ++j) {
            const Station& a = *network.station(i);
            const Station& b = *network.station(j);
            const double d = haversine_m(a.lat, a.lon, b.lat, b.lon);
            if (d < kSuspiciousM && walk_pairs.count({i, j}) == 0) {
                warnings.push_back("stations " + a.name + " and " + b.name + " are only " + format_metres(d) +
                                   " apart but not linked; missing walkway or alias?");
            }
        }
    }
    return result;
}

}  // namespace metrograph::osm
