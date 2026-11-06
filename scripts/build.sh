#!/system/bin/sh
# ═══════════════════════════════════════════════════════════
#  YAM-BZ pure .so build (AAR mode)
#  Compiles YAM-BZ Java, merges Albatross AAR jars, produces dex,
#  embeds dex + albatross.so, builds libyambz.so
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

CORE_JAR="$ROOT/albatross-libs/aar/core.jar"
OCEAN_JAR="$ROOT/albatross-libs/aar/ocean.jar"
ANN_JAR="$ROOT/albatross-libs/aar/annotation.jar"

BUILD_DIR="$ROOT/build-ninja"

echo "═══════════════════════════════════════════════"
echo "  YAM-BZ build (AAR mode)"
echo "═══════════════════════════════════════════════"
echo "  ANDROID_HOME:  $ANDROID_HOME"
echo "  NDK:           $NDK_VERSION"
echo "  ABI:           $ABI"
echo "  core.jar:      $(stat -c %s $CORE_JAR 2>/dev/null || echo MISSING) bytes"
echo "  Output:        $BUILD_DIR/cmake/libyambz.so"
echo ""

die() { echo "❌ $1" >&2; exit 1; }

[ -e "$ANDROID_JAR" ]  || die "Missing: $ANDROID_JAR"
[ -e "$D8" ]           || die "Missing: $D8"
[ -d "$NDK" ]          || die "Missing NDK: $NDK"
[ -f "$CORE_JAR" ]     || die "Missing: $CORE_JAR"
[ -f "$OCEAN_JAR" ]    || die "Missing: $OCEAN_JAR"
[ -f "$ANN_JAR" ]      || die "Missing: $ANN_JAR"
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
mkdir -p "$ROOT/app/src/main/cpp/yambz/embed"

# ═══ 1. Compile YAM-BZ Java only (Albatross from jars) ═══
echo "▶ 1/5 Compiling YAM-BZ Java..."

JAVA_SOURCES=$(find app/src/main/java/com/yambz -name "*.java")
[ -n "$JAVA_SOURCES" ] || die "No YAM-BZ Java sources"

javac -source 17 -target 17 \
      -encoding UTF-8 \
      -Xlint:-options \
      -Xlint:-deprecation \
      -cp "$ANDROID_JAR:$CORE_JAR:$ANN_JAR:$OCEAN_JAR" \
      -d "$BUILD_DIR/classes" \
      $JAVA_SOURCES 2>&1 | tee /tmp/javac.log

if grep -q "error:" /tmp/javac.log; then
    echo ""
    echo "❌ Java compilation failed. Errors:"
    grep -B1 -A2 "error:" /tmp/javac.log | head -40
    exit 1
fi

YAMBZ_CLASS_COUNT=$(find "$BUILD_DIR/classes" -name "*.class" | wc -l)
echo "  ✅ $YAMBZ_CLASS_COUNT YAM-BZ classes"

# ═══ 2. D8 → dex (YAM-BZ + Albatross jars merged) ═══
echo "▶ 2/5 Building dex (d8)..."
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
ALB_SO_SRC="$ROOT/albatross-libs/jniLibs/$ABI/libalbatross_base.so"
ALB_SO="$BUILD_DIR/stripped_libalbatross.so"

# Strip debug symbols from albatross before embedding
# This saves ~2MB (albatross ships with debug info)
LLVM_STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
if [ -x "$LLVM_STRIP" ]; then
    cp "$ALB_SO_SRC" "$ALB_SO"
    "$LLVM_STRIP" --strip-all "$ALB_SO" 2>/dev/null || \
    "$LLVM_STRIP" "$ALB_SO" 2>/dev/null || true
    echo "  stripped: $(stat -c %s "$ALB_SO_SRC") → $(stat -c %s "$ALB_SO") bytes"
else
    cp "$ALB_SO_SRC" "$ALB_SO"
    echo "  (llvm-strip not found — no strip)"
fi
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
echo "  ✅ albatross embedded ($(stat -c %s $ALB_SO) bytes)"

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
