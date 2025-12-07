set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-march=native")

if(NOT PORT MATCHES "(benchmark|libbacktrace)")
    set(VCPKG_CXX_FLAGS "-fsanitize=thread -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -march=native")
    set(VCPKG_LINKER_FLAGS "-fuse-ld=lld -fsanitize=thread")
else()
    set(VCPKG_CXX_FLAGS "-march=native")
    set(VCPKG_LINKER_FLAGS "-fuse-ld=lld")
endif()
