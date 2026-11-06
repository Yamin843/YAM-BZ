#!/system/bin/sh
set -e
cd "$(dirname "$0")/.."

EMBED="app/src/main/cpp/yambz/embed"
mkdir -p "$EMBED"

# embedded_dex stubs
cat > "$EMBED/embedded_dex.h" << 'HDR'
#pragma once
#include <cstddef>
extern const unsigned char YAMBZ_DEX0[];
extern const size_t YAMBZ_DEX0_LEN;
struct YambzDexEntry { const unsigned char* data; size_t len; };
extern const YambzDexEntry YAMBZ_DEX_TABLE[];
extern const size_t YAMBZ_DEX_TABLE_LEN;
HDR

cat > "$EMBED/embedded_dex.cpp" << 'SRC'
#include "embedded_dex.h"
const unsigned char YAMBZ_DEX0[] = {0};
const size_t YAMBZ_DEX0_LEN = 0;
const YambzDexEntry YAMBZ_DEX_TABLE[] = {{YAMBZ_DEX0, 0}};
const size_t YAMBZ_DEX_TABLE_LEN = 0;
SRC

# embedded_albatross stubs
cat > "$EMBED/embedded_albatross.h" << 'HDR'
#pragma once
#include <cstddef>
extern const unsigned char YAMBZ_ALB[];
extern const size_t YAMBZ_ALB_LEN;
HDR

cat > "$EMBED/embedded_albatross.cpp" << 'SRC'
#include "embedded_albatross.h"
const unsigned char YAMBZ_ALB[] = {0};
const size_t YAMBZ_ALB_LEN = 0;
SRC

echo "✅ Stubs generated (empty — for pass 1)"
