# Test executable
find_package(Catch2 CONFIG REQUIRED)

file(GLOB _test_sources test_*.cpp)

foreach(_source ${_test_sources})
    cmake_path(GET _source STEM _target)
    add_executable(${_target} ${_source} scheduler.cpp scheduler.hpp)

    target_link_libraries(${_target} PRIVATE coral::coral)
    target_link_libraries(${_target} PRIVATE Catch2::Catch2 Catch2::Catch2WithMain)

    # Add test
    add_test(NAME ${_target} COMMAND ${_target})

endforeach()
