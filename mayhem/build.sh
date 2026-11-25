#!/usr/bin/env bash
#
# mayhem/build.sh — SkyFire_548 fuzz harnesses + known-answer selftest.
#
# The full upstream CMake build needs Boost >= 1.91, OpenSSL 4.0 and a MySQL client
# (none at those versions in the base image), so the fuzzed loaders are compiled
# directly from their upstream sources, which need none of them:
#   shared/DataStores/{DBC,DB2}FileLoader.cpp, shared/Debugging/Errors.cpp, shared/Configuration/Config.cpp,
#   collision/Models/{WorldModel,ModelInstance}.cpp, collision/BoundingIntervalHierarchy.cpp,
#   collision/Maps/TileAssembler.cpp (defines VMAP::readChunk) and the
#   vendored dep/g3dlite (the same source list as dep/g3dlite/CMakeLists.txt).
# Every project source is compiled with $SANITIZER_FLAGS $DEBUG_FLAGS; the selftest
# is a separate build with normal flags. Objects go to build-mayhem/ (gitignored
# upstream by `build*/`). Re-runnable and offline.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
cd "$SRC"

OUT="$SRC/build-mayhem"
# DBC/DB2 records are #pragma pack(1) structs by design (DBCStructure.h, DB2Structure.h), so
# AutoProduceData stores pointers/ints at unaligned offsets on every real file. UBSan's
# `alignment` check would fire on every input; only that check is relaxed, the rest of
# UBSan and ASan stay on and halting.
FUZZ_RELAX="-fno-sanitize=alignment"
# SanCov on the project objects too (LIB_FUZZING_ENGINE only instruments at link).
COV="-fsanitize=fuzzer-no-link"
[ -n "$SANITIZER_FLAGS" ] || FUZZ_RELAX=""
mkdir -p "$OUT/fuzz" "$OUT/test"

INC=(
  -I"$SRC/src/server/shared" -I"$SRC/src/server/shared/DataStores"
  -I"$SRC/src/server/shared/Debugging" -I"$SRC/src/server/shared/Platform"
  -I"$SRC/src/server/shared/Utilities" -I"$SRC/src/server/shared/Packets"
  -I"$SRC/src/server/shared/Logging" -I"$SRC/src/server/shared/Threading"
  -I"$SRC/src/server/shared/Configuration"
  -I"$SRC/src/server/collision" -I"$SRC/src/server/collision/Models"
  -I"$SRC/src/server/collision/Maps" -I"$SRC/src/server/collision/Management"
  -I"$SRC/src/server/game/DataStores" -I"$SRC/dep/g3dlite/include"
)
G3D_SRCS=$(sed -n 's#^ *source/\(.*\.cpp\)$#dep/g3dlite/source/\1#p' dep/g3dlite/CMakeLists.txt)
PROJ_SRCS="src/server/shared/DataStores/DBCFileLoader.cpp
src/server/shared/DataStores/DB2FileLoader.cpp
src/server/shared/Debugging/Errors.cpp
src/server/shared/Configuration/Config.cpp
src/server/collision/Models/WorldModel.cpp
src/server/collision/BoundingIntervalHierarchy.cpp
src/server/collision/Maps/TileAssembler.cpp
src/server/collision/Models/ModelInstance.cpp
$G3D_SRCS"

# compile_all <objdir> <flags...>: one object per project source, in parallel.
compile_all() {
  local dir="$1"; shift
  local flags="$*"
  printf '%s\n' $PROJ_SRCS | xargs -P "$MAYHEM_JOBS" -I{} sh -c \
    'o="$0/$(echo "$1" | tr / _).o"; '"$CXX"' -std=c++2b -w '"$flags"' '"${INC[*]}"' -c "$1" -o "$o"' "$dir" {}
}

# 1) Sanitized project objects + harnesses (fuzzer and standalone).
compile_all "$OUT/fuzz" $SANITIZER_FLAGS $FUZZ_RELAX $COV $DEBUG_FLAGS
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/fuzz/standalone_main.o"
PROJ_OBJS=$(ls "$OUT"/fuzz/src_*.o "$OUT"/fuzz/dep_*.o)
for h in fuzz_dbc fuzz_db2 fuzz_vmap_model fuzz_vmap_assembler fuzz_config; do
  $CXX -std=c++2b $SANITIZER_FLAGS $FUZZ_RELAX $COV $DEBUG_FLAGS "${INC[@]}" -c "$SRC/mayhem/harnesses/$h.cpp" -o "$OUT/fuzz/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/fuzz/$h.o" $PROJ_OBJS \
      "$OUT/fuzz/lsan_off.o" -lz -lpthread -o "$OUT/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/fuzz/$h.o" "$OUT/fuzz/standalone_main.o" $PROJ_OBJS \
      "$OUT/fuzz/lsan_off.o" -lz -lpthread -o "$OUT/$h-standalone"
done

# 2) Selftest (oracle for mayhem/test.sh) with normal flags.
compile_all "$OUT/test" -O1 $COVERAGE_FLAGS
$CXX -std=c++2b -O1 $COVERAGE_FLAGS "${INC[@]}" "$SRC/mayhem/tests/selftest.cpp" \
    $(ls "$OUT"/test/src_*.o "$OUT"/test/dep_*.o) -lz -lpthread -o "$OUT/selftest"
echo "build.sh: built $(ls "$OUT"/fuzz_* | wc -l) fuzz binaries and selftest in $OUT"
