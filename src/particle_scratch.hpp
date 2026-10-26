#pragma once
#include <Windows.h>
#include <string>
namespace patch_transaction { class transaction; }
namespace particle_scratch {
// [Limits] ParticleScratch=40 enables the clamp; absent/0/off keeps retail bytes.
void require_agreement(HMODULE); // before the shared startup transaction
void contribute(patch_transaction::transaction&, HMODULE);
void install(HMODULE);
std::string telemetry_json();
bool clamp_installed(); // the clamp committed (published after its transaction)
}
