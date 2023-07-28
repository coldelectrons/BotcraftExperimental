#include <botcraft/Game/AssetsManager.hpp>
#include <botcraft/Game/Inventory/InventoryManager.hpp>
#include <botcraft/Game/Inventory/Window.hpp>
#include <botcraft/Game/Entities/EntityManager.hpp>
#include <botcraft/Game/Entities/LocalPlayer.hpp>
#include <botcraft/AI/Tasks/AllTasks.hpp>

#include <queue>

#include "WorldEaterTasks.hpp"

using namespace Botcraft;

enum ActionType
{
    BreakBlock,
    PlaceSolidBlock,
    PlaceTempBlock,
    PlaceLadderBlock
};


Status Init(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    const int bot_index = blackboard.Get<int>("Eater.bot_index");
    const int num_bot = blackboard.Get<int>("Eater.num_bot");
    const Position start_block = blackboard.Get<Position>("Eater.start_block");
    const Position end_block = blackboard.Get<Position>("Eater.end_block");

    // Get initial bot position
    Position init_pos;
    std::shared_ptr<LocalPlayer> local_player = client.GetEntityManager()->GetLocalPlayer();
    {
        std::lock_guard<std::mutex> lock(local_player->GetMutex());
        init_pos = Position(
            static_cast<int>(std::floor(local_player->GetX())),
            static_cast<int>(std::floor(local_player->GetY())),
            static_cast<int>(std::floor(local_player->GetZ()))
        );
    }
    blackboard.Set<Position>("Eater.init_pos", init_pos);

    const Position min_block(
        std::min(start_block.x, end_block.x),
        std::min(start_block.y, end_block.y),
        std::min(start_block.z, end_block.z)
    );
    const Position max_block(
        std::max(start_block.x, end_block.x),
        std::max(start_block.y, end_block.y),
        std::max(start_block.z, end_block.z)
    );

    // Find out on which axis we "cut" the work area for all the bots
    // If the bot is in a corner, one of the two sides will be chosen
    Direction input_edge;
    if (init_pos.x < min_block.x)
    {
        input_edge = Direction::West;
    }
    else if (init_pos.x > max_block.x)
    {
        input_edge = Direction::East;
    }
    else if (init_pos.z < min_block.z)
    {
        input_edge = Direction::North;
    }
    else if (init_pos.z > max_block.z)
    {
        input_edge = Direction::South;
    }
    else
    {
        LOG_ERROR("Bot is inside the working area, please move it before starting");
        return Status::Failure;
    }
    blackboard.Set<Direction>("Eater.input_edge", input_edge);

    std::shared_ptr<World> world = client.GetWorld();

    Position this_bot_start;
    Position this_bot_end;
    Position ladder_pillar_position;
    Position ladder_position;
    Position layer_entry_position;
    PlayerDiggingFace ladder_face;
    if (input_edge == Direction::West || input_edge == Direction::East)
    {
        const float bot_area_size = static_cast<float>(max_block.z - min_block.z + 1) / num_bot;
        this_bot_start = Position(
            min_block.x,
            std::min(max_block.y, world->GetMinY() + world->GetHeight()),
            static_cast<int>(std::floor(min_block.z + bot_area_size * bot_index))
        );
        this_bot_end = Position(
            max_block.x,
            std::max(min_block.y, world->GetMinY()),
            (bot_index == num_bot - 1) ? max_block.z : static_cast<int>(std::floor(min_block.z + bot_area_size * bot_index + bot_area_size - 1))
        );
        ladder_pillar_position.x = input_edge == Direction::West ? min_block.x - 2 : max_block.x + 2;
        ladder_pillar_position.z = (this_bot_end.z + this_bot_start.z) / 2;
        ladder_position.x = input_edge == Direction::West ? min_block.x - 1 : max_block.x + 1;
        ladder_position.z = (this_bot_end.z + this_bot_start.z) / 2;
        layer_entry_position.x = input_edge == Direction::West ? min_block.x : max_block.x;
        layer_entry_position.z = (this_bot_end.z + this_bot_start.z) / 2;
        ladder_face = input_edge == Direction::West ? Direction::East : Direction::West;
    }
    else
    {
        const float bot_area_size = static_cast<float>(max_block.x - min_block.x + 1) / num_bot;
        this_bot_start = Position(
            static_cast<int>(std::floor(min_block.x + bot_area_size * bot_index)),
            max_block.y,
            min_block.z
        );
        this_bot_end = Position(
            (bot_index == num_bot - 1) ? max_block.x : static_cast<int>(std::floor(min_block.x + bot_area_size * bot_index + bot_area_size - 1)),
            min_block.y,
            max_block.z
        );
        ladder_pillar_position.x = (this_bot_end.x + this_bot_start.x) / 2;
        ladder_pillar_position.z = input_edge == Direction::North ? min_block.z - 2 : max_block.z + 2;
        ladder_position.x = (this_bot_end.x + this_bot_start.x) / 2;
        ladder_position.z = input_edge == Direction::North ? min_block.z - 1 : max_block.z + 1;
        layer_entry_position.x = (this_bot_end.x + this_bot_start.x) / 2;
        layer_entry_position.z = input_edge == Direction::North ? min_block.z : max_block.z;
        ladder_face = input_edge == Direction::North ? Direction::South : Direction::North;
    }

    {
        std::lock_guard<std::mutex> lock(world->GetMutex());
        // Get top of ladder pillar (first non air block going downard
        // from the top of the working area)
        Position block_pos = ladder_pillar_position;
        for (int y = this_bot_start.y; y >= world->GetMinY(); --y)
        {
            block_pos.y = y;
            const Block* block = world->GetBlock(block_pos);
            if (block && !block->GetBlockstate()->IsAir())
            {
                ladder_pillar_position.y = y;
                ladder_position.y = y;
                layer_entry_position.y = y;
                break;
            }
        }

        // Check the whole area is loaded (required for the planning algorithm to work properly)
        if (!world->IsLoaded(this_bot_end) || !world->IsLoaded(this_bot_start))
        {
            LOG_ERROR("Work area of " << client.GetNetworkManager()->GetMyName() << " (from " << this_bot_start << "to " << this_bot_end << ") is not fully loaded, please move it closer before launching it");
            return Status::Failure;
        }
    }

    blackboard.Set<Position>("Eater.start_block", this_bot_start);
    blackboard.Set<Position>("Eater.end_block", this_bot_end);
    blackboard.Set<Position>("Eater.ladder_pillar", ladder_pillar_position);
    blackboard.Set<Position>("Eater.ladder", ladder_position);
    blackboard.Set<Position>("Eater.layer_entry", layer_entry_position);
    blackboard.Set<PlayerDiggingFace>("Eater.ladder_face", ladder_face);
    blackboard.Set<int>("Eater.current_layer", this_bot_start.y);
    blackboard.Set<bool>("Eater.init", true);
    blackboard.Set<std::array<ToolType, static_cast<size_t>(ToolType::NUM_TOOL_TYPE) - 1>>("Eater.tools", {
        ToolType::Axe,
        ToolType::Hoe,
        ToolType::Pickaxe,
        ToolType::Shears,
        ToolType::Shovel,
        ToolType::Sword,
    });

    return Status::Success;
}

