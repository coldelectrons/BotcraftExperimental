#pragma once

#include "protocolCraft/BaseMessage.hpp"
#include "protocolCraft/Types/Item/Slot.hpp"

namespace ProtocolCraft
{
    class ClientboundContainerSetContentPacket : public BaseMessage<ClientboundContainerSetContentPacket>
    {
    public:
#if   PROTOCOL_VERSION == 340 /* 1.12.2 */
        static constexpr int packet_id = 0x14;
#elif PROTOCOL_VERSION == 393 /* 1.13 */ || PROTOCOL_VERSION == 401 /* 1.13.1 */ ||  \
      PROTOCOL_VERSION == 404 /* 1.13.2 */
        static constexpr int packet_id = 0x15;
#elif PROTOCOL_VERSION == 477 /* 1.14 */ || PROTOCOL_VERSION == 480 /* 1.14.1 */ ||  \
      PROTOCOL_VERSION == 485 /* 1.14.2 */ || PROTOCOL_VERSION == 490 /* 1.14.3 */ ||  \
      PROTOCOL_VERSION == 498 /* 1.14.4 */
        static constexpr int packet_id = 0x14;
#elif PROTOCOL_VERSION == 573 /* 1.15 */ || PROTOCOL_VERSION == 575 /* 1.15.1 */ ||  \
      PROTOCOL_VERSION == 578 /* 1.15.2 */
        static constexpr int packet_id = 0x15;
#elif PROTOCOL_VERSION == 735 /* 1.16 */ || PROTOCOL_VERSION == 736 /* 1.16.1 */
        static constexpr int packet_id = 0x14;
#elif PROTOCOL_VERSION == 751 /* 1.16.2 */ || PROTOCOL_VERSION == 753 /* 1.16.3 */ ||  \
      PROTOCOL_VERSION == 754 /* 1.16.4/5 */
        static constexpr int packet_id = 0x13;
#elif PROTOCOL_VERSION == 755 /* 1.17 */ || PROTOCOL_VERSION == 756 /* 1.17.1 */ ||  \
      PROTOCOL_VERSION == 757 /* 1.18/.1 */ || PROTOCOL_VERSION == 758 /* 1.18.2 */
        static constexpr int packet_id = 0x14;
#elif PROTOCOL_VERSION == 759 /* 1.19 */ || PROTOCOL_VERSION == 760 /* 1.19.1/2 */
        static constexpr int packet_id = 0x11;
#elif PROTOCOL_VERSION == 761 /* 1.19.3 */
        static constexpr int packet_id = 0x10;
#elif PROTOCOL_VERSION == 762 /* 1.19.4 */ || PROTOCOL_VERSION == 763 /* 1.20/.1 */
        static constexpr int packet_id = 0x12;
#elif PROTOCOL_VERSION == 764 /* 1.20.2 */ || PROTOCOL_VERSION == 765 /* 1.20.3/4 */ ||  \
      PROTOCOL_VERSION == 766 /* 1.20.5/6 */
        static constexpr int packet_id = 0x13;
#else
#error "Protocol version not implemented"
#endif

        static constexpr std::string_view packet_name = "Container Set Content";

        virtual ~ClientboundContainerSetContentPacket() override
        {

        }

        void SetContainerId(const unsigned char container_id_)
        {
            container_id = container_id_;
        }

        void SetItems(const std::vector<Slot>& items_)
        {
            items = items_;
        }

#if PROTOCOL_VERSION > 755 /* > 1.17 */
        void SetCarriedItem(const Slot& carried_item_)
        {
            carried_item = carried_item_;
        }

        void SetStateId(const int state_id_)
        {
            state_id = state_id_;
        }
#endif

        unsigned char GetContainerId() const
        {
            return container_id;
        }

        const std::vector<Slot>& GetSlotData() const
        {
            return items;
        }

#if PROTOCOL_VERSION > 755 /* > 1.17 */
        const Slot& GetCarriedItem() const
        {
            return carried_item;
        }

        int GetStateId() const
        {
            return state_id;
        }
#endif

    protected:
        virtual void ReadImpl(ReadIterator& iter, size_t& length) override
        {
            container_id = ReadData<unsigned char>(iter, length);
#if PROTOCOL_VERSION < 756 /* < 1.17.1 */
            // Special case, the data size is a short instead of a varint
            items = ReadVector<Slot, short>(iter, length, ReadData<Slot>);
#else
            state_id = ReadData<VarInt>(iter, length);
            items = ReadData<std::vector<Slot>>(iter, length);
            carried_item = ReadData<Slot>(iter, length);
#endif
        }

        virtual void WriteImpl(WriteContainer& container) const override
        {
            WriteData<unsigned char>(container_id, container);
#if PROTOCOL_VERSION < 756 /* < 1.17.1 */
            WriteVector<Slot, short>(items, container, WriteData<Slot>);
#else
            WriteData<VarInt>(state_id, container);
            WriteData<std::vector<Slot>>(items, container);
            WriteData<Slot>(carried_item, container);
#endif
        }

        virtual Json::Value SerializeImpl() const override
        {
            Json::Value output;

            output["container_id"] = container_id;
            output["items"] = items;

#if PROTOCOL_VERSION > 755 /* > 1.17 */
            output["state_id"] = state_id;
            output["carried_item"] = carried_item;
#endif

            return output;
        }

    private:
        unsigned char container_id = 0;
        std::vector<Slot> items;
#if PROTOCOL_VERSION > 755 /* > 1.17 */
        Slot carried_item;
        int state_id = 0;
#endif
    };
} //ProtocolCraft
