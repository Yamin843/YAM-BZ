#!/system/bin/sh
# ═══════════════════════════════════════════════════════════
#  YAM-BZ build — Albatross-based Mod Menu
#  Pipeline:
#   1. javac: compile YAM-BZ Java with Albatross AARs on classpath
#   2. d8: merge YAM-BZ + Albatross jars → classes.dex
#   3. embed dex + albatross.so → C headers
#   4. cmake+ninja → libyambz.so
# ═══════════════════════════════════════════════════════════
set -e
cd "$(dirname "$0")/.."
ROOT="$PWD"

ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
SDK_PLATFORM="${SDK_PLATFORM:-android-34}"
BUILD_TOOLS="${BUILD_TOOLS:-35.0.0}"
ABI="${ABI:-arm64-v8a}"
MIN_SDK="${MIN_SDK:-26}"

NDK="$ANDROID_HOME/ndk/$NDK_VERSION"
ANDROID_JAR="$ANDROID_HOME/platforms/$SDK_PLATFORM/android.jar"
D8="$ANDROID_HOME/build-tools/$BUILD_TOOLS/d8"
LLVM_STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"

CORE_JAR="$ROOT/albatross-libs/aar/core.jar"
ANN_JAR="$ROOT/albatross-libs/aar/annotation.jar"
OCEAN_JAR="$ROOT/albatross-libs/aar/ocean.jar"

BUILD_DIR="$ROOT/build-ninja"

echo "═══════════════════════════════════════════════"
echo "  YAM-BZ build"
echo "═══════════════════════════════════════════════"
echo "  NDK:      $NDK_VERSION"
echo "  BuildTools: $BUILD_TOOLS"
echo "  ABI:      $ABI"
echo "  Output:   $BUILD_DIR/cmake/libyambz.so"
echo ""

die() { echo "❌ $1" >&2; exit 1; }

# Verify prerequisites
[ -e "$ANDROID_JAR" ]  || die "Missing: $ANDROID_JAR"
[ -e "$D8" ]           || die "Missing: $D8"
[ -d "$NDK" ]          || die "Missing NDK: $NDK"
[ -f "$CORE_JAR" ]     || die "Missing: $CORE_JAR"
[ -f "$ANN_JAR" ]      || die "Missing: $ANN_JAR"
[ -f "$OCEAN_JAR" ]    || die "Missing: $OCEAN_JAR"
[ -f "$ROOT/albatross-libs/jniLibs/$ABI/libalbatross_base.so" ] || \
    die "Missing libalbatross_base.so for $ABI"
[ -f "$ROOT/app/src/main/cpp/imgui/imgui.cpp" ] || \
    die "Missing ImGui — run scripts/fetch_imgui.sh first"
command -v javac >/dev/null || die "javac not found"
command -v cmake >/dev/null || die "cmake not found"
command -v ninja >/dev/null || die "ninja not found"
command -v xxd >/dev/null   || die "xxd not found"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"/{classes,dex,cmake}

# ═══ 1. Compile YAM-BZ Java ═══
echo "▶ 1/4 Compiling Java..."
JAVA_SOURCES=$(find app/src/main/java -name "*.java")
[ -n "$JAVA_SOURCES" ] || die "No Java sources found"

javac -source 17 -target 17 \
      -encoding UTF-8 \
      -Xlint:-options \
      -Xlint:-deprecation \
      -cp "$ANDROID_JAR:$CORE_JAR:$ANN_JAR:$OCEAN_JAR" \
      -d "$BUILD_DIR/classes" \
      $JAVA_SOURCES 2>&1 | tee /tmp/javac.log

if grep -q "error:" /tmp/javac.log; then
    echo "❌ Java compile failed:"
    grep -B1 -A2 "error:" /tmp/javac.log | head -40
    exit 1
fi

CLS=$(find "$BUILD_DIR/classes" -name "*.class" | wc -l)
echo "  ✅ $CLS classes"

