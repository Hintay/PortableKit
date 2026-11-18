# Cross-compiles a port for x86-64 Linux (a Steam Deck, say) on another
# machine, with zig as the C and C++ compiler:
#
#   cmake -S <port> -B <build> -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<portablekit>/cmake/toolchains/zig-linux-x86_64.cmake \
#         -DPORTABLEKIT_LINUX_SYSROOT=<sysroot>
#
# zig brings its own glibc stubs and libc++, so the build needs nothing of the
# target but what the port links against: SDL3 and the Vulkan loader, in a
# sysroot of <sysroot>/usr/lib (the target's libSDL3.so.0 and libvulkan.so.1,
# with cmake/SDL3/SDL3Config.cmake) and <sysroot>/usr/include (SDL3, vulkan
# and vk_video headers). FFmpeg is built with the project as usual.
#
# PORTABLEKIT_ZIG_TARGET (default x86_64-linux-gnu.2.35, the oldest glibc the
# executable may meet; zig carries only current glibc headers, so an older
# version makes FFmpeg's configure find functions whose headers are gone) and PORTABLEKIT_ZIG_CPU (default x86_64_v3, which the
# Steam Deck's Zen 2 has) are read from the environment by the wrappers.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_zig_dir "${CMAKE_CURRENT_LIST_DIR}/zig")
set(CMAKE_C_COMPILER "${_zig_dir}/cc")
set(CMAKE_CXX_COMPILER "${_zig_dir}/c++")
set(CMAKE_AR "${_zig_dir}/ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_zig_dir}/ranlib" CACHE FILEPATH "")

set(PORTABLEKIT_LINUX_SYSROOT "$ENV{PORTABLEKIT_LINUX_SYSROOT}" CACHE PATH
    "The target's libraries and headers to link against (see this file)")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PORTABLEKIT_LINUX_SYSROOT)
if(PORTABLEKIT_LINUX_SYSROOT)
    set(CMAKE_FIND_ROOT_PATH "${PORTABLEKIT_LINUX_SYSROOT}")
endif()
# The target's own libraries were built against its glibc, which may be newer
# than the one the executable is built for; they find their symbols in it
# when they run, so the link leaves them to it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined")

# Programs (glslang, Python, make) are the build machine's; libraries,
# headers and packages are the target's.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