Status GetNextAction(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    const std::queue<std::pair<Position, ActionType>>& queue = blackboard.Get<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue");

    if (queue.empty())
    {
        return Status::Failure;
    }

    blackboard.Set<std::pair<Position, ActionType>>("Eater.next_action", queue.front());

    return Status::Success;
}

Status ExecuteAction(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();
    std::shared_ptr<World> world = client.GetWorld();

    const std::pair<Position, ActionType>& action = blackboard.Get<std::pair<Position, ActionType>>("Eater.next_action");

    if (action.second == ActionType::BreakBlock)
    {
        const Blockstate* blockstate;
        // Check if we really need to break this block
        {
            std::lock_guard<std::mutex> lock(world->GetMutex());
            const Block* block = world->GetBlock(action.first);
            // Skip if the block is already air or unbreakable
            if (block == nullptr || block->GetBlockstate()->IsAir() || block->GetBlockstate()->GetHardness() < 0)
            {
                return Status::Success;
            }
            blockstate = block->GetBlockstate();
        }

        // Check which tool type is the best
        /*const std::array<ToolType, static_cast<size_t>(ToolType::NUM_TOOL_TYPE) - 1>& tool_types = blackboard.Get<std::array<ToolType, static_cast<size_t>(ToolType::NUM_TOOL_TYPE) - 1>>("Eater.tools");
        ToolType best_tool_type = ToolType::None;
        float min_mining_time = std::numeric_limits<float>::max();

        for (const ToolType t : tool_types)
        {
            const float mining_time = blockstate->GetMiningTimeSeconds(t, static_cast<ToolMaterial>(static_cast<int>(ToolMaterial::NUM_TOOL_MATERIAL) - 1));
            if (mining_time < min_mining_time)
            {
                min_mining_time = mining_time;
                best_tool_type = t;
            }
        }*/

        client.SendChatCommand("setblock "
            + std::to_string(action.first.x) + " "
            + std::to_string(action.first.y) + " "
            + std::to_string(action.first.z) + " "
            + "minecraft:air");

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 2000)
        {
            {
                std::lock_guard<std::mutex> lock(world->GetMutex());
                const Block* block = world->GetBlock(action.first);
                if (block == nullptr || block->GetBlockstate()->IsAir())
                {
                    return Status::Success;
                }
            }
            client.Yield();
        }
        return Status::Failure;
    }
    else if (action.second == ActionType::PlaceTempBlock || action.second == ActionType::PlaceSolidBlock || action.second == ActionType::PlaceLadderBlock)
    {
        const std::string block_to_place =
            action.second == ActionType::PlaceLadderBlock ?
            "minecraft:ladder" :
            blackboard.Get<std::string>("Eater.temp_block");

        // Check if we really need to place this block
        {
            std::lock_guard<std::mutex> lock(world->GetMutex());
            const Block* block = world->GetBlock(action.first);
            // Skip if the block is already desired one
            if (block && (block->GetBlockstate()->GetName() == block_to_place ||
                (action.second == ActionType::PlaceSolidBlock && block->GetBlockstate()->IsSolid())))
            {
                return Status::Success;
            }
        }

        client.SendChatCommand("setblock "
            + std::to_string(action.first.x) + " "
            + std::to_string(action.first.y) + " "
            + std::to_string(action.first.z) + " "
            + block_to_place + (action.second == ActionType::PlaceLadderBlock ? "[facing=east]" : ""));


        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < 2000)
        {
            {
                std::lock_guard<std::mutex> lock(world->GetMutex());
                const Block* block = world->GetBlock(action.first);
                if (block != nullptr && block->GetBlockstate()->GetName() == block_to_place)
                {
                    return Status::Success;
                }
            }
            client.Yield();
        }
        return Status::Failure;
    }
    else
    {
        LOG_WARNING("Unknown ActionType " << static_cast<int>(action.second));
        return Status::Failure;
    }

    return Status::Success;
}

