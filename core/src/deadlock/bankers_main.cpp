
#include "bankers_algorithm.hpp"

#include <iostream>
#include <vector>

using namespace std;
using namespace safebox::deadlock;

bool readResourceVector(const char* label, vector<int>& values) {
    cout << label;
    for (int index = 0; index < static_cast<int>(values.size()); index++) {
        if (!(cin >> values[index]) || values[index] < 0) return false;
    }
    return true;
}

bool readProcessMatrices(ResourceState& state) {
    int process_count = static_cast<int>(state.allocation.size());
    for (int process = 0; process < process_count; process++) {
        cout << "P" << process << " allocation: ";
        if (!readResourceVector("", state.allocation[process])) return false;
        cout << "P" << process << " maximum: ";
        if (!readResourceVector("", state.maximum[process])) return false;
        for (int resource = 0; resource < static_cast<int>(state.available.size()); resource++) {
            if (state.allocation[process][resource] > state.maximum[process][resource]) return false;
        }
    }
    return true;
}

bool readCounts(int& process_count, int& resource_count) {
    cout << "Number of processes and resource types: ";
    if (!(cin >> process_count >> resource_count)) return false;
    return process_count > 0 && process_count <= 20 &&
           resource_count > 0 && resource_count <= 10;
}

void printSafetyResult(bool is_safe, const vector<int>& sequence) {
    if (!is_safe) {
        cout << "Unsafe state: no safe sequence exists.\n";
        return;
    }
    cout << "Safe state. Safe sequence: ";
    for (int index = 0; index < static_cast<int>(sequence.size()); index++) {
        if (index > 0) cout << " -> ";
        cout << "P" << sequence[index];
    }
    cout << "\n";
}

int main() {
    int process_count = 0;
    int resource_count = 0;
    if (!readCounts(process_count, resource_count)) {
        cerr << "Enter 1-20 processes and 1-10 resource types.\n";
        return 1;
    }
    ResourceState state{};
    state.allocation.resize(process_count, vector<int>(resource_count, 0));
    state.maximum.resize(process_count, vector<int>(resource_count, 0));
    state.available.resize(resource_count, 0);
    if (!readProcessMatrices(state) || !readResourceVector("Available resources: ", state.available)) {
        cerr << "Enter non-negative values and ensure allocation does not exceed maximum.\n";
        return 1;
    }
    vector<int> safe_sequence;
    bool is_safe = isSafeState(state, safe_sequence);
    printSafetyResult(is_safe, safe_sequence);
    return 0;
}
