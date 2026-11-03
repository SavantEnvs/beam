// Fuzzes the transaction wire format and consensus validation: deserialize beam::Transaction,
// then run the full stateless validity check (rangeproofs, kernels, signatures, sigma).
#include "harness_common.h"
#include "utility/serialize.h"
#include "core/serialization_adapters.h"

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mayhem::InitBeam();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2)
        return -1;

    beam::Deserializer der;
    der.reset(data, size);

    beam::Transaction tx;
    if (!der.deserialize(tx))
        return -1;

    beam::Transaction::Context ctx;
    std::string err;
    tx.IsValid(ctx, &err);
    return 0;
}