Botcraft::Status CheckActionsDone(Botcraft::SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    const std::queue<std::pair<Position, ActionType>>& queue = blackboard.Get<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue");

    return queue.empty() ? Status::Success : Status::Failure;
}

Status PopAction(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    NotifyOnEndUseRef<std::queue<std::pair<Position, ActionType>>> queue_ref = blackboard.GetRef<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue");
    std::queue<std::pair<Position, ActionType>>& queue = queue_ref.ref();

    if (queue.empty())
    {
        return Status::Failure;
    }

    queue.pop();

    return Status::Success;
}

Status IsInventoryFull(SimpleBehaviourClient& client)
{
    std::shared_ptr<InventoryManager> inventory_manager = client.GetInventoryManager();
    {
        std::lock_guard<std::mutex> lock_inventory(inventory_manager->GetMutex());
        std::shared_ptr<Window> player_inventory = inventory_manager->GetPlayerInventory();
        for (const auto& [idx, slot] : player_inventory->GetSlots())
        {
            if (idx < Window::INVENTORY_STORAGE_START)
            {
                continue;
            }
            if (slot.IsEmptySlot())
            {
                return Status::Failure;
            }
        }
    }
    return Status::Success;
}

Status CleanInventory(SimpleBehaviourClient& client)
{
    std::shared_ptr<LocalPlayer> local_player = client.GetEntityManager()->GetLocalPlayer();
    std::shared_ptr<World> world = client.GetWorld();
    std::shared_ptr<NetworkManager> network_manager = client.GetNetworkManager();
    std::shared_ptr<InventoryManager> inventory_manager = client.GetInventoryManager();
    Blackboard& blackboard = client.GetBlackboard();

    Vector3<double> init_position;
    // Look down
    {
        std::lock_guard<std::mutex> player_lock(local_player->GetMutex());
        init_position = local_player->GetPosition();
        local_player->LookAt(init_position, true);
    }

    // Wait for ~0.5 sec so the server registers the new look at
    for (int i = 0; i < 50; ++i)
    {
        client.Yield();
    }

    // Throw all items below
    const std::unordered_set<ItemId> items_to_keep = {
        AssetsManager::getInstance().GetItemID("minecraft:lava_bucket"),
        AssetsManager::getInstance().GetItemID("minecraft:golden_carrot"),
        AssetsManager::getInstance().GetItemID("minecraft:ladder"),
        AssetsManager::getInstance().GetItemID(blackboard.Get<std::string>("Eater.temp_block"))
    };
    std::vector<short> slots_to_throw;
    slots_to_throw.reserve(Window::INVENTORY_OFFHAND_INDEX);
    {
        std::lock_guard<std::mutex> lock_inventory(inventory_manager->GetMutex());
        std::shared_ptr<Window> player_inventory = inventory_manager->GetPlayerInventory();
        for (const auto& [idx, slot] : player_inventory->GetSlots())
        {
            // Don't throw lava bucket, food, temp block or tools
            if (idx < Window::INVENTORY_STORAGE_START ||
                slot.IsEmptySlot() ||
                items_to_keep.find(slot.GetItemID()) != items_to_keep.end() ||
                AssetsManager::getInstance().GetItem(slot.GetItemID())->GetToolType() != ToolType::None)
            {
                continue;
            }
            slots_to_throw.push_back(idx);
        }
    }

    for (const short idx : slots_to_throw)
    {
        DropItemsFromContainer(client, Window::PLAYER_INVENTORY_INDEX, idx);
    }

    // Jump
    {
        std::lock_guard<std::mutex> player_lock(local_player->GetMutex());
        local_player->Jump();
    }

    // Wait to be above start block
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
        // Something went wrong during jumping
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >= 2000)
        {
            LOG_WARNING("Timeout waiting for jump above 1 trying to clean inventory");
            return Status::Failure;
        }
        {
            std::lock_guard<std::mutex> player_lock(local_player->GetMutex());
            if (local_player->GetY() - init_position.y >= 1.0f)
            {
                break;
            }
        }
        client.Yield();
    }

    // Put lava below
    if (SetItemInHand(client, "minecraft:lava_bucket") != Botcraft::Status::Success)
    {
        return Status::Failure;
    }

    // Use item on
    std::shared_ptr<ProtocolCraft::ServerboundUseItemOnPacket> use_item_on = std::make_shared<ProtocolCraft::ServerboundUseItemOnPacket>();
    use_item_on->SetLocation(Position(
        static_cast<int>(std::floor(init_position.x)),
        static_cast<int>(std::floor(init_position.y)) - 1,
        static_cast<int>(std::floor(init_position.z))).ToNetworkPosition());
    use_item_on->SetDirection(static_cast<int>(Direction::Up));
    use_item_on->SetCursorPositionX(0.5f);
    use_item_on->SetCursorPositionY(1.0f);
    use_item_on->SetCursorPositionZ(0.5f);
