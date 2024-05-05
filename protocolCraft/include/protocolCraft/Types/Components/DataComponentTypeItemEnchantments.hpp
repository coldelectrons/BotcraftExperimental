#pragma once
#if PROTOCOL_VERSION > 765 /* > 1.20.4 */
#include "protocolCraft/Types/Components/DataComponentType.hpp"

#include <map>

namespace ProtocolCraft
{
    namespace Components
    {
        class DataComponentTypeItemEnchantments : public DataComponentType
        {
        public:
            virtual ~DataComponentTypeItemEnchantments()
            {

            }


            const std::map<int, int>& GetEnchantments() const
            {
                return enchantments;
            }

            bool GetShowInTooltip() const
            {
                return show_in_tooltip;
            }


            void SetEnchantments(const std::map<int, int>& enchantments_)
            {
                enchantments = enchantments_;
            }

            void SetShowInTooltip(const bool show_in_tooltip_)
            {
                show_in_tooltip = show_in_tooltip_;
            }

        protected:
            virtual void ReadImpl(ReadIterator& iter, size_t& length) override
            {
                enchantments = ReadMap<int, int>(iter, length,
                    [](ReadIterator& i, size_t& l)
                    {
                        const int enchantment = ReadData<VarInt>(i, l);
                        const int level = ReadData<VarInt>(i, l);

                        return std::make_pair(enchantment, level);
                    }
                );
                show_in_tooltip = ReadData<bool>(iter, length);
            }

            virtual void WriteImpl(WriteContainer& container) const override
            {
                WriteMap<int, int>(enchantments, container,
                    [](const std::pair<int, int>& p, WriteContainer& c)
                    {
                        WriteData<VarInt>(p.first, c);
                        WriteData<VarInt>(p.second, c);
                    }
                );
                WriteData<bool>(show_in_tooltip, container);
            }

            virtual Json::Value SerializeImpl() const override
            {
                Json::Value output;

                output["enchantments"] = Json::Object();
                for (const auto& [k, v] : enchantments)
                {
                    output["enchantmens"][std::to_string(k)] = v;
                }
                output["show_in_tooltip"] = show_in_tooltip;

                return output;
            }

        private:
            std::map<int, int> enchantments;
            bool show_in_tooltip = true;

        };
    }
}
#endif
