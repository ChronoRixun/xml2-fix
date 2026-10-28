#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
namespace patch_transaction {class transaction;struct contribution;}
namespace e4_core {
void configure();
void require_agreement(HMODULE);
void prepare(HMODULE);
void contribute(patch_transaction::transaction&,HMODULE);
void after_commit(bool);
void verify_consumers();
bool installed();
std::string status();
// Synchronous lifecycle notifications from the compulsory observer. None of
// these publishes, constructs, rolls back or frees a half-constructed brain.
void actor_admitted(unsigned);
void actor_free(unsigned);
void controller_pop();
void controller_freed(unsigned new_live);
void boundary();
unsigned reserved();
#ifdef XML2_E4_TEST
bool test_set_encoding(unsigned);
#endif
}