#if PROTOCOL_VERSION > 452
    use_item_on->SetInside(false);
#endif
    use_item_on->SetHand(static_cast<int>(Hand::Right));
#if PROTOCOL_VERSION > 758
    {
        std::lock_guard<std::mutex> world_guard(world->GetMutex());
        use_item_on->SetSequence(world->GetNextWorldInteractionSequenceId());
    }
#endif
    network_manager->Send(use_item_on);
    // Use item
    std::shared_ptr<ProtocolCraft::ServerboundUseItemPacket> use_item = std::make_shared<ProtocolCraft::ServerboundUseItemPacket>();
    use_item->SetHand(static_cast<int>(Hand::Right));
#if PROTOCOL_VERSION > 758
    {
        std::lock_guard<std::mutex> world_guard(world->GetMutex());
        use_item->SetSequence(world->GetNextWorldInteractionSequenceId());
    }
#endif
    network_manager->Send(use_item);

    // Wait for jump heighest point
    start = std::chrono::steady_clock::now();
    while (true)
    {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() >= 2000)
        {
            LOG_WARNING("Timeout waiting for jump highest point trying to clean inventory");
            return Status::Failure;
        }
        {
            std::lock_guard<std::mutex> player_lock(local_player->GetMutex());
            if (local_player->GetSpeedY() < 0.0f)
            {
                break;
            }
        }
        client.Yield();
    }

    // Get back lava in bucket
    if (SetItemInHand(client, "minecraft:bucket") != Status::Success)
    {
        return Status::Failure;
    }

#if PROTOCOL_VERSION > 758
    {
        std::lock_guard<std::mutex> world_guard(world->GetMutex());
        use_item_on->SetSequence(world->GetNextWorldInteractionSequenceId());
        use_item->SetSequence(world->GetNextWorldInteractionSequenceId());
    }
#endif
    network_manager->Send(use_item_on);
    network_manager->Send(use_item);

    return Botcraft::Status::Success;
}

Status MoveToNextLayer(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    const int last_layer = blackboard.Get<Position>("Eater.end_block").y;
    const int current_layer = blackboard.Get<int>("Eater.current_layer");

    // We reached the end
    if (current_layer == last_layer - 1)
    {
        blackboard.Set<bool>("Eater.done", true);
        return Status::Failure;
    }
    else
    {
        blackboard.Set<int>("Eater.current_layer", current_layer - 1);
    }

    return Status::Success;
}

