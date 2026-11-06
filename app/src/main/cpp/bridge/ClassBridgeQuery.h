#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <utility>

namespace yambz {
namespace ClassBridgeQuery {

    /// Returns [(fieldName, fieldType)] for a class
    std::vector<std::pair<std::string, std::string>>
    getFieldSchema(const std::string& className);

    /// Returns [(methodName, argsString, returnType)] for a class
    std::vector<std::tuple<std::string, std::string, std::string>>
    getMethodSchema(const std::string& className);

} // namespace ClassBridgeQuery
} // namespace yambz
