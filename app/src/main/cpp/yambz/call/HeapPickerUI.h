#pragma once
#include <cstdint>
namespace yambz {

struct HeapPickerUIState {
    bool   show = false;
    char   searchType[256] = {0};
    int    selectedIdx = -1;
    int    scrollOffset = 0;
    int    lastTotal = 0;
};

HeapPickerUIState& heapPickerUI();
void drawHeapPicker();

} // namespace yambz
