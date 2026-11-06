#pragma once
#include <cstdint>
namespace yambz {
class TraceDialog {
public:
    static void draw();
    /// فتح حوار trace method (يُستدعى من ClassesTab)
    static void openForMethod(class ElementRef*);
};
}
