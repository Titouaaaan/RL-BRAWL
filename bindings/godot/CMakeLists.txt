# Builds the Godot extension DLL into godot/bin/. It's a separate CMake
# project from the root one so the Python build never has to compile
# godot-cpp. Run it through play.bat, or by hand:
#   python -m cmake -S bindings/godot -B build/godot
#   python -m cmake --build build/godot --config Release
cmake_minimum_required(VERSION 3.26)
project(fightgame LANGUAGES CXX)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# godot-cpp normally generates C++ wrappers for all ~1000 Godot classes, which
# is slow to compile. A build profile limits that to the classes we use.
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE PATH "")

# Pull in godot-cpp's own CMake project. It defines the `godot-cpp` target,
# a static library (compiled code that gets copied into our DLL at link time).
add_subdirectory("${REPO_ROOT}/extern/godot-cpp" godot-cpp)

add_library(fightgame SHARED fightgame.cpp "${REPO_ROOT}/core/sim.cpp" "${REPO_ROOT}/core/moves.cpp"
            "${REPO_ROOT}/core/obs.cpp")
target_link_libraries(fightgame PRIVATE godot-cpp)

# The file name has to match godot/fightgame.gdextension, e.g.
# fightgame.windows.template_debug.x86_64.dll. godot-cpp computes that suffix.
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set_target_properties(fightgame PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME "fightgame${GODOTCPP_SUFFIX}"
    # $<1:...> stops Visual Studio from adding a Release/ subfolder.
    RUNTIME_OUTPUT_DIRECTORY "$<1:${REPO_ROOT}/godot/bin>"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${REPO_ROOT}/godot/bin>"
    PDB_OUTPUT_DIRECTORY "$<1:${REPO_ROOT}/godot/bin>"
)
