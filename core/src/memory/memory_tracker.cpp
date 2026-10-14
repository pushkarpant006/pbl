
#include "memory_tracker.hpp"

#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace std;

namespace safebox {
namespace memory {
namespace {

bool readProcFile(const string& path, string& contents, string& error,
                  SampleReadResult& result) {
    errno = 0;
    ifstream input(path.c_str());
    if (!input) {
        if (errno == ENOENT) {
            result = SampleReadResult::PROCESS_ENDED;
            return false;
        }
        error = "cannot open " + path;
        result = SampleReadResult::ERROR;
        return false;
    }
    ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        error = "cannot read " + path;
        result = SampleReadResult::ERROR;
        return false;
    }
    contents = buffer.str();
    return true;
}

bool parseCounter(const string& text, unsigned long& value) {
    try {
        size_t parsed = 0;
        value = stoul(text, &parsed);
        return parsed == text.size();
    } catch (const exception&) {
        return false;
    }
}

bool parseStatusMemory(const string& contents, long& rss_kb, long& vsize_kb) {
    bool found_rss = false;
    bool found_vsize = false;
    istringstream lines(contents);
    string line;
    while (getline(lines, line)) {
        istringstream fields(line);
        string name;
        long value = 0;
        string unit;
        if (!(fields >> name) || (name != "VmRSS:" && name != "VmSize:")) {
            continue;
        }
        if (!(fields >> value >> unit) || unit != "kB" || value < 0) {
            return false;
        }
        if (name == "VmRSS:") {
            rss_kb = value;
            found_rss = true;
        } else {
            vsize_kb = value;
            found_vsize = true;
        }
    }
    return found_rss && found_vsize;
}

bool parseStatm(const string& contents, unsigned long& virtual_pages,
                unsigned long& resident_pages) {
    istringstream fields(contents);
    string virtual_text;
    string resident_text;
    if (!(fields >> virtual_text >> resident_text)) {
        return false;
    }
    return parseCounter(virtual_text, virtual_pages) &&
           parseCounter(resident_text, resident_pages);
}

bool pagesToKilobytes(unsigned long virtual_pages, unsigned long resident_pages,
                      long& vsize_kb, long& rss_kb) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return false;
    }
    vsize_kb = static_cast<long>(virtual_pages * static_cast<unsigned long>(page_size) / 1024UL);
    rss_kb = static_cast<long>(resident_pages * static_cast<unsigned long>(page_size) / 1024UL);
    return true;
}

bool readProcessFiles(pid_t pid, string& status_text, string& stat_text,
                      string& statm_text, string& error, SampleReadResult& result) {
    string base = "/proc/" + to_string(pid);
    return readProcFile(base + "/status", status_text, error, result) &&
           readProcFile(base + "/stat", stat_text, error, result) &&
           readProcFile(base + "/statm", statm_text, error, result);
}

bool parseMemorySizes(const string& base, const string& status_text,
                      const string& statm_text, long& rss_kb, long& vsize_kb,
                      string& error) {
    bool status_has_memory = parseStatusMemory(status_text, rss_kb, vsize_kb);
    unsigned long virtual_pages = 0;
    unsigned long resident_pages = 0;
    if (!parseStatm(statm_text, virtual_pages, resident_pages)) {
        error = "invalid memory fields in " + base + "/statm";
        return false;
    }
    if (!status_has_memory && !pagesToKilobytes(virtual_pages, resident_pages, vsize_kb, rss_kb)) {
        error = "sysconf(_SC_PAGESIZE) failed";
        return false;
    }
    return true;
}

}
optional<ProcessStatFields> parseStatLine(const string& line) {
    size_t close_parenthesis = line.rfind(')');
    if (close_parenthesis == string::npos) {
        return nullopt;
    }
    istringstream fields(line.substr(close_parenthesis + 1));
    string values[10];
    for (int index = 0; index < 10; index++) {
        if (!(fields >> values[index])) {
            return nullopt;
        }
    }
    ProcessStatFields result{};
    if (!parseCounter(values[7], result.minor_faults) ||
        !parseCounter(values[9], result.major_faults)) {
        return nullopt;
    }
    return result;
}

SampleReadResult readSample(pid_t pid, MemorySample& sample, string& error) {
    if (pid <= 0) {
        error = "PID must be positive";
        return SampleReadResult::ERROR;
    }
    string status_text;
    string stat_text;
    string statm_text;
    SampleReadResult result = SampleReadResult::ERROR;
    if (!readProcessFiles(pid, status_text, stat_text, statm_text, error, result)) {
        return result;
    }
    string base = "/proc/" + to_string(pid);
    long rss_kb = 0;
    long vsize_kb = 0;
    if (!parseMemorySizes(base, status_text, statm_text, rss_kb, vsize_kb, error)) {
        return SampleReadResult::ERROR;
    }
    optional<ProcessStatFields> counters = parseStatLine(stat_text);
    if (!counters) {
        error = "invalid process counters in " + base + "/stat";
        return SampleReadResult::ERROR;
    }
    sample = MemorySample{chrono::steady_clock::now(), pid, rss_kb, vsize_kb,
                          counters->minor_faults, counters->major_faults};
    error.clear();
    return SampleReadResult::SUCCESS;
}

}}