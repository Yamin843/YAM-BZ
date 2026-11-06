#include "ClassesState.h"
#include "core/Helpers.h"
namespace yambz {
ClassesState& ClassesState::instance() {
    static ClassesState s;
    return s;
}
} // namespace yambz
