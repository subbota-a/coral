# Coral Examples

| Target | Description |
| ------ | ----------- |
| example_01 | Racing coroutines with `when_any`, cancellation via `std::stop_token`, signal handling with `when_signal`, and custom awaitable implementation. |
| example_02 | Structured concurrency with `nursery_task` and `coral::nursery` for spawning dynamic child coroutines. Demonstrates `when_stopped` awaitable. |
| example_03 | Lazy value sequences with `coral::generator<T>`. Shows `iota`, infinite `fibonacci`, mutable references, and C++20/23 range integration. |

## Building

### Option 1: As part of the coral project

```bash
cmake -S .. -B build -DCORAL_BUILD_EXAMPLES=ON
cmake --build build
./build/examples/example_01
```

### Option 2: As a standalone project

First, install coral using one of these methods:
- **Manual install**: Build and install the library (see [build instructions](../README.md#building-and-installation))
- **vcpkg**: Add `coral` to your [vcpkg.json](vcpkg.json) dependencies

Then add to your `CMakeLists.txt`:
```cmake
find_package(coral REQUIRED CONFIG)
target_link_libraries(your_target PRIVATE coral::coral)
```

#### Building with vcpkg

```bash
export VCPKG_ROOT=/path/to/vcpkg

cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=../vcpkg-overlay-port

cmake --build build
./build/example_01
```
