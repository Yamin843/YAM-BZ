#!/system/bin/sh
# ═══════════════════════════════════════════════════════════
#  YAM-BZ pure .so build
#  Compiles Albatross + YAM-BZ Java together from source.
#  No AAR, no Gradle, no prebuilt jars.
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

BUILD_DIR="$ROOT/build-ninja"

echo "═══════════════════════════════════════════════"
echo "  YAM-BZ build"
echo "═══════════════════════════════════════════════"
echo "  ANDROID_HOME:  $ANDROID_HOME"
echo "  NDK:           $NDK_VERSION"
echo "  ABI:           $ABI"
echo "  Output:        $BUILD_DIR/cmake/libyambz.so"
echo ""

die() { echo "❌ $1" >&2; exit 1; }

[ -e "$ANDROID_JAR" ]  || die "Missing: $ANDROID_JAR"
[ -e "$D8" ]           || die "Missing: $D8"
[ -d "$NDK" ]          || die "Missing NDK: $NDK"
[ -f "$ROOT/albatross-libs/jniLibs/$ABI/libalbatross_base.so" ] || \
    die "Missing libalbatross_base.so for $ABI"
[ -f "$ROOT/app/src/main/cpp/imgui/imgui.cpp" ] || \
    die "Missing ImGui — run scripts/fetch_imgui.sh first"
[ -d "$ROOT/app/src/main/java/qing/albatross/core" ] || \
    die "Missing Albatross source — check app/src/main/java/qing/albatross/"
command -v javac >/dev/null || die "javac not found"
command -v cmake >/dev/null || die "cmake not found"
command -v ninja >/dev/null || die "ninja not found"
command -v xxd >/dev/null   || die "xxd not found"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"/{classes,dex,cmake}
mkdir -p "$ROOT/app/src/main/cpp/yambz/embed"

# ═══ 1. Compile ALL Java (YAM-BZ + Albatross source) ═══
echo "▶ 1/5 Compiling Java (YAM-BZ + Albatross)..."

# Find all .java files (no external classpath — everything from source)
JAVA_SOURCES=$(find app/src/main/java -name "*.java")
[ -n "$JAVA_SOURCES" ] || die "No Java sources"

javac -source 17 -target 17 \
      -encoding UTF-8 \
      -Xlint:-options \
      -Xlint:-deprecation \
      -cp "$ANDROID_JAR:$ROOT/albatross-libs/aar/core.jar:$ROOT/albatross-libs/aar/annotation.jar" \
      -d "$BUILD_DIR/classes" \
      $JAVA_SOURCES 2>&1 | tee /tmp/javac.log

# Check for real errors (skip "uses or overrides a deprecated API")
if grep -q "error:" /tmp/javac.log; then
    echo ""
    echo "❌ Java compilation failed. First 20 errors:"
    grep -A1 "error:" /tmp/javac.log | head -40
    exit 1
fi

CLASS_COUNT=$(find "$BUILD_DIR/classes" -name "*.class" | wc -l)
echo "  ✅ $CLASS_COUNT classes"
[ "$CLASS_COUNT" -ge 100 ] || die "Only $CLASS_COUNT classes — Albatross source missing?"

# ═══ 2. D8 → dex (all classes from source) ═══
echo "▶ 2/5 Building dex (d8)..."
"$D8" --output "$BUILD_DIR/dex" \
     --lib "$ANDROID_JAR" \
     --min-api "$MIN_SDK" \
     --no-desugaring \
     $(find "$BUILD_DIR/classes" -name "*.class") \
     "$ROOT/albatross-libs/aar/core.jar" \
     "$ROOT/albatross-libs/aar/annotation.jar"
[ -f "$BUILD_DIR/dex/classes.dex" ] || die "d8 produced no classes.dex"
echo "  ✅ $(stat -c %s $BUILD_DIR/dex/classes.dex) bytes"

# ═══ 3. Embed dex ═══
echo "▶ 3/5 Embedding dex..."
cat > "$ROOT/app/src/main/cpp/yambz/embed/embedded_dex.h" << 'HDR'
#pragma once
#include <cstddef>
extern const unsigned char YAMBZ_DEX0[];
extern const size_t YAMBZ_DEX0_LEN;
struct YambzDexEntry { const unsigned char* data; size_t len; };
extern const YambzDexEntry YAMBZ_DEX_TABLE[];
extern const size_t YAMBZ_DEX_TABLE_LEN;
HDR

{
    echo '#include "embedded_dex.h"'
    echo "const unsigned char YAMBZ_DEX0[] = {"
    xxd -i < "$BUILD_DIR/dex/classes.dex"
    echo "};"
    echo "const size_t YAMBZ_DEX0_LEN = $(stat -c %s $BUILD_DIR/dex/classes.dex);"
    echo "const YambzDexEntry YAMBZ_DEX_TABLE[] = {{YAMBZ_DEX0, YAMBZ_DEX0_LEN}};"
    echo "const size_t YAMBZ_DEX_TABLE_LEN = 1;"
} > "$ROOT/app/src/main/cpp/yambz/embed/embedded_dex.cpp"
echo "  ✅ dex embedded"

# ═══ 4. Embed albatross .so ═══
echo "▶ 4/5 Embedding libalbatross_base.so..."
ALB_SO="$ROOT/albatross-libs/jniLibs/$ABI/libalbatross_base.so"
cat > "$ROOT/app/src/main/cpp/yambz/embed/embedded_albatross.h" << 'HDR'
#pragma once
#include <cstddef>
extern const unsigned char YAMBZ_ALB[];
extern const size_t YAMBZ_ALB_LEN;
HDR

{
    echo '#include "embedded_albatross.h"'
    echo "const unsigned char YAMBZ_ALB[] = {"
    xxd -i < "$ALB_SO"
    echo "};"
    echo "const size_t YAMBZ_ALB_LEN = $(stat -c %s $ALB_SO);"
} > "$ROOT/app/src/main/cpp/yambz/embed/embedded_albatross.cpp"
echo "  ✅ albatross.so embedded ($(stat -c %s $ALB_SO) bytes)"

# ═══ 5. CMake ═══
echo "▶ 5/5 Building .so (CMake + Ninja)..."
cmake -B "$BUILD_DIR/cmake" -S "$ROOT/app/src/main/cpp" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$ABI" \
      -DANDROID_PLATFORM="android-$MIN_SDK" \
      -DANDROID_STL=c++_shared \
      -DCMAKE_BUILD_TYPE=Release \
      -DALBATROSS_INCLUDE="$ROOT/albatross-libs/include"

cmake --build "$BUILD_DIR/cmake" --config Release

SO_OUT="$BUILD_DIR/cmake/libyambz.so"
[ -f "$SO_OUT" ] || die "Build failed — no libyambz.so produced"

echo ""
echo "═══════════════════════════════════════════════"
echo "  ✅ BUILD SUCCESS"
echo "═══════════════════════════════════════════════"
echo "  Output: $SO_OUT"
echo "  Size:   $(stat -c %s $SO_OUT) bytes"
echo "  ABI:    $ABI"
echo ""
