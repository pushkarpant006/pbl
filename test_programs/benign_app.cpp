
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

const int STABLE_ALLOCATION_BYTES = 32 * 1024 * 1024;
const int HOLD_DURATION_SECONDS = 5;

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

void holdMemory() {
    this_thread::sleep_for(chrono::seconds(HOLD_DURATION_SECONDS));
}

int main() {
    vector<char> stable_memory(STABLE_ALLOCATION_BYTES);
    if (!touchPages(stable_memory)) {
        return 1;
    }
    cout << "benign_app pid=" << getpid() << " holding 32 MiB stable\n" << flush;
    holdMemory();
    cout << "benign_app pid=" << getpid() << " finished\n" << flush;
    return 0;
}
