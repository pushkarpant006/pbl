
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

void runChild() {
    sleep(2);
    _exit(0);
}

int main() {
    std::vector<pid_t> children;
    for (int index = 0; index < 3; index++) {
        pid_t child = fork();
        if (child < 0) {
            std::cerr << "fork failed.\n";
            return 1;
        }
        if (child == 0) runChild();
        children.push_back(child);
    }
    for (int index = 0; index < static_cast<int>(children.size()); index++) {
        if (waitpid(children[index], nullptr, 0) != children[index]) return 1;
    }
    return 0;
}
