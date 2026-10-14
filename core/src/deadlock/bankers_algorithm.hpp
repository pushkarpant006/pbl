
#pragma once

#include <string>
#include <vector>

namespace safebox {
namespace deadlock {

struct ResourceState {
    std::vector<std::vector<int>> allocation;
    std::vector<std::vector<int>> maximum;
    std::vector<int> available;
};

bool isSafeState(const ResourceState& state, std::vector<int>& safe_sequence);

}}