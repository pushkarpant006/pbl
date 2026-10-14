
#pragma once

#include <string>
#include <vector>

namespace safebox {
namespace analyzer {

struct Finding {
    std::string type;
    std::string severity;
    std::string message;
};

struct FindingsResult {
    std::string verdict;
    std::vector<Finding> findings;
};

FindingsResult buildFindings(bool timed_out, int process_count);

}}