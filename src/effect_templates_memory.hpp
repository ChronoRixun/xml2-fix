#pragma once
#include "patch_transaction.hpp"

#ifdef XML2_EFFECT_TEMPLATES_TEST
namespace effect_templates_test {
void* allocate(std::size_t bytes);
const patch_transaction::hooks* transaction_hooks();
}
#endif
namespace effect_templates_memory {
inline void* allocate(std::size_t bytes){
#ifdef XML2_EFFECT_TEMPLATES_TEST
    return effect_templates_test::allocate(bytes);
#else
    return VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#endif
}
inline const patch_transaction::hooks* hooks(){
#ifdef XML2_EFFECT_TEMPLATES_TEST
    return effect_templates_test::transaction_hooks();
#else
    return nullptr;
#endif
}
}
