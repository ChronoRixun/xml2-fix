#pragma once
#include <string>
namespace test_state {
void sample(); // game keyboard poll only, enabled test pipe only
std::string reply(const std::string& command); // pipe thread: copies cached JSON
}
