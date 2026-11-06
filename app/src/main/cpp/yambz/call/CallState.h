#pragma once
#include "ParamNode.h"
#include <jni.h>
#include <string>
#include <vector>
#include <memory>

namespace yambz {

/// يمثّل method اختارها المستخدم للاستدعاء
struct CallTarget {
    jobject     method = nullptr;    // GlobalRef
    std::string methodDesc;
    std::string returnType;
    std::vector<std::string> argTypes;
    bool        isStatic = false;
    std::string declaringClass;
};

struct CallUIState {
    bool     show = false;
    CallTarget target;

    // Parameters (this + args)
    std::shared_ptr<ParamNode> thisNode;
    std::vector<std::shared_ptr<ParamNode>> args;

    // Result
    bool     showingResult = false;
    std::string lastResultJson;

    // Flags
    bool     invoking = false;
};

CallUIState& callUI();

void openCallForMethod(JNIEnv* env, jobject method);

} // namespace yambz
