# MetroGraph

A transit routing engine in C++17, running on the real Delhi NCR metro network built from
OpenStreetMap data (276 stations, 14 line patterns). It answers **fastest-route** (Dijkstra) and
**fewest-stop** (BFS) queries, plus station lookup, radius-based nearest-station search, and a
persistent query history with side-by-side comparison.

> **Status: work in progress.** Done: the engine, the console app, the OpenStreetMap importer, tests
> and CI. Still to do: transfer-aware routing and a WebAssembly demo. See [Roadmap](#roadmap).

## Build

Requires CMake 3.25+, Ninja and a C++17 compiler (MSVC 2022, GCC 11+ or Clang 14+).
Catch2 and nlohmann/json are fetched automatically at configure time.

```sh
cmake --workflow --preset release      # configure + build + test
```

On Windows, first load the MSVC environment into your PowerShell session:

```powershell
. .\scripts\dev-shell.ps1
```

Other presets: `debug`, and `asan` (GCC/Clang only), which builds everything with AddressSanitizer and
UndefinedBehaviorSanitizer.

## Usage

```text
metrograph <network-file> [--history <file>]          interactive console
metrograph <network-file> stats
metrograph <network-file> route <from> <to> [--fewest-stops]
metrograph <network-file> nearest <lat> <lon> [radius-m]
```

Stations can be given by id, by full name (case-insensitive), or by any unique part of the name.

```text
$ metrograph data/delhi.graph route "Pari Chowk" "Rajiv Chowk"
Fastest route: Pari Chowk -> Rajiv Chowk
  1 h 21 min, 33 stops, 1 line change

  Aqua Line             Pari Chowk -> Noida Sector 51                       16 stops  40 min 27 s
  Walk                  Noida Sector 51 -> Noida Sector 52                            4 min 50 s
  Blue Line             Noida Sector 52 -> Rajiv Chowk                      17 stops  35 min 49 s
```

Journey times are estimates; see [Assumptions](#assumptions).

## Data

`data/delhi.graph` is generated from an OpenStreetMap extract by the project's own importer, and a test
fails if the committed file drifts from what the importer produces.

```sh
# 1. Refresh the extract (optional): POST data/raw/delhi_query.overpassql to an Overpass API server
#    and save the response as data/raw/delhi_overpass.json.
# 2. Regenerate the network:
cmake --build --preset release --target import-delhi
```

The extract covers DMRC, Rapid Metro Gurgaon and Noida Metro (the Aqua Line). RapidX (Delhi–Meerut RRTS) is
regional rail and deliberately left out. OSM doesn't model a metro as a graph, so the importer
(`src/osm/osm_import.cpp`) has to derive one:

| OSM has                                                  | MetroGraph needs                   | How it is derived                                                                                                   |
|----------------------------------------------------------|------------------------------------|---------------------------------------------------------------------------------------------------------------------|
| One route relation per direction and service pattern     | One line per pattern               | Relations with the same base name and termini are paired, so 28 relations become 14 lines.                         |
| One stop position per line and direction at each station | One station                        | Names are normalised ("Anand Vihar (Pink Line)" becomes "Anand Vihar"), then same-name stops within 300 m are merged: 590 stop positions become 276 stations. |
| Ordered stop members                                     | Directed segments                  | Consecutive stops, deduplicated.                                                                                    |
| Differently named stations joined by a skywalk           | Walkways                           | Three official interchanges are listed in `data/delhi_import.tsv`.                                                 |
| No timetables                                            | Segment times                      | Estimated; see [Assumptions](#assumptions).                                                                         |

The importer's sanity checks flag disconnected groups of stations, one-way segments, same-name places that
are far apart, and unlinked stations under 150 m apart. The current extract passes all of them.

### Assumptions

These numbers are **assumed, not measured**. They are all set in `data/delhi_import.tsv`:

- **Segment time** = 25 s dwell + straight-line distance at 40 km/h, or 70 km/h on the Airport Express.
  Track is rarely straight, so the distances are slight underestimates.
- **Walkway time** = 1.3 × straight-line distance at 1.2 m/s.
- **Changing lines is free** in the current model, so the fastest route sometimes changes line to save a
  minute. Transfer modelling (roadmap item 1) addresses this.

The next milestone calibrates these values against the official DMRC journey planner, and the comparison
table will be added here.

## Architecture

```text
include/metrograph/      public headers
  network.hpp            Network: stations, lines, directed segments, walkways; text loader/saver
  graph.hpp              Graph: adjacency list built from a Network
  routing.hpp            fastest_route (Dijkstra), fewest_stops_route (BFS)
  nearest.hpp            stations_within: haversine radius search
  history.hpp            append-only query history format
  osm_import.hpp         Overpass JSON -> Network
src/                     metrograph_core (no third-party dependencies)
src/osm/                 metrograph_osm, the importer (uses nlohmann/json)
app/                     console front end: argument parsing, interactive menu, formatting
tools/                   metrograph_import command-line tool
tests/                   Catch2 unit, property, importer and CLI smoke tests
data/                    generated Delhi network, import settings, raw OSM extract
```

The **network** (what exists) is kept separate from the **graph** (what is routed over). Today the
graph has one node per station. For transfer modelling it will have one node per (station, line) pair,
joined by transfer edges. That change stays inside `Graph::from_network`; the routing algorithms
don't change. The importer is a separate library, so the engine has no JSON dependency. That matters
for the planned WebAssembly build.

### Network file format

A tab-separated text file with one record per line. Ids are dense and appear in order:

```text
metrograph  1
L  <id>  <colour>  <name>                  line
S  <id>  <lat>  <lon>  <name>              station
E  <from>  <to>  <line>  <seconds>         directed segment
W  <station>  <station>  <seconds>         two-way walkway
```

## Testing

`ctest --preset debug` runs:

- unit tests for loading/saving, lookup, haversine distance, nearest search and history;
- **property tests** on thousands of random graphs, which check that:
  - on unit-weight graphs, Dijkstra's cost equals BFS's hop count;
  - every returned route is a connected walk from origin to destination that uses real graph edges, with a total equal to the sum of its steps;
  - Dijkstra's cost matches a Floyd–Warshall reference on every pair of nodes;
- **importer tests** on a hand-written OSM fixture covering each rule above, plus checks on the real Delhi
  extract: it imports with no warnings, every station can reach every other, known interchanges such as
  Kashmere Gate (three lines) are single stations, and `data/delhi.graph` is up to date;
- CLI smoke tests of the built binary.

CI builds and tests with GCC, Clang and MSVC, and runs the suite under ASan and UBSan.

## Roadmap

1. Transfer modelling: per-line nodes with an interchange penalty, and a verification table against the
   DMRC journey planner
2. A* with a haversine heuristic, plus latency benchmarks
3. A WebAssembly demo on GitHub Pages

## Licence and attribution

Network data © OpenStreetMap contributors, available under the
[Open Database License](https://www.openstreetmap.org/copyright). `data/delhi.graph` is a derived
database, and it is distributed under the same licence.
