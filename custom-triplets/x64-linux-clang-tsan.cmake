set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/clang-toolchain.cmake")

set(VCPKG_C_FLAGS "-march=native")

if(NOT PORT MATCHES "(benchmark|libbacktrace)")
    set(VCPKG_CXX_FLAGS "-stdlib=libc++ -fsanitize=alignment,null,thread -g -fno-omit-frame-pointer -fno-optimize-sibling-calls -march=x86-64-v4")
    set(VCPKG_LINKER_FLAGS "-stdlib=libc++ -fuse-ld=lld -ltsan")
else()
    set(VCPKG_CXX_FLAGS "-stdlib=libc++ -march=native")
    set(VCPKG_LINKER_FLAGS "-stdlib=libc++ -fuse-ld=lld")
endif()
