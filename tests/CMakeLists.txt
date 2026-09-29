include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(Catch2)

add_executable(metrograph_tests
    test_geo.cpp
    test_history.cpp
    test_nearest.cpp
    test_network.cpp
    test_routing.cpp
)
target_link_libraries(metrograph_tests PRIVATE metrograph_core metrograph_warnings Catch2::Catch2WithMain)
target_compile_definitions(metrograph_tests PRIVATE
    METROGRAPH_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    METROGRAPH_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
if(METROGRAPH_BUILD_IMPORTER)
    target_sources(metrograph_tests PRIVATE test_osm_import.cpp)
    target_link_libraries(metrograph_tests PRIVATE metrograph_osm)
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(metrograph_tests)

# End-to-end smoke tests of the console binary on the fixture network.
set(MINI ${CMAKE_CURRENT_SOURCE_DIR}/data/mini.graph)
add_test(NAME cli.route COMMAND metrograph ${MINI} route North Harbour)
set_tests_properties(cli.route PROPERTIES PASS_REGULAR_EXPRESSION "Fastest route: North -> Harbour.*5 min 10 s, 3 stops, 1 line change")
add_test(NAME cli.fewest_stops COMMAND metrograph ${MINI} route North South --fewest-stops)
set_tests_properties(cli.fewest_stops PROPERTIES PASS_REGULAR_EXPRESSION "10 min 00 s, 1 stop, 0 line changes")
add_test(NAME cli.stats COMMAND metrograph ${MINI} stats)
set_tests_properties(cli.stats PROPERTIES PASS_REGULAR_EXPRESSION "Stations: +6.*Interchanges: 3 ")
add_test(NAME cli.missing_file COMMAND metrograph does-not-exist.graph stats)
set_tests_properties(cli.missing_file PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.ambiguous_station COMMAND metrograph ${MINI} route "o" Harbour)
set_tests_properties(cli.ambiguous_station PROPERTIES WILL_FAIL TRUE)