Status PrepareLayer(SimpleBehaviourClient& client)
{
    Blackboard& blackboard = client.GetBlackboard();

    const Position& start = blackboard.Get<Position>("Eater.start_block");
    const Position& end = blackboard.Get<Position>("Eater.end_block");
    const int current_layer = blackboard.Get<int>("Eater.current_layer");
    const Position& ladder_pillar = blackboard.Get<Position>("Eater.ladder_pillar");
    Position current_pillar = ladder_pillar;
    Position current_ladder = blackboard.Get<Position>("Eater.ladder");

    std::queue<std::pair<Position, ActionType>> action_queue;
    
    // Special case, remove all ladders
    if (current_layer < end.y)
    {
        for (int y = end.y; y < ladder_pillar.y; ++y)
        {
            current_ladder.y = y;
            action_queue.push({ current_ladder, ActionType::BreakBlock });
        }
        for (int y = start.y; y >= ladder_pillar.y; --y)
        {
            current_ladder.y = y;
            action_queue.push({ current_ladder, ActionType::BreakBlock });
        }
    }
    // Place ladder up to current layer
    else if (current_layer > ladder_pillar.y)
    {
        for (int y = ladder_pillar.y + 1; y <= current_layer; ++y)
        {
            current_ladder.y = y;
            current_pillar.y = y;
            action_queue.push({ current_pillar, ActionType::PlaceTempBlock });
            action_queue.push({ current_ladder, ActionType::PlaceLadderBlock });
        }
    }
    // Place ladder down to current layer
    else
    {
        for (int y = ladder_pillar.y; y >= current_layer; --y)
        {
            current_ladder.y = y;
            current_pillar.y = y;
            action_queue.push({ current_pillar, ActionType::PlaceTempBlock });
            action_queue.push({ current_ladder, ActionType::PlaceLadderBlock });
        }
    }

    blackboard.Set<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue", action_queue);
    return Status::Success;
}

Status PlanLayerFluids(SimpleBehaviourClient& client)
{
    std::shared_ptr<World> world = client.GetWorld();

    Blackboard& blackboard = client.GetBlackboard();
    const Position& start = blackboard.Get<Position>("Eater.start_block");
    const Position& end = blackboard.Get<Position>("Eater.end_block");
    const int current_layer = blackboard.Get<int>("Eater.current_layer");
    const PlayerDiggingFace ladder_face = blackboard.Get<PlayerDiggingFace>("Eater.ladder_face");
    Position layer_entry = blackboard.Get<Position>("Eater.ladder");
    layer_entry.y = current_layer;


    // Fluids pass
    // Get all fluid blocks
    // -1/+1 to avoid leaking from the sides
    std::unordered_set<Botcraft::Position> blocks_fluids = CollectBlocks(world, start + Botcraft::Position(-1, 0, -1), end + Botcraft::Position(1, 0, 1), current_layer, true, {});
    blocks_fluids.insert(layer_entry);
    std::unordered_map<Botcraft::Position, std::unordered_set<Botcraft::Position>> additional_neighbours_fluids;
    const std::vector<std::unordered_set<Botcraft::Position>> components_fluids = GroupBlocksInComponents(start - Botcraft::Position(1, 0, 1), end + Botcraft::Position(1, 0, 1), layer_entry, client, blocks_fluids, &additional_neighbours_fluids);
    const std::vector<Botcraft::Position> blocks_to_add_fluids = GetBlocksToAdd(components_fluids, layer_entry);
    const std::unordered_set<Botcraft::Position> blocks_to_fill_fluids = FlattenComponentsAndAdditionalBlocks(components_fluids, blocks_to_add_fluids);
    std::vector<Botcraft::Position> ordered_blocks_to_place_fluids = ComputeBlockOrder(blocks_to_fill_fluids, layer_entry, ladder_face, additional_neighbours_fluids);
    std::reverse(ordered_blocks_to_place_fluids.begin(), ordered_blocks_to_place_fluids.end());

    std::queue<std::pair<Position, ActionType>> action_queue;

    for (const Botcraft::Position& p : ordered_blocks_to_place_fluids)
    {
        // Don't replace the ladder...
        if (p == layer_entry)
        {
            continue;
        }
        action_queue.push({ p, ActionType::PlaceSolidBlock });
    }

    blackboard.Set<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue", action_queue);

    return Status::Success;
}

