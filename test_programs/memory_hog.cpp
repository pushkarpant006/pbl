// CONCEPT: Rapid memory-allocation workload
// WHY SAFEBOX NEEDS IT: A real process that touches memory lets the monitor measure
// resident growth and exercise caller-selected limits without fake counters.
// HOW IT WORKS: The program allocates 2 MiB chunks, writes to every page, and waits
// briefly between chunks until it reaches the requested target size.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

const long DEFAULT_TARGET_MB = 16;
const long MAX_TARGET_MB = 512;
const int CHUNK_BYTES = 2 * 1024 * 1024;

// Touch one byte on every page so Linux makes the allocation resident.
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

// Parse the optional target and force flag from command-line arguments.
bool parseArguments(int argc, char** argv, long& target_mb, bool& force) {
    target_mb = DEFAULT_TARGET_MB;
    force = false;
    for (int index = 1; index < argc; index++) {
        string argument = argv[index];
        if (argument == "--force") {
            force = true;
        } else if (argument == "--target-mb" && index + 1 < argc) {
            try {
                size_t parsed = 0;
                target_mb = stol(argv[++index], &parsed);
                if (parsed != string(argv[index]).size()) {
                    return false;
                }
            } catch (const exception&) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (target_mb <= 0 || (target_mb > MAX_TARGET_MB && !force)) {
        return false;
    }
    return true;
}

// Allocate and retain touched chunks until the requested target has been reached.
bool allocateToTarget(long target_mb) {
    long target_bytes = target_mb * 1024 * 1024;
    long allocated_bytes = 0;
    vector<vector<char>> chunks;
    while (allocated_bytes < target_bytes) {
        long remaining_bytes = target_bytes - allocated_bytes;
        int chunk_size = CHUNK_BYTES;
        if (remaining_bytes < CHUNK_BYTES) {
            chunk_size = static_cast<int>(remaining_bytes);
        }
        chunks.push_back(vector<char>(chunk_size));
        if (!touchPages(chunks.back())) {
            return false;
        }
        allocated_bytes += chunk_size;
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    return true;
}

// Validate the target and run the harmless touched-memory workload.
int main(int argc, char** argv) {
    long target_mb = 0;
    bool force = false;
    if (!parseArguments(argc, argv, target_mb, force)) {
        cerr << "usage: memory_hog [--target-mb N] [--force]; max is 512 MiB without --force\n";
        return 2;
    }
    cout << "memory_hog pid=" << getpid() << " target_mb=" << target_mb << "\n" << flush;
    if (!allocateToTarget(target_mb)) {
        return 1;
    }
    cout << "memory_hog pid=" << getpid() << " reached_target_mb="
         << target_mb << "\n" << flush;
    return 0;
}
