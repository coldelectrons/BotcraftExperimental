#pragma once
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */

#include "protocolCraft/BaseMessage.hpp"

#include <string>

namespace ProtocolCraft
{
    class ClientboundTransferConfigurationPacket : public BaseMessage<ClientboundTransferConfigurationPacket>
    {
    public:
        static constexpr int packet_id = 0x05;
        static constexpr std::string_view packet_name = "Transfer (Configuration)";

        virtual ~ClientboundTransferConfigurationPacket() override
        {

        }


        void SetHost(const std::string& host_)
        {
            host = host_;
        }

        void SetPort(const int port_)
        {
            port = port_;
        }


        const std::string& GetHost() const
        {
            return host;
        }

        int GetPort() const
        {
            return port;
        }

    protected:
        virtual void ReadImpl(ReadIterator& iter, size_t& length) override
        {
            host = ReadData<std::string>(iter, length);
            port = ReadData<VarInt>(iter, length);
        }

        virtual void WriteImpl(WriteContainer& container) const override
        {
            WriteData<std::string>(host, container);
            WriteData<VarInt>(port, container);
        }

        virtual Json::Value SerializeImpl() const override
        {
            Json::Value output;

            output["host"] = host;
            output["port"] = port;

            return output;
        }

    private:
        std::string host;
        int port = 0;
    };
}
#endif