# ═══ 2. d8 → classes.dex ═══
echo "▶ 2/4 Building dex (d8)..."
"$D8" --output "$BUILD_DIR/dex" \
      --lib "$ANDROID_JAR" \
      --min-api "$MIN_SDK" \
      --no-desugaring \
      $(find "$BUILD_DIR/classes" -name "*.class") \
      "$CORE_JAR" \
      "$ANN_JAR" \
      "$OCEAN_JAR"

[ -f "$BUILD_DIR/dex/classes.dex" ] || die "d8 produced no classes.dex"
echo "  ✅ $(stat -c %s $BUILD_DIR/dex/classes.dex) bytes"

# ═══ 3. Embed dex + albatross ═══
echo "▶ 3/4 Embedding assets..."

mkdir -p "$ROOT/app/src/main/cpp/generated"

# 3a) dex
cat > "$ROOT/app/src/main/cpp/generated/embedded_assets.h" << 'HDR'
#pragma once
#include <cstddef>
#include <cstdint>

struct YambzAsset {
    const unsigned char* data;
    size_t len;
};

// Dex (single .dex)
extern const unsigned char YAMBZ_DEX_BYTES[];
extern const size_t YAMBZ_DEX_LEN;

// Albatross .so (stripped)
extern const unsigned char YAMBZ_ALB_BYTES[];
extern const size_t YAMBZ_ALB_LEN;
HDR

{
    echo "// AUTO-GENERATED"
    echo '#include "generated/embedded_assets.h"'
    echo ""
    echo "const unsigned char YAMBZ_DEX_BYTES[] = {"
    xxd -i < "$BUILD_DIR/dex/classes.dex"
    echo "};"
    echo "const size_t YAMBZ_DEX_LEN = $(stat -c %s $BUILD_DIR/dex/classes.dex);"
    echo ""
} > "$ROOT/app/src/main/cpp/generated/embedded_assets.cpp"

# 3b) albatross (stripped copy)
ALB_SRC="$ROOT/albatross-libs/jniLibs/$ABI/libalbatross_base.so"
ALB_TMP="$BUILD_DIR/libalbatross_stripped.so"
cp "$ALB_SRC" "$ALB_TMP"
if [ -x "$LLVM_STRIP" ]; then
    "$LLVM_STRIP" --strip-all "$ALB_TMP" 2>/dev/null || true
fi
echo "  albatross: $(stat -c %s $ALB_SRC) → $(stat -c %s $ALB_TMP) bytes"

{
    echo "const unsigned char YAMBZ_ALB_BYTES[] = {"
    xxd -i < "$ALB_TMP"
    echo "};"
    echo "const size_t YAMBZ_ALB_LEN = $(stat -c %s $ALB_TMP);"
} >> "$ROOT/app/src/main/cpp/generated/embedded_assets.cpp"

echo "  ✅ dex + albatross embedded"

# ═══ 4. CMake ═══
echo "▶ 4/4 Building .so (CMake + Ninja)..."

cmake -B "$BUILD_DIR/cmake" -S "$ROOT/app/src/main/cpp" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$ABI" \
      -DANDROID_PLATFORM="android-$MIN_SDK" \
      -DANDROID_STL=c++_shared \
      -DCMAKE_BUILD_TYPE=Release \
      -DALBATROSS_INCLUDE="$ROOT/albatross-libs/include"

cmake --build "$BUILD_DIR/cmake" --config Release

SO_OUT="$BUILD_DIR/cmake/libyambz.so"
[ -f "$SO_OUT" ] || die "Build failed — no libyambz.so"

echo ""
echo "═══════════════════════════════════════════════"
echo "  ✅ BUILD SUCCESS"
echo "═══════════════════════════════════════════════"
echo "  Output: $SO_OUT"
echo "  Size:   $(stat -c %s $SO_OUT) bytes ($(du -h $SO_OUT | cut -f1))"
echo ""
