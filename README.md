# MetroGraph

A transit routing engine in C++17. It models a metro network as a weighted graph and answers
**fastest-route** (Dijkstra) and **fewest-stop** (BFS) queries, plus station lookup, radius-based
nearest-station search, and a persistent query history with side-by-side comparison.

> **Status: work in progress.** The engine, console app, tests and CI are in place. Still to do:
> importing the Delhi Metro network from OpenStreetMap (a raw extract is already in `data/raw/`),
> transfer-aware routing, and a WebAssembly demo. See [Roadmap](#roadmap).

## Build

Requires CMake 3.25+, Ninja and a C++17 compiler (MSVC 2022, GCC 11+ or Clang 14+).
Catch2 is fetched automatically at configure time.

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
Sample run on the six-station test fixture:

```text
$ metrograph tests/data/mini.graph route North Harbour
Fastest route: North -> Harbour
  5 min 10 s, 3 stops, 1 line change

  Red Line              North -> Central                                    1 stop    2 min 00 s
  Blue Line             Central -> Harbour                                  2 stops   3 min 10 s
```

## Architecture

```text
include/metrograph/      public headers of the core library
  network.hpp            Network: stations, lines, directed segments; text loader/saver
  graph.hpp              Graph: adjacency list built from a Network
  routing.hpp            fastest_route (Dijkstra), fewest_stops_route (BFS)
  nearest.hpp            stations_within: haversine radius search
  history.hpp            append-only query history format
src/                     core library implementation (metrograph_core)
app/                     console front end: argument parsing, interactive menu, formatting
tests/                   Catch2 unit tests, property tests and CLI smoke tests
data/raw/                raw OpenStreetMap extract and the Overpass query that produced it
```

The **network** (what exists) is kept separate from the **graph** (what is routed over). Today the
graph has one node per station. For transfer modelling it will have one node per (station, line) pair,
joined by transfer edges. That change stays inside `Graph::from_network`; the routing algorithms
don't change.

### Network file format

A tab-separated text file with one record per line. Ids are dense and appear in order:

```text
metrograph  1
L  <id>  <colour>  <name>                  line
S  <id>  <lat>  <lon>  <name>              station
E  <from>  <to>  <line>  <seconds>          directed segment
```

## Testing

`ctest --preset debug` runs:

- unit tests for loading/saving, lookup, haversine distance, nearest search and history;
- **property tests** on thousands of random graphs, which check that:
  - on unit-weight graphs, Dijkstra's cost equals BFS's hop count;
  - every returned route is a connected walk from origin to destination that uses real graph edges, with a total equal to the sum of its steps;
  - Dijkstra's cost matches a Floyd–Warshall reference on every pair of nodes;
- CLI smoke tests of the built binary.

CI builds and tests with GCC, Clang and MSVC, and runs the suite under ASan and UBSan.

## Roadmap

1. Import the Delhi Metro network from OpenStreetMap
2. Transfer modelling: per-line nodes with an interchange penalty
3. A* with a haversine heuristic, plus latency benchmarks
4. A WebAssembly demo on GitHub Pages

## Data

Network data © OpenStreetMap contributors, available under the
[Open Database License](https://www.openstreetmap.org/copyright).
