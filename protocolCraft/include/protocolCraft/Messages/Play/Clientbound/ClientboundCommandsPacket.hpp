#pragma once

#if PROTOCOL_VERSION > 344
#include "protocolCraft/BaseMessage.hpp"
#include "protocolCraft/Types/CommandNode/CommandNode.hpp"

namespace ProtocolCraft
{
    class ClientboundCommandsPacket : public BaseMessage<ClientboundCommandsPacket>
    {
    public:
        virtual const int GetId() const override
        {
#if PROTOCOL_VERSION == 393 || PROTOCOL_VERSION == 401 || PROTOCOL_VERSION == 404 // 1.13.X
            return 0x11;
#elif PROTOCOL_VERSION == 477 || PROTOCOL_VERSION == 480 || PROTOCOL_VERSION == 485 || PROTOCOL_VERSION == 490 || PROTOCOL_VERSION == 498 // 1.14.X
            return 0x11;
#elif PROTOCOL_VERSION == 573 || PROTOCOL_VERSION == 575 || PROTOCOL_VERSION == 578 // 1.15.X
            return 0x12;
#elif PROTOCOL_VERSION == 735 || PROTOCOL_VERSION == 736  // 1.16.0 or 1.16.1
            return 0x11;
#elif PROTOCOL_VERSION == 751 || PROTOCOL_VERSION == 753 || PROTOCOL_VERSION == 754 // 1.16.2, 1.16.3, 1.16.4, 1.16.5
            return 0x10;
#else
            #error "Protocol version not implemented"
#endif
        }

        virtual const std::string GetName() const override
        {
            return "Commands";
        }

        virtual ~ClientboundCommandsPacket() override
        {

        }

        void SetNodes(const std::vector<CommandNode>& nodes_)
        {
            nodes = nodes_;
        }

        void SetRootIndex(const int root_index_)
        {
            root_index = root_index_;
        }


        const std::vector<CommandNode>& GetNodes() const
        {
            return nodes;
        }

        const int GetRootIndex() const
        {
            return root_index;
        }


    protected:
        virtual void ReadImpl(ReadIterator& iter, size_t& length) override
        {
            const int nodes_size = ReadVarInt(iter, length);
            nodes = std::vector<CommandNode>(nodes_size);
            for (int i = 0; i < nodes_size; ++i)
            {
                nodes[i].Read(iter, length);
            }
            root_index = ReadVarInt(iter, length);
        }

        virtual void WriteImpl(WriteContainer& container) const override
        {
            WriteVarInt(nodes.size(), container);
            for (int i = 0; i < nodes.size(); ++i)
            {
                nodes[i].Write(container);
            }
            WriteVarInt(root_index, container);
        }

        virtual const picojson::value SerializeImpl() const override
        {
            picojson::value value(picojson::object_type, false);
            picojson::object& object = value.get<picojson::object>();

            object["nodes"] = picojson::value(picojson::array_type, false);
            picojson::array& array = object["nodes"].get<picojson::array>();
            for (int i = 0; i < nodes.size(); ++i)
            {
                array.push_back(nodes[i].Serialize());
            }
            object["root_index"] = picojson::value((double)root_index);

            return value;
        }

    private:
        std::vector<CommandNode> nodes;
        int root_index;

    };
} //ProtocolCraft
#endif
