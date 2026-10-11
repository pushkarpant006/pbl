// CONCEPT: Steady memory-growth workload
// WHY SAFEBOX NEEDS IT: Gradual retained allocation gives the detector a real
// process whose RSS growth can be measured over a sustained window.
// HOW IT WORKS: The process retains a touched 1 MiB chunk every 300 milliseconds,
// grows for seven seconds, and then exits normally.

#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

const int CHUNK_BYTES = 1024 * 1024;
const int ALLOCATION_INTERVAL_MS = 300;
const int RUN_DURATION_SECONDS = 7;

// Touch each memory page so the leak workload increases resident memory.
bool touchPages(vector<char>& memory) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        cerr << "sysconf(_SC_PAGESIZE) failed\n";
        return false;
    }
    for (int offset = 0; offset < static_cast<int>(memory.size()); offset += page_size) {
        memory[offset] = static_cast<char>(offset);
    }
    return true;
}

// Retain one touched chunk per interval until the demo duration expires.
bool growMemory(vector<vector<char>>& retained_memory) {
    chrono::steady_clock::time_point finish_time =
        chrono::steady_clock::now() + chrono::seconds(RUN_DURATION_SECONDS);
    while (chrono::steady_clock::now() < finish_time) {
        retained_memory.push_back(vector<char>(CHUNK_BYTES));
        if (!touchPages(retained_memory.back())) {
            return false;
        }
        this_thread::sleep_for(chrono::milliseconds(ALLOCATION_INTERVAL_MS));
    }
    return true;
}

// Print workload progress and run the steady-growth allocation.
int main() {
    vector<vector<char>> retained_memory;
    cout << "memory_leak pid=" << getpid() << " growing at about 3.3 MiB/s\n" << flush;
    if (!growMemory(retained_memory)) {
        return 1;
    }
    cout << "memory_leak pid=" << getpid() << " finished retained_mib="
         << retained_memory.size() << "\n" << flush;
    return 0;
}
