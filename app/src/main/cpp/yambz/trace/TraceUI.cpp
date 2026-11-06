#include "TraceUI.h"
namespace yambz {
TraceUIOptions& traceUI() {
    static TraceUIOptions s;
    return s;
}
} // namespace yambz
