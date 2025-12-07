cmake_minimum_required(VERSION 3.20)

project(coral
    VERSION 0.1.0
    DESCRIPTION "A header-only C++20 coroutine library"
    LANGUAGES CXX
)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Options
option(CORAL_BUILD_TESTS "Build tests" OFF)
option(CORAL_BUILD_EXAMPLES "Build examples" OFF)
option(CORAL_INSTALL "Generate install target" ON)

# Header-only library
add_library(coral INTERFACE)
add_library(coral::coral ALIAS coral)

target_include_directories(coral INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(coral INTERFACE cxx_std_20)

# Tests
if(CORAL_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

# Examples
if(CORAL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
if(CORAL_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    # Install headers
    install(DIRECTORY include/coral
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Install targets
    install(TARGETS coral
        EXPORT coralTargets
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Install export
    install(EXPORT coralTargets
        FILE coralTargets.cmake
        NAMESPACE coral::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coral
    )

    # Create config file
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/coralConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/coralConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coral
    )

    # Create version file
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/coralConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Install config files
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/coralConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/coralConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/coral
    )
endif()
