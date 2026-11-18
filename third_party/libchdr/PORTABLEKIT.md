# libchdr in PortableKit

An unmodified copy of [libchdr](https://github.com/rtissera/libchdr) at commit
`47d794151b2b64f6aba60304e6480f4f5a2bc24b`, for reading disc images in CHD
format (host/kernel/disc_reader.cpp). Only the library's sources are kept:
`include/`, `src/` and the bundled decoders in `deps/` (LZMA SDK 26.02,
miniz 3.1.2, the single-file Zstandard 1.5.7 decompressor), without their
assembly, build files, tests or the optional micro-flac backend. PortableKit
builds it with its own target (portablekit_chdr in cmake/PortableKit.cmake).

Licences: libchdr BSD-3-Clause (LICENSE.txt); the bundled libraries' notices are gathered in NOTICES.txt for release packaging; LZMA SDK public domain
(deps/lzma-26.02/LICENSE); miniz MIT (notice in miniz.c); Zstandard BSD
(notice in zstddeclib.c); dr_flac public domain or MIT-0 (notice in
include/dr_libs/dr_flac.h).
