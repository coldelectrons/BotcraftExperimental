#pragma once

#if PROTOCOL_VERSION > 344 /* > 1.12.2 */
#include "protocolCraft/Types/CommandNode/BrigadierProperty.hpp"

namespace ProtocolCraft
{
    class BrigadierPropertyScoreHolder : public BrigadierProperty
    {
        DECLARE_FIELDS_TYPES(char);
        DECLARE_FIELDS_NAMES(Flags);
        DECLARE_READ_WRITE_SERIALIZE;

        GETTER_SETTER(Flags);
    };
}
#endif
