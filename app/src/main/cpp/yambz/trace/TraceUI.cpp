#include "TraceUI.h"
#include "core/Helpers.h"
namespace yambz {
TraceUIOptions& traceUI() {
    static TraceUIOptions s;
    return s;
}
} // namespace yambz
