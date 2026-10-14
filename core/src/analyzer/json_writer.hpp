
#pragma once

#include <string>

namespace safebox {
namespace analyzer {

std::string escapeJson(const std::string& value);
void writeJsonLine(const std::string& record);

}}