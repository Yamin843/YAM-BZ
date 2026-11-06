#!/system/bin/sh
set -e
cd "$(dirname "$0")/.."

# قراءة ABI من gradle.properties
TARGET_ABI=$(grep "^yambz.targetAbi=" gradle.properties | cut -d= -f2 | tr -d ' ')
if [ -z "$TARGET_ABI" ]; then
    TARGET_ABI="arm64-v8a"
fi
echo "🎯 Target ABI: $TARGET_ABI"

APK_DIR="app/build/outputs/apk/release"
APK=$(find "$APK_DIR" -name "*.apk" | head -1)
if [ -z "$APK" ]; then
    echo "❌ No APK — run './gradlew :app:assembleRelease' first"
    exit 1
fi
echo "📦 APK: $APK"

EMBED_DIR="app/src/main/cpp/yambz/embed"
mkdir -p "$EMBED_DIR"

TMPD="$EMBED_DIR/.tmp"
rm -rf "$TMPD"
mkdir -p "$TMPD"

# ─── dex ───
unzip -o "$APK" "classes*.dex" -d "$TMPD/" >/dev/null
DEX_COUNT=$(ls "$TMPD"/classes*.dex 2>/dev/null | wc -l)
echo "📄 $DEX_COUNT dex file(s)"

# header
{
    echo "// AUTO-GENERATED"
    echo "#pragma once"
    echo "#include <cstddef>"
    echo "#include <cstdint>"
    echo ""
    IDX=0
    for DEX in "$TMPD"/classes*.dex; do
        echo "extern const unsigned char YAMBZ_DEX${IDX}[];"
        echo "extern const size_t YAMBZ_DEX${IDX}_LEN;"
        IDX=$((IDX+1))
    done
    echo ""
    echo "struct YambzDexEntry { const unsigned char* data; size_t len; };"
    echo "extern const YambzDexEntry YAMBZ_DEX_TABLE[];"
    echo "extern const size_t YAMBZ_DEX_TABLE_LEN;"
} > "$EMBED_DIR/embedded_dex.h"

# cpp
{
    echo "// AUTO-GENERATED"
    echo '#include "embedded_dex.h"'
    echo ""
    IDX=0
    for DEX in "$TMPD"/classes*.dex; do
        echo "const unsigned char YAMBZ_DEX${IDX}[] = {"
        xxd -i < "$DEX"
        echo "};"
        echo "const size_t YAMBZ_DEX${IDX}_LEN = $(stat -c %s "$DEX");"
        echo ""
        IDX=$((IDX+1))
    done
    echo "const YambzDexEntry YAMBZ_DEX_TABLE[] = {"
    IDX=0
    for DEX in "$TMPD"/classes*.dex; do
        echo "    { YAMBZ_DEX${IDX}, YAMBZ_DEX${IDX}_LEN },"
        IDX=$((IDX+1))
    done
    echo "};"
    echo "const size_t YAMBZ_DEX_TABLE_LEN = ${DEX_COUNT};"
} > "$EMBED_DIR/embedded_dex.cpp"

# ─── libalbatross_base.so (ABI واحد فقط) ───
SO="albatross-libs/jniLibs/$TARGET_ABI/libalbatross_base.so"
if [ ! -f "$SO" ]; then
    echo "❌ Missing: $SO"
    exit 1
fi
SO_SIZE=$(stat -c %s "$SO")
echo "📦 libalbatross_base.so ($TARGET_ABI): $SO_SIZE bytes"

{
    echo "// AUTO-GENERATED"
    echo "#pragma once"
    echo "#include <cstddef>"
    echo "extern const unsigned char YAMBZ_ALB[];"
    echo "extern const size_t YAMBZ_ALB_LEN;"
} > "$EMBED_DIR/embedded_albatross.h"

{
    echo "// AUTO-GENERATED"
    echo '#include "embedded_albatross.h"'
    echo "const unsigned char YAMBZ_ALB[] = {"
    xxd -i < "$SO"
    echo "};"
    echo "const size_t YAMBZ_ALB_LEN = ${SO_SIZE};"
} > "$EMBED_DIR/embedded_albatross.cpp"

rm -rf "$TMPD"

echo ""
echo "✅ Embedded for $TARGET_ABI:"
ls -lh "$EMBED_DIR"/*.h "$EMBED_DIR"/*.cpp
