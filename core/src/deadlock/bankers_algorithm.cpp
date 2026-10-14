
#include "bankers_algorithm.hpp"

namespace safebox {
namespace deadlock {
namespace {

bool hasValidState(const ResourceState& state) {
    if (state.allocation.empty() || state.available.empty() ||
        state.allocation.size() != state.maximum.size()) return false;
    for (int process = 0; process < static_cast<int>(state.allocation.size()); process++) {
        if (state.allocation[process].size() != state.available.size() ||
            state.maximum[process].size() != state.available.size()) return false;
        for (int resource = 0; resource < static_cast<int>(state.available.size()); resource++) {
            if (state.allocation[process][resource] < 0 ||
                state.maximum[process][resource] < state.allocation[process][resource]) return false;
        }
    }
    for (int resource = 0; resource < static_cast<int>(state.available.size()); resource++) {
        if (state.available[resource] < 0) return false;
    }
    return true;
}

bool canFinishProcess(int process, const ResourceState& state,
                      const std::vector<int>& work, const std::vector<bool>& finished) {
    for (int resource = 0; resource < static_cast<int>(work.size()); resource++) {
        int need = state.maximum[process][resource] - state.allocation[process][resource];
        if (need > work[resource]) return false;
    }
    return finished[process] == 0;
}

}
bool isSafeState(const ResourceState& state, std::vector<int>& safe_sequence) {
    safe_sequence.clear();
    if (!hasValidState(state)) return false;
    std::vector<int> work = state.available;
    std::vector<bool> finished(state.allocation.size(), false);
    bool made_progress = true;
    while (made_progress) {
        made_progress = false;
        for (int process = 0; process < static_cast<int>(finished.size()); process++) {
            if (!canFinishProcess(process, state, work, finished)) continue;
            for (int resource = 0; resource < static_cast<int>(work.size()); resource++) {
                work[resource] += state.allocation[process][resource];
            }
            finished[process] = true;
            safe_sequence.push_back(process);
            made_progress = true;
        }
    }
    return safe_sequence.size() == state.allocation.size();
}

}}