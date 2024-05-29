#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
#pragma once
#include "protocolCraft/NetworkType.hpp"

namespace ProtocolCraft
{
    namespace Components
    {
        class AttributeModifier : public NetworkType
        {
            DECLARE_FIELDS_TYPES(UUID, std::string, double, VarInt);
            DECLARE_FIELDS_NAMES(Id, Name, Amount, Operation);
            DECLARE_READ_WRITE_SERIALIZE;

            GETTER_SETTER(Id);
            GETTER_SETTER(Name);
            GETTER_SETTER(Amount);
            GETTER_SETTER(Operation);
        };
    }
}
#endif
