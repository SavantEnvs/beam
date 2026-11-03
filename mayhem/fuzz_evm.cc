// Fuzzes the EVM interpreter (bvm/evm.cpp) with an in-memory account store.
// Input: byte 0 bit 7 selects "deploy" (rest is init code, run as a constructor) vs "call"
// (rest is runtime code installed at a fixed address, then called); bits 0-6 give the call-data length.
// Execution is bounded by the interpreter's own gas accounting.
#include "harness_common.h"
#include "bvm/evm.h"
#include "core/block_crypt.h"

namespace beam {

struct FuzzProcessor : public Evm::Processor
{
    typedef Evm::Address Address;
    typedef Evm::Word Word;

    Address m_aContract;
    Blob m_Code;

    ~FuzzProcessor() { Reset(); }

    Account& LoadAccount(const Address& addr) override
    {
        auto* p = new Account;
        p->m_Key = addr;
        m_Accounts.insert(*p);

        const bool bContract = (addr == m_aContract) && m_Code.n;
        p->m_Balance.m_Value = Zero;
        p->m_Exists.m_Value = bContract;
        if (bContract)
            p->m_Code.m_Value = m_Code;
        else
            ZeroObject(p->m_Code.m_Value);
        return *p;
    }

    Account::Slot& LoadSlot(Account& x, const Word& key) override
    {
        auto* p = new Account::Slot;
        p->m_Key = key;
        x.m_Slots.insert(*p);
        p->m_Data.m_Value = Zero;
        return *p;
    }

    Height get_Height() override { return 4355; }

    bool get_BlockHeader(BlockHeader& bh, Height) override
    {
        ZeroObject(bh);
        return true;
    }

    void RunFull()
    {
        while (!m_lstFrames.empty())
            RunOnce();
    }
};

} // namespace beam

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mayhem::InitBeam();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2)
        return -1;

    const bool bDeploy = (data[0] & 0x80) != 0;
    size_t nCall = data[0] & 0x7f;
    data++;
    size--;
    if (nCall > size)
        nCall = size;

    beam::FuzzProcessor evm;
    ZeroObject(evm.m_aContract);
    evm.m_aContract.m_pData[0] = 0xc0;

    beam::Evm::Address aCaller;
    ZeroObject(aCaller);
    aCaller.m_pData[0] = 0xca;

    try {
        evm.Reset();
        auto& acc = evm.LoadAccount(aCaller);
        evm.m_Top.m_pAccount = &acc;
        evm.m_Top.m_Gas = 3000000;

        beam::Evm::Processor::Args args;
        args.m_CallValue = beam::Zero;

        if (bDeploy) {
            args.m_Buf = beam::Blob(data, static_cast<uint32_t>(size));
            evm.Call(evm.m_aContract, args, true);
        } else {
            evm.m_Code = beam::Blob(data + nCall, static_cast<uint32_t>(size - nCall));
            args.m_Buf = beam::Blob(data, static_cast<uint32_t>(nCall));
            evm.Call(evm.m_aContract, args, false);
        }
        evm.RunFull();
    } catch (const std::exception&) {
        return -1;
    }
    return 0;
}
