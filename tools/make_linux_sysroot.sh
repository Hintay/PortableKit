#!/usr/bin/env bash
# Builds the sysroot cmake/toolchains/zig-linux-x86_64.cmake links against:
# the target's own SDL3 and Vulkan loader, fetched over SSH, with headers
# from this machine's SDL3 and Vulkan-Headers.
#
#   tools/make_linux_sysroot.sh HOST SYSROOT [SDL3_INCLUDE] [VULKAN_INCLUDE]
#
# HOST is anything ssh and scp take, such as user@host. The include
# directories default to Homebrew's. SDL3's headers should be the target's
# release or older: a function newer than the target's library fails to
# link, which is the point of linking against the real one.
set -euo pipefail

host="${1:?HOST}"
root="${2:?SYSROOT}"
sdl_include="${3:-$(brew --prefix sdl3 2>/dev/null)/include}"
vulkan_include="${4:-$(brew --prefix vulkan-headers 2>/dev/null)/include}"

# The real files behind the target's sonames.
real() { ssh "$host" "readlink -f '$1'"; }
sdl="$(real /usr/lib/libSDL3.so.0)"
vulkan="$(real /usr/lib/libvulkan.so.1)"

mkdir -p "$root/usr/lib/cmake/SDL3" "$root/usr/include"
scp -q "$host:$sdl" "$root/usr/lib/libSDL3.so.0"
scp -q "$host:$vulkan" "$root/usr/lib/libvulkan.so.1"
ln -sf libSDL3.so.0 "$root/usr/lib/libSDL3.so"
ln -sf libvulkan.so.1 "$root/usr/lib/libvulkan.so"
rm -rf "$root/usr/include/SDL3" "$root/usr/include/vulkan" "$root/usr/include/vk_video"
cp -R "$sdl_include/SDL3" "$root/usr/include/"
cp -R "$vulkan_include/vulkan" "$vulkan_include/vk_video" "$root/usr/include/"

cat > "$root/usr/lib/cmake/SDL3/SDL3Config.cmake" <<'EOF'
# The SDL3 of the target system, for linking only: its library copied from
# the target, its headers from a matching release.
get_filename_component(_sdl3_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(SDL3_FOUND TRUE)
if(NOT TARGET SDL3::SDL3-shared)
    add_library(SDL3::SDL3-shared SHARED IMPORTED)
    set_target_properties(SDL3::SDL3-shared PROPERTIES
        IMPORTED_LOCATION "${_sdl3_root}/lib/libSDL3.so.0"
        IMPORTED_SONAME "libSDL3.so.0"
        INTERFACE_INCLUDE_DIRECTORIES "${_sdl3_root}/include")
    add_library(SDL3::SDL3 INTERFACE IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES INTERFACE_LINK_LIBRARIES SDL3::SDL3-shared)
endif()
EOF

echo "SDL3:   $sdl"
echo "Vulkan: $vulkan"
echo "Configure with -DPORTABLEKIT_LINUX_SYSROOT=$root/usr"
