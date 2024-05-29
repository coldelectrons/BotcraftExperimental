#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
#pragma once
#include "protocolCraft/Types/Components/DataComponentType.hpp"

#include <map>

namespace ProtocolCraft
{
    namespace Components
    {
        class DataComponentTypeBlockItemStateProperties : public DataComponentType
        {
            DECLARE_FIELDS_TYPES(std::map<std::string, std::string>);
            DECLARE_FIELDS_NAMES(Properties);
            DECLARE_READ_WRITE_SERIALIZE;

            GETTER_SETTER(Properties);
        };
    }
}
#endif
