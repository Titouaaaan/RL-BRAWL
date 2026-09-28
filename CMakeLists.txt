# CMake doesn't compile anything itself. It generates a project for the real
# build tool (here a Visual Studio solution driving MSVC, Microsoft's C++
# compiler) and describes "targets": things to build (an .exe, a Python
# module) plus the source files and flags each one uses.
cmake_minimum_required(VERSION 3.26)
project(fightcore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_SOURCES core/sim.cpp core/moves.cpp)

# scikit-build-core (driven by `pip install`) sets SKBUILD. In that case we
# build the Python module. Otherwise, in a plain CMake build, we build the
# native smoke test with AddressSanitizer.
if(SKBUILD)
    find_package(Python 3.9 COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    nanobind_add_module(fightcore NB_STATIC bindings/python/module.cpp ${CORE_SOURCES})
    install(TARGETS fightcore LIBRARY DESTINATION .)
else()
    add_executable(smoke core/smoke.cpp ${CORE_SOURCES})
    if(MSVC)
        # AddressSanitizer (ASan) adds checks around every memory access, so an
        # out-of-bounds write crashes at the exact line instead of silently
        # corrupting memory. It only applies to Debug builds (it is slow).
        target_compile_options(smoke PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
        # Incremental linking (faster relinks) doesn't work with ASan.
        target_link_options(smoke PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL:NO>)
        # MSVC's Debug defaults include /RTC1 (runtime checks), which can't be
        # combined with ASan, so we remove it.
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        # An ASan program needs the ASan runtime DLL at startup. Windows looks
        # for DLLs next to the .exe first, so we copy it there after each build.
        get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB ASAN_DLLS "${MSVC_BIN_DIR}/clang_rt.asan*dynamic-x86_64.dll")
        add_custom_command(TARGET smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASAN_DLLS} $<TARGET_FILE_DIR:smoke>)
    endif()
endif()
