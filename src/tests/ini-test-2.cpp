#include <cassert>
#include <fstream>
#include <inicpp.h>

using namespace std;

int main(int,const char**) {
    ifstream is("../src/tests/ini-test-1.ini");
    assert(is.is_open());
    ini::IniFile ini;
    ini.decode(is);
    int n = ini["bruce"]["node"].as<int>();
    assert(n == 61057);
    std::string s = ini["bruce"]["job"].as<std::string>();
    assert(s == "retired");
}
