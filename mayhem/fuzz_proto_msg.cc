// Fuzzes the node-to-node message decoders: every message type in BeamNodeMsgsAll is registered on a
// plain beam::Protocol dispatch table exactly like NodeConnection does, then one framed message
// body (type byte + payload) is dispatched through Protocol::on_new_message.
#include "harness_common.h"
#include "core/proto.h"
#include "core/serialization_adapters.h"

namespace {

struct Handler : beam::IErrorHandler
{
    void on_protocol_error(uint64_t, beam::ProtocolError) override {}
    void on_connection_error(uint64_t, beam::io::ErrorCode) override {}

#define MAYHEM_HANDLER(code, msg) \
    bool On_##msg(uint64_t, beam::proto::msg##_NoInit&&, uint32_t) { return true; }
    BeamNodeMsgsAll(MAYHEM_HANDLER)
#undef MAYHEM_HANDLER
};

Handler g_Handler;
beam::Protocol* g_pProtocol = nullptr;

} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mayhem::InitBeam();

    g_pProtocol = new beam::Protocol(0x42, 0x43, 0x44, 256, g_Handler, 1024 * 1024);
#define MAYHEM_REGISTER(code, msg) \
    g_pProtocol->add_message_handler<Handler, beam::proto::msg##_NoInit, &Handler::On_##msg>(uint8_t(code), &g_Handler, 0, 1024 * 1024 * 10);
    BeamNodeMsgsAll(MAYHEM_REGISTER)
#undef MAYHEM_REGISTER
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
        return -1;
    const uint8_t type = data[0];
    return g_pProtocol->on_new_message(0, type, data + 1, size - 1) ? 0 : -1;
}