Status PlanLayerBlocks(SimpleBehaviourClient& client)
{
    std::shared_ptr<World> world = client.GetWorld();

    Blackboard& blackboard = client.GetBlackboard();
    const std::string& temp_block = blackboard.Get<std::string>("Eater.temp_block");
    const Position& start = blackboard.Get<Position>("Eater.start_block");
    const Position& end = blackboard.Get<Position>("Eater.end_block");
    const int current_layer = blackboard.Get<int>("Eater.current_layer");
    const PlayerDiggingFace ladder_face = blackboard.Get<PlayerDiggingFace>("Eater.ladder_face");
    Position layer_entry = blackboard.Get<Position>("Eater.ladder");
    layer_entry.y = current_layer;

    std::queue<std::pair<Position, ActionType>> action_queue;

    // Solid pass
    // Get all solid blocks
    std::unordered_set<Botcraft::Position> blocks = CollectBlocks(world, start, end, current_layer, false, {});
    blocks.insert(layer_entry);
    std::unordered_map<Botcraft::Position, std::unordered_set<Botcraft::Position>> additional_neighbours;
    const std::vector<std::unordered_set<Botcraft::Position>> components = GroupBlocksInComponents(start, end, layer_entry, client, blocks, &additional_neighbours);
    const std::vector<Botcraft::Position> blocks_to_add = GetBlocksToAdd(components, layer_entry);
    const std::unordered_set<Botcraft::Position> blocks_to_mine = FlattenComponentsAndAdditionalBlocks(components, blocks_to_add);
    const std::vector<Botcraft::Position> ordered_blocks_to_mine = ComputeBlockOrder(blocks_to_mine, layer_entry, ladder_face, additional_neighbours);

    for (const Botcraft::Position& p : blocks_to_add)
    {
        action_queue.push({ p, ActionType::PlaceSolidBlock });
    }
    for (const Botcraft::Position& p : ordered_blocks_to_mine)
    {
        // Don't replace the ladder...
        if (p == layer_entry)
        {
            continue;
        }
        action_queue.push({ p, ActionType::BreakBlock });
    }

    blackboard.Set<std::queue<std::pair<Position, ActionType>>>("Eater.action_queue", action_queue);

    return Status::Success;
}




std::unordered_set<Position> CollectBlocks(const std::shared_ptr<Botcraft::World> world, const Position& start, const Position& end, const int layer, const bool fluids, const std::unordered_set<std::string>& spared_blocks)
{
    std::unordered_set<Botcraft::Position> output;
    Botcraft::Position p(0, layer, 0);
    for (int x = start.x; x <= end.x; ++x)
    {
        p.x = x;
        for (int z = start.z; z <= end.z; ++z)
        {
            p.z = z;
            std::scoped_lock<std::mutex> lock(world->GetMutex());
            const Botcraft::Block* block = world->GetBlock(p);
            if (block && !block->GetBlockstate()->IsAir() && spared_blocks.find(block->GetBlockstate()->GetName()) == spared_blocks.cend() &&
                ((fluids && block->GetBlockstate()->IsFluid())
                    || (!fluids && !block->GetBlockstate()->IsAir()))
                )
            {
                output.insert(Botcraft::Position(x, layer, z));
            }
        }
    }
    return output;
}

