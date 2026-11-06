#!/system/bin/sh
# فحص بنية الملفات قبل push (بدون الحاجة لـ JDK)
set -e
cd "$(dirname "$0")/.."

echo "═══════════════════════════════════════════════"
echo "  Pre-push verify (no JDK needed)"
echo "═══════════════════════════════════════════════"

EXIT=0

# 1) brace balance
echo ""
echo "▶ Brace balance:"
for f in $(find app/src/main/java -name "*.java"); do
    O=$(grep -o "{" "$f" | wc -l)
    C=$(grep -o "}" "$f" | wc -l)
    if [ "$O" != "$C" ]; then
        echo "  ❌ $f: { $O != } $C"
        EXIT=1
    fi
done
[ $EXIT -eq 0 ] && echo "  ✅ All balanced"

# 2) orphan default
echo ""
echo "▶ Orphan default:"
for f in $(find app/src/main/java -name "*.java"); do
    S=$(grep -c "switch (" "$f" || echo 0)
    D=$(grep -c "default:" "$f" || echo 0)
    if [ "$D" -gt "$S" ]; then
        echo "  ❌ $f: switch=$S default=$D"
        EXIT=1
    fi
done
[ $EXIT -eq 0 ] && echo "  ✅ All good"

# 3) No fldName orphan
echo ""
echo "▶ No orphan fldName:"
BAD=$(grep -rln "fldName" app/src/main/java 2>/dev/null || true)
if [ -n "$BAD" ]; then
    echo "  ❌ $BAD"
    EXIT=1
else
    echo "  ✅ All good"
fi

# 4) Critical Albatross source intact
echo ""
echo "▶ Albatross source:"
[ -f app/src/main/java/qing/albatross/core/Albatross.java ] && echo "  ✅" || { echo "  ❌"; EXIT=1; }
[ -f app/src/main/java/qing/albatross/nativehook/AlbNative.java ] && echo "  ✅ nativehook" || { echo "  ❌ nativehook"; EXIT=1; }
[ -f app/src/main/java/qing/albatross/search/SearchClassCallback.java ] && echo "  ✅ search" || { echo "  ❌ search"; EXIT=1; }

# 5) build.sh no AAR references
echo ""
echo "▶ No AAR refs in build.sh:"
grep "core.jar\|annotation.jar\|\.aar" scripts/build.sh >/dev/null 2>&1 && \
    { echo "  ❌ still references"; EXIT=1; } || echo "  ✅ clean"

echo ""
echo "═══════════════════════════════════════════════"
if [ $EXIT -eq 0 ]; then
    echo "  ✅ READY TO PUSH"
else
    echo "  ❌ FIX ISSUES FIRST"
fi
exit $EXIT
