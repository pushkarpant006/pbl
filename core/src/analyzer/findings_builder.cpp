
#include "findings_builder.hpp"

namespace safebox {
namespace analyzer {

FindingsResult buildFindings(bool timed_out, int process_count) {
    FindingsResult result{};
    if (timed_out) {
        result.findings.push_back({"TIMEOUT", "MEDIUM", "The analysis reached its configured timeout."});
    }
    if (process_count > 10) {
        result.findings.push_back({"TOO_MANY_PROCESSES", "MEDIUM",
                                   "The application created more than 10 processes."});
    }
    if (timed_out || process_count > 10) result.verdict = "MEDIUM";
    else result.verdict = "No problem detected";
    return result;
}

}}