std::vector<std::unordered_set<Position>> GroupBlocksInComponents(const Position& start, const Position& end, const Botcraft::Position& start_point, const Botcraft::BehaviourClient& client, const std::unordered_set<Position>& positions, std::unordered_map<Position, std::unordered_set<Position>>* additional_neighbours)
{
    std::vector<std::unordered_set<Botcraft::Position>> components;

    std::unordered_map<Botcraft::Position, int> components_index;
    for (const Botcraft::Position& b : positions)
    {
        components_index[b] = -1;
    }

    // For all block elements
    for (const auto& p : positions)
    {
        // If we already set a component to this position, skip it
        if (components_index[p] != -1)
        {
            continue;
        }
        // Otherwise add all neighbours we can find to the current component
        std::unordered_set<Botcraft::Position> current_component;
        std::unordered_set<Botcraft::Position> neighbours = { p };
        while (neighbours.size() > 0)
        {
            // Take first element in current neighbours
            const Botcraft::Position current_pos = *neighbours.begin();
            neighbours.erase(current_pos);

            // This neighbour is already in a component
            if (components_index[current_pos] != -1)
            {
                continue;
            }

            current_component.insert(current_pos);
            components_index[current_pos] = components.size();

            if (current_pos.x > start.x)
            {
                const Botcraft::Position west = current_pos + Botcraft::Position(-1, 0, 0);
                if (const auto it = components_index.find(west); it != components_index.end() && it->second == -1)
                {
                    neighbours.insert(west);
                }
            }
            if (current_pos.x < end.x)
            {
                const Botcraft::Position east = current_pos + Botcraft::Position(1, 0, 0);
                if (const auto it = components_index.find(east); it != components_index.end() && it->second == -1)
                {
                    neighbours.insert(east);
                }
            }
            if (current_pos.z > start.z)
            {
                const Botcraft::Position north = current_pos + Botcraft::Position(0, 0, -1);
                if (const auto it = components_index.find(north); it != components_index.end() && it->second == -1)
                {
                    neighbours.insert(north);
                }
            }
            if (current_pos.z < end.z)
            {
                const Botcraft::Position south = current_pos + Botcraft::Position(0, 0, 1);
                if (const auto it = components_index.find(south); it != components_index.end() && it->second == -1)
                {
                    neighbours.insert(south);
                }
            }
        }

        if (current_component.size() == 0)
        {
            continue;
        }

        // If additional neighbours are not asked, don't try to merge
        // components using pathfinding
        if (additional_neighbours == nullptr)
        {
            components.push_back(current_component);
            continue;
        }
        // Check if we can pathfind from one of the previous components
        // to this one and merge them if we can
        bool merged = false;
        for (size_t i = 0; i < components.size(); ++i)
        {
            const Botcraft::Position pathfinding_start = *components[i].begin() + Botcraft::Position(0, 1, 0);
            const Botcraft::Position pathfinding_end = *current_component.begin() + Botcraft::Position(0, 1, 0);
            const std::vector<Botcraft::Position> path = Botcraft::FindPath(client, pathfinding_start, pathfinding_end, 0, true);
            const std::vector<Botcraft::Position> reversed_path = Botcraft::FindPath(client, pathfinding_end, pathfinding_start, 0, true);
            // If we can pathfind from start to end (both ways to prevent cliff falls that would only allow one-way travel)
            if (path.back() == pathfinding_end && reversed_path.back() == pathfinding_start)
            {
                merged = true;
                // Add link between the two components as non adjacent neighbours
                Botcraft::Position path_block_component1 = pathfinding_start + Botcraft::Position(0, -1, 0);
                for (size_t j = 0; j < path.size(); ++j)
                {
                    // If the pathfinding crosses the boundaries of the work area anywhere else than the ladder,
                    // cancel the merging of the components
                    if ((path[j].x < start.x || path[j].x > end.x || path[j].z < start.z || path[j].z > end.z) &&
                        path[j].x != start_point.x && path[j].z != start_point.z)
                    {
                        merged = false;
                        break;
                    }

                    if (additional_neighbours != nullptr)
                    {
                        // If component index of the current path position is equal to the one currently compared to (i)
                        auto it = components_index.find(path[j] + Botcraft::Position(0, -1, 0));
                        if (it != components_index.end() && it->second == i)
                        {
                            path_block_component1 = path[j] + Botcraft::Position(0, -1, 0);
                        }
                    }
                }
                Botcraft::Position path_block_component2 = pathfinding_end + Botcraft::Position(0, -1, 0);
                for (size_t j = 0; j < reversed_path.size(); ++j)
                {
                    // If the pathfinding crosses the boundaries of the work area anywhere else than the ladder,
                    // cancel the merging of the components
                    if ((reversed_path[j].x < start.x || reversed_path[j].x > end.x || reversed_path[j].z < start.z || reversed_path[j].z > end.z) &&
                        reversed_path[j].x != start_point.x && reversed_path[j].z != start_point.z)
                    {
                        merged = false;
                        break;
                    }

                    if (additional_neighbours != nullptr)
                    {
                        // If component index of the current path position is equal to the current one to add (components.size())
                        auto it = components_index.find(reversed_path[j] + Botcraft::Position(0, -1, 0));
                        if (it != components_index.end() && it->second == components.size())
                        {
                            path_block_component2 = reversed_path[j] + Botcraft::Position(0, -1, 0);
                        }
                    }
                }
                if (merged)
                {
                    for (const Botcraft::Position& p : current_component)
                    {
                        components_index[p] = i;
                    }
                    components[i].insert(current_component.begin(), current_component.end());
                    if (additional_neighbours != nullptr)
                    {
                        (*additional_neighbours)[path_block_component1].insert(path_block_component2);
                        (*additional_neighbours)[path_block_component2].insert(path_block_component1);
                    }
                }
            }
        }
        if (!merged)
        {
            components.push_back(current_component);
        }
    }
    return components;
}

std::vector<Position> GetBlocksToAdd(const std::vector<std::unordered_set<Position>>& components, const Position& start_point)
{
    if (components.empty())
    {
        return {};
    }

    std::unordered_set<int> components_already_processed = { -1 };
    std::unordered_set<int> components_to_process;

    // Add all components to "to_process" vector
    for (int i = 0; i < components.size(); ++i)
    {
        components_to_process.insert(i);
    }

    std::vector<Botcraft::Position> output;
    output.reserve(components_to_process.size()); // rough estimate of ~1:1 ratio block present/block to add
    while (components_to_process.size() > 0)
    {
        float min_dist = std::numeric_limits<float>::max();
        int argmin_component_idx = -1;
        Botcraft::Position from;
        Botcraft::Position to;

        // Look for the "closest" remaining component
        // from the one we already have
        for (const int c1 : components_already_processed)
        {
            for (const int c2 : components_to_process)
            {
                for (const Botcraft::Position& p1 : c1 != -1 ? components[c1] : std::unordered_set<Botcraft::Position>{ start_point })
                {
                    for (const Botcraft::Position& p2 : components[c2])
                    {
                        const float dist = std::abs(p1.x - p2.x) + std::abs(p1.z - p2.z);
                        if (dist < min_dist)
                        {
                            min_dist = dist;
                            argmin_component_idx = c2;
                            from = p1;
                            to = p2;
                        }
                    }
                }
            }
        }

        // Dumb 2D staircase between from and to
        while (from != to)
        {
            if (std::abs(to.x - from.x) > std::abs(to.z - from.z))
            {
                from.x += from.x < to.x ? 1 : -1;
            }
            else
            {
                from.z += from.z < to.z ? 1 : -1;
            }
            if (from != to)
            {
                output.push_back(from);
            }
        }
        components_already_processed.insert(argmin_component_idx);
        // Remove start point after first time
        components_already_processed.erase(-1);
        components_to_process.erase(argmin_component_idx);
    }

    return output;
}

