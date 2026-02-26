#include <iostream>
#include "ProgramUtils.h"

using namespace std;
using namespace kc1fsz;
using namespace amp;

int main(int, const char**) {

    cout << "Segments:" << endl;
    std::queue<std::string> fs = ProgramUtils::getSegments("/tmp/program");
    while (!fs.empty()) {
        cout << fs.front() << endl;
        fs.pop();
    }

    cout << "Breaks:" << endl;
    fs = ProgramUtils::getBreaks("/tmp/program");
    while (!fs.empty()) {
        cout << fs.front() << endl;
        fs.pop();
    }

    return 0;
}
