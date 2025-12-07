cmake_minimum_required(VERSION 3.20)

project(coral-examples LANGUAGES CXX)

if(NOT CORAL_BUILD_TESTS)
    find_package(coral REQUIRED)
endif()

file(GLOB _examples example_*.cpp)

foreach(_source ${_examples})
    cmake_path(GET _source STEM _target)

    add_executable(${_target} ${_source})
    target_compile_features(${_target} PRIVATE cxx_std_23)
    target_link_libraries(${_target} PRIVATE coral::coral)
endforeach()
