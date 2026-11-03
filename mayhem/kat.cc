// Known-answer probe for mayhem/test.sh. Built with the project's normal flags (no sanitizers) and
// dynamically linked, so the sabotage shim can neuter it. Prints one "key=value" line per check.
//   usage: kat <shader.wasm> <valid-tx.bin>
#include "harness_common.h"
#include "bvm/bvm2.h"
#include "bvm/evm.h"
#include "utility/serialize.h"
#include "core/serialization_adapters.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {

std::vector<uint8_t> ReadFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool Validate(const beam::Transaction& tx)
{
    beam::Transaction::Context ctx;
    return tx.IsValid(ctx);
}

} // namespace

int main(int, char** argv)
{
    mayhem::InitBeam();

    // 1. keccak256("abc") through the EVM hash helper.
    {
        beam::Evm::Word w;
        beam::Evm::Processor::HashOf(w, beam::Blob("abc", 3));
        char sz[beam::Evm::Word::nTxtLen + 1];
        w.Print(sz);
        printf("keccak256_abc=%s\n", sz);
    }

    // 2. Ethereum address derivation from a secret key.
    {
        ECC::Scalar sk;
        sk.m_Value.Scan("b533ffad9736e46ef0fbe34018fb0a30572fbe477bcc3a4e641c210ecc3ed8b5");
        ECC::Point::Native pt = ECC::Context::get().G * sk;
        beam::Evm::Address addr;
        addr.FromPubKey(pt);
        char sz[beam::Evm::Address::nTxtLen + 1];
        addr.Print(sz);
        printf("evm_address=%s\n", sz);
    }

    // 3. Compile a real contract shader to BVM bytecode.
    {
        auto wasm = ReadFile(argv[1]);
        beam::ByteBuffer res;
        beam::bvm2::Processor::Compile(res, beam::Blob(wasm.data(), static_cast<uint32_t>(wasm.size())),
                                       beam::bvm2::Processor::Kind::Contract);
        beam::Evm::Word w;
        beam::Evm::Processor::HashOf(w, beam::Blob(res));
        char sz[beam::Evm::Word::nTxtLen + 1];
        w.Print(sz);
        printf("wasm_in=%zu wasm_out=%zu wasm_out_keccak=%s\n", wasm.size(), res.size(), sz);
    }

    // 4. Full consensus validation of a serialized transaction, then of a tampered copy.
    {
        auto raw = ReadFile(argv[2]);
        beam::Deserializer der;
        der.reset(raw.data(), raw.size());
        beam::Transaction tx;
        const bool bOk = der.deserialize(tx);
        printf("tx_parsed=%d inputs=%zu outputs=%zu kernels=%zu\n", bOk ? 1 : 0, tx.m_vInputs.size(),
               tx.m_vOutputs.size(), tx.m_vKernels.size());
        if (bOk) {
            printf("tx_fee=%llu\n", (unsigned long long) tx.m_vKernels.at(0)->m_Fee);
            printf("tx_valid=%d\n", Validate(tx) ? 1 : 0);
            tx.m_vKernels.at(0)->m_Fee += 1;
            printf("tx_tampered_valid=%d\n", Validate(tx) ? 1 : 0);
        }
    }
    return 0;
}
