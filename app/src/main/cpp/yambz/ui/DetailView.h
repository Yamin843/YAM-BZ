#pragma once
#include <cstdint>
#include <string>
namespace yambz {

struct DetailViewState {
    bool show = false;
    uint32_t classIdx = UINT32_MAX;
};

DetailViewState& detailViewState();
void drawDetailView();

} // namespace yambz
