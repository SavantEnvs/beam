// One-off seed generator (not built by build.sh). Writes starter seeds for fuzz_tx_validate and
// fuzz_proto_msg into the directory given as argv[1]:
//   <dir>/tx_*.bin    serialized, fully valid beam::Transaction objects
//   <dir>/msg_*.bin   one default-initialised message of each node message type: type byte + body
#include "../harness_common.h"
#include "utility/serialize.h"
#include "core/serialization_adapters.h"
#include "core/proto.h"
#include <cstdio>
#include <string>

namespace {

void Dump(const std::string& path, const std::vector<uint8_t>& v)
{
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(v.data(), 1, v.size(), f);
    fclose(f);
}

template <typename T> std::vector<uint8_t> Ser(const T& obj)
{
    beam::Serializer ser;
    ser & obj;
    std::vector<uint8_t> v;
    ser.swap_buf(v);
    return v;
}

void MakeTx(const std::string& path, beam::Amount valIn, beam::Amount fee, uint32_t nOutputs)
{
    using namespace ECC;
    beam::Key::IKdf::Ptr pKdf;
    HKdf::Create(pKdf, Zero);

    Scalar::Native sk, kOffset(Zero);
    beam::Transaction tx;

    beam::CoinID cidIn(valIn, 12, beam::Key::Type::Regular);
    beam::Input::Ptr pInp(new beam::Input);
    beam::CoinID::Worker(cidIn).Create(sk, pInp->m_Commitment, *pKdf);
    tx.m_vInputs.push_back(std::move(pInp));
    kOffset += sk;

    beam::Amount left = valIn - fee;
    for (uint32_t i = 0; i < nOutputs; i++) {
        beam::Amount v = (i + 1 == nOutputs) ? left : left / 2;
        left -= v;
        beam::CoinID cidOut(v, 100 + i, beam::Key::Type::Regular);
        beam::Output::Ptr pOut(new beam::Output);
        pOut->Create(0, sk, *pKdf, cidOut, *pKdf);
        tx.m_vOutputs.push_back(std::move(pOut));
        kOffset += -sk;
    }

    beam::TxKernelStd::Ptr pKrn(new beam::TxKernelStd);
    pKdf->DeriveKey(sk, beam::Key::ID(23123, beam::Key::Type::Kernel));
    pKrn->m_Commitment = Context::get().G * sk;
    pKrn->m_Fee = fee;
    pKrn->Sign(sk);
    tx.m_vKernels.push_back(std::move(pKrn));
    kOffset += -sk;

    tx.m_Offset = kOffset;
    tx.Normalize();

    beam::Transaction::Context ctx;
    if (!tx.IsValid(ctx)) {
        fprintf(stderr, "generated tx %s is not valid\n", path.c_str());
        exit(1);
    }
    Dump(path, Ser(tx));
}

} // namespace

int main(int, char** argv)
{
    mayhem::InitBeam();
    const std::string dir = argv[1];

    MakeTx(dir + "/tx_1out.bin", 1000, 100, 1);
    MakeTx(dir + "/tx_2out.bin", 500000, 250, 2);
    MakeTx(dir + "/tx_3out.bin", 123456789, 1000, 3);

#define GEN_MSG(code, msg) \
    { \
        beam::proto::msg m; \
        auto v = Ser(m); \
        v.insert(v.begin(), uint8_t(code)); \
        Dump(dir + "/msg_" #msg ".bin", v); \
    }
    BeamNodeMsgsAll(GEN_MSG)
#undef GEN_MSG
    return 0;
}
