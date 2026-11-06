#pragma once
#include <string>
#include <vector>

namespace yambz {
namespace ClassesTab_jni {
void startEnum();
bool isEnumerating();
bool isComplete();
int  getTotal();
std::vector<std::string> nextBatch(int n);
std::vector<std::string> getMembers(const std::string& className);
}
}
