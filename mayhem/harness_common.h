// Shared by every mayhem harness (one translation unit per binary).
// Beam keeps the active consensus rules in a thread-local pointer that the final program must define.
#pragma once
#include "core/block_crypt.h"
#include "core/ecc_native.h"

thread_local const beam::Rules* beam::Rules::s_pInstance = nullptr;

namespace mayhem {
inline void InitBeam()
{
    // Never freed: the scopes must stay installed for the whole process.
    static auto* prg = new ECC::PseudoRandomGenerator;
    static auto* prgScope = new ECC::PseudoRandomGenerator::Scope(prg);
    static auto* rules = new beam::Rules;
    rules->UpdateChecksum();
    static auto* rulesScope = new beam::Rules::Scope(*rules);
    (void) prgScope;
    (void) rulesScope;
}
} // namespace mayhem
