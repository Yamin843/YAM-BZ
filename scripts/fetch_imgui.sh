#!/system/bin/sh
# ImGui — آخر إصدار + تصفية للملفات الجوهرية فقط
set -e
cd "$(dirname "$0")/.."

DEST="app/src/main/cpp/imgui"

# ─── آخر إصدار عبر GitHub API ───
echo "🔍 Querying latest ImGui..."
LATEST=$(curl -sSL "https://api.github.com/repos/ocornut/imgui/releases/latest" \
         | grep -o '"tag_name": *"[^"]*"' | head -1 | cut -d'"' -f4)
if [ -z "$LATEST" ]; then
    echo "⚠️  API rate-limited — fallback to v1.92.9b"
    LATEST="v1.92.9b"
fi
echo "📌 Latest: $LATEST"

# idempotent
if [ -f "$DEST/.version" ]; then
    CURRENT=$(cat "$DEST/.version")
    if [ "$CURRENT" = "$LATEST" ] && [ -f "$DEST/imgui.cpp" ]; then
        echo "✅ ImGui $LATEST already present"
        exit 0
    fi
fi

rm -rf "$DEST"
mkdir -p "$DEST"
cd "$DEST"

echo "📥 Downloading $LATEST..."
curl -sSL -o imgui.tar.gz \
    "https://github.com/ocornut/imgui/archive/refs/tags/${LATEST}.tar.gz"

if ! gzip -t imgui.tar.gz 2>/dev/null; then
    echo "❌ Not valid gzip"
    rm -f imgui.tar.gz
    exit 1
fi

echo "📦 Extracting..."
tar -xzf imgui.tar.gz --strip-components=1
rm imgui.tar.gz

# ─── تصفية: احذف كل ما لا نحتاجه ───
rm -rf examples/ docs/ misc/ fonts/
rm -f  imgui_demo.cpp
# كل backends عدا opengl3
find backends/ -type f ! -name 'imgui_impl_opengl3.*' -delete

echo "$LATEST" > .version

# ─── تحقق ───
echo ""
echo "✅ ImGui $LATEST ready (filtered)"
echo "الملفات:"
ls -1 *.cpp *.h backends/*.cpp backends/*.h 2>/dev/null
echo ""
echo "الحجم: $(du -sh . | cut -f1)"