std::unordered_set<Position> FlattenComponentsAndAdditionalBlocks(const std::vector<std::unordered_set<Position>>& components, const std::vector<Position>& additional_blocks)
{
    std::unordered_set<Botcraft::Position> output;
    for (const std::unordered_set<Botcraft::Position>& v : components)
    {
        output.insert(v.begin(), v.end());
    }
    output.insert(additional_blocks.begin(), additional_blocks.end());
    return output;
}

std::vector<Position> ComputeBlockOrder(std::unordered_set<Position> blocks, const Position& start_block, const Botcraft::Direction orientation, const std::unordered_map<Position, std::unordered_set<Position>>& additional_neighbours)
{
    struct SortingNode
    {
        Botcraft::Position pos;
        std::vector<SortingNode> children;
    };

    // Should not happen as at least start_block should be in blocks
    if (blocks.size() == 0)
    {
        return { };
    }
    blocks.erase(start_block);

    SortingNode root{ start_block };

    std::queue<SortingNode*> to_process;
    to_process.push(&root);

    std::array<Botcraft::Position, 4> potential_neighbours;
    
    if (orientation == Direction::East || orientation == Direction::West)
    {
        potential_neighbours = {
            Botcraft::Position(-1, 0, 0),   // west
            Botcraft::Position(1, 0, 0),    // east
            Botcraft::Position(0, 0, -1),   // north
            Botcraft::Position(0, 0, 1)     // south
        };
    }
    else
    {
        potential_neighbours = {
            Botcraft::Position(0, 0, -1),   // north
            Botcraft::Position(0, 0, 1),    // south
            Botcraft::Position(-1, 0, 0),   // west
            Botcraft::Position(1, 0, 0)     // east
        };
    }
    

    while (blocks.size() > 0)
    {
        SortingNode* current_node = to_process.front();

        // Add direct neighbours
        for (const Botcraft::Position& offset : potential_neighbours)
        {
            const Botcraft::Position potential_neighbour = current_node->pos + offset;
            if (blocks.find(potential_neighbour) != blocks.end())
            {
                SortingNode neighbour;
                neighbour.pos = potential_neighbour;
                current_node->children.emplace_back(neighbour);
            }
        }
        // Add additional ones (if any)
        if (const auto it = additional_neighbours.find(current_node->pos); it != additional_neighbours.end())
        {
            for (const Botcraft::Position& potential_neighbour : it->second)
            {
                if (blocks.find(potential_neighbour) != blocks.end())
                {
                    SortingNode neighbour;
                    neighbour.pos = potential_neighbour;
                    current_node->children.emplace_back(neighbour);
                }
            }
        }

        for (size_t i = 0; i < current_node->children.size(); ++i)
        {
            to_process.push(current_node->children.data() + i);
            blocks.erase(current_node->children[i].pos);
        }
        to_process.pop();
    }

    // Recursive function to add all children, then current node
    // This allows to be sure that we will never "cut the branch
    // we are sitting on" preventing getting back to start_point
    const std::function<std::vector<Botcraft::Position>(const SortingNode& n)> get_sorted_positions =
        [&get_sorted_positions](const SortingNode& n) -> std::vector<Botcraft::Position>
    {
        std::vector<Botcraft::Position> output;
        for (const SortingNode& c : n.children)
        {
            const std::vector<Botcraft::Position> children_sorted = get_sorted_positions(c);
            output.insert(output.end(), children_sorted.begin(), children_sorted.end());
        }
        output.push_back(n.pos);

        return output;
    };

    return get_sorted_positions(root);
}

Position GetFarthestXZBlock(const std::unordered_set<Position>& blocks, const Position& p)
{
    int max_dist = 0;
    Botcraft::Position max_pos = p;
    for (const Botcraft::Position& b : blocks)
    {
        const int dist = std::abs(b.x - p.x) + std::abs(b.z - p.z);
        if (dist > max_dist)
        {
            max_dist = dist;
            max_pos = b;
        }
    }
    return max_pos;
}
