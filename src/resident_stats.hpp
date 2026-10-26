#pragma once
#include <Windows.h>
#include <string>
#include "stats_layout.hpp"
namespace patch_transaction {class transaction;}
namespace resident_stats {
void require_agreement(HMODULE);
void contribute(patch_transaction::transaction&,HMODULE,const stats_layout::descriptor& =stats_layout::raised);
void sample();
void begin_zone();
void end_zone();
std::string telemetry_json();
std::string status();
std::string census_json();
}
