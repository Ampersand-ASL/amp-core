// Building command
//   g++ -Wall hello-json-1.cpp -I ../../nlohmann -o hello-json-1
#include <iostream>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

int main(int, const char**) {
    // Using (raw) string literals and json::parse
    json ex1 = json::parse(R"(
    {
        "pi": 3.141,
        "happy": true
    }
    )");
    cout << "pi=" << ex1["pi"] << endl;
}