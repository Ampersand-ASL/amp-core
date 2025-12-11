#include <ini.h>

#include <iostream>

using namespace std;

static int handler(void* user, const char* section, const char* name,
    const char* value) {
    cout << "Section: " << section << ", Name: " << name << ", Value: " << value << endl;
    return 1;
}

int main(int, const char**) {
    if (ini_parse("../src/tests/ini-test-1.ini", handler, 0) < 0) {
        cout << "Can't load file" << endl;
        return 1;
    }
    return 0;
}

