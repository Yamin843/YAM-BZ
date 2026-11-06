#!/system/bin/sh
set -e
cd "$(dirname "$0")/.."

echo "═══════════════════════════════════════════"
echo "  Pre-push verification"
echo "═══════════════════════════════════════════"

EXIT=0

echo ""
echo "▶ Brace balance:"
for f in $(find app/src/main -name "*.java" -o -name "*.cpp" -o -name "*.h" 2>/dev/null); do
    if [ "$(basename $(dirname "$f"))" = "imgui" ]; then continue; fi
    O=$(grep -o "{" "$f" 2>/dev/null | wc -l)
    C=$(grep -o "}" "$f" 2>/dev/null | wc -l)
    if [ "$O" != "$C" ]; then
        echo "  ❌ $(basename $f): { $O } $C"
        EXIT=1
    fi
done
[ $EXIT -eq 0 ] && echo "  ✅ all balanced"

echo ""
echo "▶ Albatross AARs present:"
for f in albatross-libs/aar/core.jar albatross-libs/aar/annotation.jar albatross-libs/aar/ocean.jar; do
    [ -f "$f" ] && echo "  ✅ $f" || { echo "  ❌ $f MISSING"; EXIT=1; }
done

echo ""
echo "▶ File counts:"
echo "  Java:  $(find app/src/main/java -name '*.java' | wc -l)"
echo "  C++:   $(find app/src/main/cpp -maxdepth 2 -name '*.cpp' -not -path '*/imgui/*' | wc -l)"
echo "  Headers: $(find app/src/main/cpp -maxdepth 2 -name '*.h' -not -path '*/imgui/*' | wc -l)"

echo ""
if [ $EXIT -eq 0 ]; then
    echo "═══════════════════════════════════════════"
    echo "  ✅ READY TO PUSH"
else
    echo "═══════════════════════════════════════════"
    echo "  ❌ FIX ISSUES FIRST"
fi
exit $EXIT
