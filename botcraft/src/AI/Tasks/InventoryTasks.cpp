#include "botcraft/AI/Tasks/InventoryTasks.hpp"
#include "botcraft/AI/BehaviourClient.hpp"
#include "botcraft/AI/BehaviourTree.hpp"
#include "botcraft/AI/Blackboard.hpp"
#include "botcraft/AI/Tasks/BaseTasks.hpp"
#include "botcraft/AI/Tasks/PathfindingTask.hpp"

#include "botcraft/Game/AssetsManager.hpp"
#include "botcraft/Game/Entities/EntityManager.hpp"
#include "botcraft/Game/Entities/LocalPlayer.hpp"
#include "botcraft/Game/Inventory/InventoryManager.hpp"
#include "botcraft/Game/Inventory/Window.hpp"
#include "botcraft/Game/World/World.hpp"
#include "botcraft/Network/NetworkManager.hpp"
#include "botcraft/Utilities/Logger.hpp"

using namespace ProtocolCraft;

namespace Botcraft {
Status ClickSlotInContainer(BehaviourClient &client, const short container_id,
                            const short slot_id, const int click_type,
                            const char button_num) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  std::shared_ptr<ServerboundContainerClickPacket> click_window_msg =
      std::make_shared<ServerboundContainerClickPacket>();

  click_window_msg->SetContainerId(static_cast<unsigned char>(container_id));
  click_window_msg->SetSlotNum(slot_id);
  click_window_msg->SetButtonNum(button_num);
  click_window_msg->SetClickType(click_type);

  // ItemStack/CarriedItem, StateId and ChangedSlots will be set in
  // SendInventoryTransaction
  int transaction_id = client.SendInventoryTransaction(click_window_msg);

  // Wait for the click confirmation (versions < 1.17)
#if PROTOCOL_VERSION < 755
  auto start = std::chrono::steady_clock::now();
  while (true) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() >= 10000) {
      LOG_WARNING("Something went wrong trying to click slot (Timeout).");
      return Status::Failure;
    }
    TransactionState transaction_state =
        inventory_manager->GetTransactionState(container_id, transaction_id);
    if (transaction_state == TransactionState::Accepted) {
      break;
    }
    // The transaction has been refused by the server
    else if (transaction_state == TransactionState::Refused) {
      return Status::Failure;
    }

    client.Yield();
  }
#endif
  return Status::Success;
}

Status ClickSlotInContainerBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "ClickSlotInContainer.container_id", "ClickSlotInContainer.slot_id",
      "ClickSlotInContainer.click_type", "ClickSlotInContainer.button_num"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const short container_id = blackboard.Get<short>(variable_names[0]);
  const short slot_id = blackboard.Get<short>(variable_names[1]);
  const int click_type = blackboard.Get<int>(variable_names[2]);
  const char button_num = blackboard.Get<char>(variable_names[3]);

  return ClickSlotInContainer(client, container_id, slot_id, click_type,
                              button_num);
}

Status SwapItemsInContainer(BehaviourClient &client, const short container_id,
                            const short first_slot, const short second_slot) {
  // Left click on the first slot, transferring the slot to the cursor
  if (ClickSlotInContainer(client, container_id, first_slot, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Failed to swap items (first click)");
    return Status::Failure;
  }

  // Left click on the second slot, transferring the cursor to the slot
  if (ClickSlotInContainer(client, container_id, second_slot, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Failed to swap items (second click)");
    return Status::Failure;
  }

  // Left click on the first slot, transferring back the cursor to the slot
  if (ClickSlotInContainer(client, container_id, first_slot, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Failed to swap items (third click)");
    return Status::Failure;
  }

  return Status::Success;
}

Status SwapItemsInContainerBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "SwapItemsInContainer.container_id", "SwapItemsInContainer.first_slot",
      "SwapItemsInContainer.second_slot"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const short container_id = blackboard.Get<short>(variable_names[0]);
  const short first_slot = blackboard.Get<short>(variable_names[1]);
  const short second_slot = blackboard.Get<short>(variable_names[2]);

  return SwapItemsInContainer(client, container_id, first_slot, second_slot);
}

Status DropItemsFromContainer(BehaviourClient &client, const short container_id,
                              const short slot_id, const short num_to_keep) {
  if (ClickSlotInContainer(client, container_id, slot_id, 0, 0) ==
      Status::Failure) {
    return Status::Failure;
  }

  // Drop all
  if (num_to_keep == 0) {
    return ClickSlotInContainer(client, container_id, -999, 0, 0);
  }

  int item_count = 0;
  {
    std::shared_ptr<InventoryManager> inventory_manager =
        client.GetInventoryManager();
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    item_count = inventory_manager->GetCursor().GetItemCount();
  }

  // Drop the right amount of items
  while (item_count > num_to_keep) {
    if (ClickSlotInContainer(client, container_id, -999, 0, 1) ==
        Status::Failure) {
      return Status::Failure;
    }
    item_count -= 1;
  }

  // Put back remaining items in the initial slot
  return ClickSlotInContainer(client, container_id, slot_id, 0, 0);
}

Status DropItemsFromContainerBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "DropItemsFromContainer.container_id", "DropItemsFromContainer.slot_id",
      "DropItemsFromContainer.num_to_keep"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const short container_id = blackboard.Get<short>(variable_names[0]);
  const short slot_id = blackboard.Get<short>(variable_names[1]);

  // Optional
  const short num_to_keep = blackboard.Get<short>(variable_names[2], 0);

  return DropItemsFromContainer(client, container_id, slot_id, num_to_keep);
}

Status PutOneItemInContainerSlot(BehaviourClient &client,
                                 const short container_id,
                                 const short source_slot,
                                 const short destination_slot) {

  // Left click on the first slot, transferring the slot to the cursor
  if (ClickSlotInContainer(client, container_id, source_slot, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Failed to put one item in slot (first click)");
    return Status::Failure;
  }

  // Right click on the second slot, transferring one item of the cursor to the
  // slot
  if (ClickSlotInContainer(client, container_id, destination_slot, 0, 1) ==
      Status::Failure) {
    LOG_WARNING("Failed to put one item in slot (second click)");
    return Status::Failure;
  }

  // Left click on the first slot, transferring back the cursor to the slot
  if (ClickSlotInContainer(client, container_id, source_slot, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Failed to put one item in slot (third click)");
    return Status::Failure;
  }

  return Status::Success;
}

Status PutOneItemInContainerSlotBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "PutOneItemInContainerSlot.container_id",
      "PutOneItemInContainerSlot.source_slot",
      "PutOneItemInContainerSlot.destination_slot"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const short container_id = blackboard.Get<short>(variable_names[0]);
  const short source_slot = blackboard.Get<short>(variable_names[1]);
  const short destination_slot = blackboard.Get<short>(variable_names[2]);

  return PutOneItemInContainerSlot(client, container_id, source_slot,
                                   destination_slot);
}

Status SetItemInHand(BehaviourClient &client, const std::string &item_name,
                     const Hand hand) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  short inventory_correct_slot_index = -1;
  short inventory_destination_slot_index = -1;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());

    inventory_destination_slot_index =
        hand == Hand::Off ? Window::INVENTORY_OFFHAND_INDEX
                          : (Window::INVENTORY_HOTBAR_START +
                             inventory_manager->GetIndexHotbarSelected());

    // We need to check the inventory
    // If the currently selected item is the right one, just go for it
    const Slot &current_selected = hand == Hand::Off
                                       ? inventory_manager->GetOffHand()
                                       : inventory_manager->GetHotbarSelected();
    if (!current_selected.IsEmptySlot()
#if PROTOCOL_VERSION < 347
        && AssetsManager::getInstance()
                   .Items()
                   .at(current_selected.GetBlockID())
                   .at(static_cast<unsigned char>(
                       current_selected.GetItemDamage()))
                   ->GetName() == item_name)
#else
        && AssetsManager::getInstance()
                   .Items()
                   .at(current_selected.GetItemID())
                   ->GetName() == item_name)
#endif
    {
      return Status::Success;
    }

    // Otherwise we need to find a slot with the given item
    const std::map<short, Slot> &slots =
        inventory_manager->GetPlayerInventory()->GetSlots();
    for (auto it = slots.begin(); it != slots.end(); ++it) {
      if (it->first >= Window::INVENTORY_STORAGE_START &&
          it->first < Window::INVENTORY_OFFHAND_INDEX &&
          !it->second.IsEmptySlot()
#if PROTOCOL_VERSION < 347
          && AssetsManager::getInstance()
                     .Items()
                     .at(it->second.GetBlockID())
                     .at(static_cast<unsigned char>(it->second.GetItemDamage()))
                     ->GetName() == item_name)
#else
          && AssetsManager::getInstance()
                     .Items()
                     .at(it->second.GetItemID())
                     ->GetName() == item_name)
#endif
      {
        inventory_correct_slot_index = it->first;
        break;
      }
    }

    // If there is no stack with the given item in the inventory
    if (inventory_correct_slot_index == -1) {
      return Status::Failure;
    }
  }

  return SwapItemsInContainer(client, Window::PLAYER_INVENTORY_INDEX,
                              inventory_correct_slot_index,
                              inventory_destination_slot_index);
}

Status SetItemInHandBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {"SetItemInHand.item_name",
                                                   "SetItemInHand.hand"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::string &item_name = blackboard.Get<std::string>(variable_names[0]);
  const Hand hand = blackboard.Get<Hand>(variable_names[1], Hand::Main);

  return SetItemInHand(client, item_name, hand);
}

Status PlaceBlock(BehaviourClient &client, const std::string &item_name,
                  const Position &pos, const PlayerDiggingFace face,
                  const bool wait_confirmation,
                  const bool allow_midair_placing) {
  std::shared_ptr<World> world = client.GetWorld();
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();
  std::shared_ptr<EntityManager> entity_manager = client.GetEntityManager();
  std::shared_ptr<NetworkManager> network_manager = client.GetNetworkManager();
  std::shared_ptr<LocalPlayer> local_player = entity_manager->GetLocalPlayer();

  Vector3<double> player_pos;
  {
    std::lock_guard<std::mutex> lock(local_player->GetMutex());
    player_pos = local_player->GetPosition();
  }

  // Compute the distance from the hand? Might be from somewhere else
  player_pos.y += 1.0;

  if (player_pos.SqrDist(Vector3<double>(0.5, 0.5, 0.5) + pos) > 16.0f) {
    if (GoTo(client, pos, 4, 1) == Status::Failure) {
      return Status::Failure;
    }
  }

  {
    std::lock_guard<std::mutex> lock(local_player->GetMutex());
    local_player->LookAt(Vector3<double>(0.5) + pos);
  }

  const std::vector<Position> neighbour_offsets(
      {Position(0, 1, 0), Position(0, -1, 0), Position(0, 0, 1),
       Position(0, 0, -1), Position(1, 0, 0), Position(-1, 0, 0)});

  // Check if block is air
  bool midair_placing = true;
  {
    std::lock_guard<std::mutex> world_guard(world->GetMutex());

    const Block *block = world->GetBlock(pos);

    if (block && !block->GetBlockstate()->IsAir()) {
      return Status::Failure;
    }

    const Block *neighbour_block =
        world->GetBlock(pos + neighbour_offsets[static_cast<int>(face)]);
    midair_placing =
        !neighbour_block || neighbour_block->GetBlockstate()->IsAir();

    if (!allow_midair_placing && midair_placing) {
      LOG_WARNING("Can't place a block in midair at " << pos);
      return Status::Failure;
    }
  }

  // Check if any entity is in the middle
  {
    const AABB this_box_collider =
        AABB(Vector3<double>(pos.x + 0.5, pos.y + 0.5, pos.z + 0.5),
             Vector3<double>(0.5, 0.5, 0.5));

    std::lock_guard<std::mutex> entity_manager_guard(
        entity_manager->GetMutex());
    const std::unordered_map<int, std::shared_ptr<Entity>> &entities =
        entity_manager->GetEntities();
    for (auto it = entities.begin(); it != entities.end(); ++it) {
      // xp orbs and items don't prevent block placing
      if (it->second->GetType() == EntityType::ExperienceOrb ||
          it->second->GetType() == EntityType::ItemEntity) {
        continue;
      }
      if (this_box_collider.Collide(it->second->GetCollider())) {
        return Status::Failure;
      }
    }
  }

  // Check if item in inventory
  if (SetItemInHand(client, item_name, Hand::Main) == Status::Failure) {
    return Status::Failure;
  }

  int num_item_in_hand;
  {
    std::lock_guard<std::mutex> inventory_lock(inventory_manager->GetMutex());
    num_item_in_hand =
        inventory_manager->GetPlayerInventory()
            ->GetSlot(Window::INVENTORY_HOTBAR_START +
                      inventory_manager->GetIndexHotbarSelected())
            .GetItemCount();
  }

  // If cheating is not allowed, adjust the placing position to the block
  // containing the face we're placing against
  const Position placing_pos =
      (allow_midair_placing && midair_placing)
          ? pos
          : (pos + neighbour_offsets[static_cast<int>(face)]);

  std::shared_ptr<ServerboundUseItemOnPacket> place_block_msg =
      std::make_shared<ServerboundUseItemOnPacket>();
  place_block_msg->SetLocation(placing_pos.ToNetworkPosition());
  place_block_msg->SetDirection(static_cast<int>(face));
  switch (face) {
  case PlayerDiggingFace::Down:
    place_block_msg->SetCursorPositionX(0.5f);
    place_block_msg->SetCursorPositionY(0.0f);
    place_block_msg->SetCursorPositionZ(0.5f);
    break;
  case PlayerDiggingFace::Up:
    place_block_msg->SetCursorPositionX(0.5f);
    place_block_msg->SetCursorPositionY(1.0f);
    place_block_msg->SetCursorPositionZ(0.5f);
    break;
  case PlayerDiggingFace::North:
    place_block_msg->SetCursorPositionX(0.5f);
    place_block_msg->SetCursorPositionY(0.5f);
    place_block_msg->SetCursorPositionZ(0.0f);
    break;
  case PlayerDiggingFace::South:
    place_block_msg->SetCursorPositionX(0.5f);
    place_block_msg->SetCursorPositionY(0.5f);
    place_block_msg->SetCursorPositionZ(1.0f);
    break;
  case PlayerDiggingFace::East:
    place_block_msg->SetCursorPositionX(1.0f);
    place_block_msg->SetCursorPositionY(0.5f);
    place_block_msg->SetCursorPositionZ(0.5f);
    break;
  case PlayerDiggingFace::West:
    place_block_msg->SetCursorPositionX(0.0f);
    place_block_msg->SetCursorPositionY(0.5f);
    place_block_msg->SetCursorPositionZ(0.5f);
    break;
  default:
    break;
  }
#if PROTOCOL_VERSION > 452
  place_block_msg->SetInside(false);
#endif
  place_block_msg->SetHand(static_cast<int>(Hand::Main));
#if PROTOCOL_VERSION > 758
  {
    std::lock_guard<std::mutex> world_guard(world->GetMutex());
    place_block_msg->SetSequence(world->GetNextWorldInteractionSequenceId());
  }
#endif

  // Place the block
  network_manager->Send(place_block_msg);

  if (!wait_confirmation) {
    return Status::Success;
  }

  bool is_block_ok = false;
  bool is_slot_ok = false;
  auto start = std::chrono::steady_clock::now();
  while (!is_block_ok || !is_slot_ok) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() >= 3000) {
      LOG_WARNING(
          '['
          << network_manager->GetMyName()
          << "] Something went wrong waiting block placement confirmation at "
          << pos << " (Timeout).");
      return Status::Failure;
    }
    if (!is_block_ok) {
      std::lock_guard<std::mutex> world_guard(world->GetMutex());
      const Block *block = world->GetBlock(pos);

      if (block && block->GetBlockstate()->GetName() == item_name) {
        is_block_ok = true;
      }
    }
    if (!is_slot_ok) {
      std::lock_guard<std::mutex> inventory_lock(inventory_manager->GetMutex());
      int new_num_item_in_hand =
          inventory_manager->GetPlayerInventory()
              ->GetSlot(Window::INVENTORY_HOTBAR_START +
                        inventory_manager->GetIndexHotbarSelected())
              .GetItemCount();
      is_slot_ok = new_num_item_in_hand == num_item_in_hand - 1;
    }

    if (is_block_ok && is_slot_ok) {
      return Status::Success;
    }

    client.Yield();
  }

  return Status::Success;
}

Status PlaceBlockBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "PlaceBlock.item_name", "PlaceBlock.pos", "PlaceBlock.face",
      "PlaceBlock.wait_confirmation", "PlaceBlock.allow_midair_placing"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::string &item_name = blackboard.Get<std::string>(variable_names[0]);
  const Position &pos = blackboard.Get<Position>(variable_names[1]);

  // Optional
  const PlayerDiggingFace face = blackboard.Get<PlayerDiggingFace>(
      variable_names[2], PlayerDiggingFace::Up);
  const bool wait_confirmation = blackboard.Get<bool>(variable_names[3], false);
  const bool allow_midair_placing =
      blackboard.Get<bool>(variable_names[4], false);

  return PlaceBlock(client, item_name, pos, face, wait_confirmation,
                    allow_midair_placing);
}

Status Eat(BehaviourClient &client, const std::string &food_name,
           const bool wait_confirmation) {
  if (SetItemInHand(client, food_name, Hand::Off) == Status::Failure) {
    return Status::Failure;
  }

  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();
  std::shared_ptr<NetworkManager> network_manager = client.GetNetworkManager();

  const char current_stack_size =
      inventory_manager->GetOffHand().GetItemCount();
  std::shared_ptr<ServerboundUseItemPacket> use_item_msg(
      new ServerboundUseItemPacket);
  use_item_msg->SetHand(static_cast<int>(Hand::Off));
#if PROTOCOL_VERSION > 758
  {
    std::shared_ptr<World> world = client.GetWorld();
    std::lock_guard<std::mutex> world_guard(world->GetMutex());
    use_item_msg->SetSequence(world->GetNextWorldInteractionSequenceId());
  }
#endif
  network_manager->Send(use_item_msg);

  if (!wait_confirmation) {
    return Status::Success;
  }

  auto start = std::chrono::steady_clock::now();
  while (inventory_manager->GetOffHand().GetItemCount() == current_stack_size) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() >= 3000) {
      LOG_WARNING("Something went wrong trying to eat (Timeout).");
      return Status::Failure;
    }
    client.Yield();
  }

  return Status::Success;
}

Status EatBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {"Eat.food_name",
                                                   "Eat.wait_confirmation"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::string &food_name = blackboard.Get<std::string>(variable_names[0]);

  // Optional
  const bool wait_confirmation = blackboard.Get<bool>(variable_names[1], false);

  return Eat(client, food_name, wait_confirmation);
}

Status OpenContainer(BehaviourClient &client, const Position &pos) {
  // Open the container
  if (InteractWithBlock(client, pos, PlayerDiggingFace::Up) ==
      Status::Failure) {
    return Status::Failure;
  }

  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  // Wait for a window to be opened
  auto start = std::chrono::steady_clock::now();
  while (inventory_manager->GetFirstOpenedWindowId() == -1) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() >= 3000) {
      LOG_WARNING("Something went wrong trying to open container (Timeout).");
      return Status::Failure;
    }
    client.Yield();
  }

  return Status::Success;
}

Status OpenContainerBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {"OpenContainer.pos"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const Position &pos = blackboard.Get<Position>(variable_names[0]);

  return OpenContainer(client, pos);
}

Status CloseContainer(BehaviourClient &client, const short container_id) {
  std::shared_ptr<NetworkManager> network_manager = client.GetNetworkManager();
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  std::shared_ptr<ServerboundContainerClosePacket> close_container_msg =
      std::make_shared<ServerboundContainerClosePacket>();
  short true_container_id = container_id;
  if (true_container_id < 0) {
    std::lock_guard<std::mutex> lock_inventory_manager(
        inventory_manager->GetMutex());
    true_container_id = inventory_manager->GetFirstOpenedWindowId();
  }
  close_container_msg->SetContainerId(
      static_cast<unsigned char>(true_container_id));
  network_manager->Send(close_container_msg);

  // There is no confirmation from the server, so we
  // can simply close the window here
  inventory_manager->EraseInventory(true_container_id);

  return Status::Success;
}

Status CloseContainerBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "CloseContainer.container_id"};

  Blackboard &blackboard = client.GetBlackboard();

  // Optional
  const short container_id = blackboard.Get<short>(variable_names[0], -1);

  return CloseContainer(client, container_id);
}

Status LogInventoryContent(BehaviourClient &client, const LogLevel level) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  std::stringstream output;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    output << "Cursor --> " << inventory_manager->GetCursor().Serialize().Dump()
           << "\n";
    for (const auto &s : inventory_manager->GetPlayerInventory()->GetSlots()) {
      output << s.first << " --> " << s.second.Serialize().Dump() << "\n";
    }
  }
  LOG(output.str(), level);
  return Status::Success;
}

Status LogInventoryContentBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {"LogInventoryContent.level"};

  Blackboard &blackboard = client.GetBlackboard();

  // Optional
  const LogLevel level =
      blackboard.Get<LogLevel>(variable_names[0], LogLevel::Info);

  return LogInventoryContent(client, level);
}

#if PROTOCOL_VERSION > 451
Status Trade(BehaviourClient &client, const int item_id, const bool buy,
             const int trade_id) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  // Make sure a trading window is opened and
  // possible trades are available
  auto start = std::chrono::steady_clock::now();
  size_t num_trades = 0;
  do {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() > 5000) {
      LOG_WARNING("Something went wrong waiting trade opening (Timeout).");
      return Status::Failure;
    }

    {
      std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
      num_trades = inventory_manager->GetAvailableTrades().size();
    }
    client.Yield();
  } while (num_trades <= 0 ||
           inventory_manager->GetFirstOpenedWindowId() == -1);

  short container_id;
  std::shared_ptr<Window> trading_container;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    container_id = inventory_manager->GetFirstOpenedWindowId();
    trading_container = inventory_manager->GetWindow(container_id);
  }

  if (!trading_container) {
    LOG_WARNING("Something went wrong during trade (window closed).");
    return Status::Failure;
  }

  int trade_index = trade_id;
  bool has_trade_second_item = false;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    const std::vector<ProtocolCraft::Trade> &trades =
        inventory_manager->GetAvailableTrades();

    // Find which trade we want in the list
    if (trade_id == -1) {
      for (int i = 0; i < trades.size(); ++i) {
        if ((buy && trades[i].GetOutputItem().GetItemID() == item_id) ||
            (!buy && trades[i].GetInputItem1().GetItemID() == item_id)) {
          trade_index = i;
          has_trade_second_item = trades[i].GetInputItem2().has_value();
          break;
        }
      }
    }

    if (trade_index == -1) {
      LOG_WARNING("Failed trading (this villager does not sell/buy "
                  << AssetsManager::getInstance().Items().at(item_id)->GetName()
                  << ")");
      return Status::Failure;
    }

    // Check that the trade is not locked
    if (trades[trade_index].GetNumberOfTradesUses() >=
        trades[trade_index].GetMaximumNumberOfTradeUses()) {
      LOG_WARNING("Failed trading (trade locked)")
      return Status::Failure;
    }
  }

  std::shared_ptr<NetworkManager> network_manager = client.GetNetworkManager();

  // Select the trade in the list
  std::shared_ptr<ServerboundSelectTradePacket> select_trade_msg =
      std::make_shared<ServerboundSelectTradePacket>();
  select_trade_msg->SetItem(trade_index);

  network_manager->Send(select_trade_msg);

  start = std::chrono::steady_clock::now();
  // Wait until the output/input is set with the correct item
  bool correct_items = false;
  do {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() > 5000) {
      LOG_WARNING("Something went wrong waiting trade selection (Timeout). "
                  "Maybe an item was missing?");
      return Status::Failure;
    }

    {
      std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
      correct_items =
          (buy && trading_container->GetSlot(2).GetItemID() == item_id) ||
          (!buy && !trading_container->GetSlot(2).IsEmptySlot() &&
           (trading_container->GetSlot(0).GetItemID() == item_id ||
            trading_container->GetSlot(1).GetItemID() == item_id));
    }
    client.Yield();
  } while (!correct_items);

  // Check we have at least one empty slot to get back input remainings +
  // outputs
  std::vector<short> empty_slots(has_trade_second_item ? 3 : 2);
  int empty_slots_index = 0;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    for (const auto &s : trading_container->GetSlots()) {
      if (s.first < trading_container->GetFirstPlayerInventorySlot()) {
        continue;
      }

      if (s.second.IsEmptySlot()) {
        empty_slots[empty_slots_index] = s.first;
        empty_slots_index++;
        if (empty_slots_index == empty_slots.size()) {
          break;
        }
      }
    }
  }
  if (empty_slots_index == 0) {
    LOG_WARNING("No free space in inventory for trading to happen.");
    return Status::Failure;
  } else if (empty_slots_index < empty_slots.size()) {
    LOG_WARNING("Not enough free space in inventory for trading. Input items "
                "may be lost");
  }

  // Copy the input slots to see when they'll change
  Slot input_slot_1, input_slot_2;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    input_slot_1 = trading_container->GetSlot(0);
    input_slot_2 = trading_container->GetSlot(1);
  }

  // Get the output in the inventory
  if (SwapItemsInContainer(client, container_id, empty_slots[0], 2) ==
      Status::Failure) {
    LOG_WARNING("Failed to swap output slot during trading attempt");
    return Status::Failure;
  }

  // Wait for the server to update the input slots
  start = std::chrono::steady_clock::now();
  while (true) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() > 5000) {
      LOG_WARNING("Something went wrong waiting trade input update (Timeout).");
      return Status::Failure;
    }

    {
      std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
      if ((input_slot_1.IsEmptySlot() ||
           input_slot_1.GetItemCount() !=
               trading_container->GetSlot(0).GetItemCount()) &&
          (input_slot_2.IsEmptySlot() ||
           input_slot_2.GetItemCount() !=
               trading_container->GetSlot(1).GetItemCount())) {
        break;
      }
    }
    client.Yield();
  }

  // Get back the input remainings in the inventory
  for (int i = 0; i < empty_slots_index - 1; ++i) {
    if (SwapItemsInContainer(client, container_id, empty_slots[i + 1], i) ==
        Status::Failure) {
      LOG_WARNING("Failed to swap slots " << i << " after trading attempt");
      return Status::Failure;
    }
  }

  // If we are here, everything is fine (or should be),
  // remove 1 to the possible trade counter on the villager
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    ProtocolCraft::Trade &trade =
        inventory_manager->GetAvailableTrade(trade_index);
    trade.SetNumberOfTradesUses(trade.GetNumberOfTradesUses() + 1);
  }

  return Status::Success;
}

Status TradeBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {"Trade.item_id", "Trade.buy",
                                                   "Trade.trade_id"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const int item_id = blackboard.Get<int>(variable_names[0]);
  const bool buy = blackboard.Get<bool>(variable_names[1]);

  // Optional
  const int trade_id = blackboard.Get<int>(variable_names[2], -1);

  return Trade(client, item_id, buy, trade_id);
}

Status TradeName(BehaviourClient &client, const std::string &item_name,
                 const bool buy, const int trade_id) {
  // Get item id corresponding to the name
  const int item_id = AssetsManager::getInstance().GetItemID(item_name);
  if (item_id < 0) {
    LOG_WARNING("Trying to trade an unknown item");
    return Status::Failure;
  }

  return Trade(client, item_id, buy, trade_id);
}

Status TradeNameBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "TradeName.item_name", "TradeName.buy", "TradeName.trade_id"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::string &item_name = blackboard.Get<std::string>(variable_names[0]);
  const bool buy = blackboard.Get<bool>(variable_names[1]);

  // Optional
  const int trade_id = blackboard.Get<int>(variable_names[2], -1);

  return TradeName(client, item_name, buy, trade_id);
}
#endif

#if PROTOCOL_VERSION < 350
Status
Craft(BehaviourClient &client,
      const std::array<std::array<std::pair<int, unsigned char>, 3>, 3> &inputs,
      const bool allow_inventory_craft)
#else
Status Craft(BehaviourClient &client,
             const std::array<std::array<int, 3>, 3> &inputs,
             const bool allow_inventory_craft)
#endif
{
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  int min_x = 3;
  int max_x = -1;
  int min_y = 3;
  int max_y = -1;
  bool use_inventory_craft = false;
  if (!allow_inventory_craft) {
    use_inventory_craft = false;
    min_x = 0;
    max_x = 2;
    min_y = 0;
    max_y = 2;
  } else {
    for (int y = 0; y < 3; ++y) {
      for (int x = 0; x < 3; ++x) {
#if PROTOCOL_VERSION < 350
        if (inputs[y][x].first != -1)
#else
        if (inputs[y][x] != -1)
#endif
        {
          min_x = std::min(x, min_x);
          max_x = std::max(x, max_x);
          min_y = std::min(y, min_y);
          max_y = std::max(y, max_y);
        }
      }
    }

    use_inventory_craft = (max_x - min_x) < 2 && (max_y - min_y) < 2;
  }

  int crafting_container_id = -1;
  // If we need a crafting table, make sure one is open
  if (!use_inventory_craft) {
    auto start = std::chrono::steady_clock::now();
    do {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count() > 5000) {
        LOG_WARNING("Something went wrong waiting craft opening (Timeout).");
        return Status::Failure;
      }
      {
        std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
        crafting_container_id = inventory_manager->GetFirstOpenedWindowId();
      }
      client.Yield();
    } while (crafting_container_id == -1);
  } else {
    crafting_container_id = Window::PLAYER_INVENTORY_INDEX;
  }

  std::shared_ptr<Window> crafting_container;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    crafting_container = inventory_manager->GetWindow(crafting_container_id);
  }

  if (!crafting_container) {
    LOG_WARNING("Something went wrong during craft (window closed).");
    return Status::Failure;
  }

  Slot output_slot_before;
  // For each input slot
  for (int y = min_y; y < max_y + 1; ++y) {
    for (int x = min_x; x < max_x + 1; ++x) {
      // Save the output slot just before the last input is set
      if (y == min_y && x == min_x) {
        std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
        output_slot_before = crafting_container->GetSlot(0);
      }

      const int destination_slot = use_inventory_craft
                                       ? (1 + x - min_x + (y - min_y) * 2)
                                       : (1 + x + 3 * y);

      int source_slot = -1;
      int source_quantity = -1;
      // Search for the required item in inventory
      {
        std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
        for (const auto &s : crafting_container->GetSlots()) {
          if (s.first < crafting_container->GetFirstPlayerInventorySlot()) {
            continue;
          }
#if PROTOCOL_VERSION < 350
          if (s.second.GetBlockID() == inputs[y][x].first &&
              s.second.GetItemDamage() == inputs[y][x].second)
#else
          if (s.second.GetItemID() == inputs[y][x])
#endif
          {
            source_slot = s.first;
            source_quantity = s.second.GetItemCount();
            break;
          }
        }
      }

      if (source_slot == -1) {
#if PROTOCOL_VERSION < 350
        LOG_WARNING("Not enough source item ["
                    << AssetsManager::getInstance()
                           .Items()
                           .at(inputs[y][x].first)
                           .at(inputs[y][x].second)
                           ->GetName()
                    << "] found in inventory for crafting.");
#else
        LOG_WARNING(
            "Not enough source item ["
            << AssetsManager::getInstance().Items().at(inputs[y][x])->GetName()
            << "] found in inventory for crafting.");
#endif
        return Status::Failure;
      }

      if (ClickSlotInContainer(client, crafting_container_id, source_slot, 0,
                               0) == Status::Failure) {
#if PROTOCOL_VERSION < 350
        LOG_WARNING("Error trying to pick source item ["
                    << AssetsManager::getInstance()
                           .Items()
                           .at(inputs[y][x].first)
                           .at(inputs[y][x].second)
                           ->GetName()
                    << "] during crafting");
#else
        LOG_WARNING(
            "Error trying to pick source item ["
            << AssetsManager::getInstance().Items().at(inputs[y][x])->GetName()
            << "] during crafting");
#endif
        return Status::Failure;
      }

      // Right click in the destination slot
      if (ClickSlotInContainer(client, crafting_container_id, destination_slot,
                               0, 1) == Status::Failure) {
#if PROTOCOL_VERSION < 350
        LOG_WARNING("Error trying to place source item ["
                    << AssetsManager::getInstance()
                           .Items()
                           .at(inputs[y][x].first)
                           .at(inputs[y][x].second)
                           ->GetName()
                    << "] during crafting");
#else
        LOG_WARNING(
            "Error trying to place source item ["
            << AssetsManager::getInstance().Items().at(inputs[y][x])->GetName()
            << "] during crafting");
#endif
        return Status::Failure;
      }

      // Put back the remaining items in the origin slot
      if (source_quantity > 1) {
        if (ClickSlotInContainer(client, crafting_container_id, source_slot, 0,
                                 0) == Status::Failure) {
#if PROTOCOL_VERSION < 350
          LOG_WARNING("Error trying to place back source item ["
                      << AssetsManager::getInstance()
                             .Items()
                             .at(inputs[y][x].first)
                             .at(inputs[y][x].second)
                             ->GetName()
                      << "] during crafting");
#else
          LOG_WARNING("Error trying to place back source item ["
                      << AssetsManager::getInstance()
                             .Items()
                             .at(inputs[y][x])
                             ->GetName()
                      << "] during crafting");
#endif
          return Status::Failure;
        }
      }
    }
  }

  // Wait for the server to send the output change
  // TODO: with the recipe book, we could know without waiting
  auto start = std::chrono::steady_clock::now();
  while (true) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count() > 5000) {
      LOG_WARNING(
          "Something went wrong waiting craft output update (Timeout).");
      return Status::Failure;
    }
    {
      std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
      if (!crafting_container->GetSlot(0).SameItem(output_slot_before)) {
        break;
      }
    }
    client.Yield();
  }

  // All inputs are in place, output is ready, click on output
  if (ClickSlotInContainer(client, crafting_container_id, 0, 0, 0) ==
      Status::Failure) {
    LOG_WARNING("Error trying to click on output during crafting");
    return Status::Failure;
  }

  // Find an empty slot in inventory to place the cursor content
  int destination_slot = -999;
  {
    std::lock_guard<std::mutex> lock(inventory_manager->GetMutex());
    for (const auto &s : crafting_container->GetSlots()) {
      if (s.first < (use_inventory_craft
                         ? Window::INVENTORY_STORAGE_START
                         : crafting_container->GetFirstPlayerInventorySlot())) {
        continue;
      }

      // If it fits in a slot (empty or with the same item)
      if (s.second.IsEmptySlot() ||
#if PROTOCOL_VERSION < 347
          (inventory_manager->GetCursor().GetBlockID() ==
               s.second.GetBlockID() &&
           inventory_manager->GetCursor().GetItemDamage() ==
               s.second.GetItemDamage() &&
           s.second.GetItemCount() <
               AssetsManager::getInstance()
                       .Items()
                       .at(s.second.GetBlockID())
                       .at(static_cast<unsigned char>(s.second.GetItemDamage()))
                       ->GetStackSize() -
                   1)
#else
          (inventory_manager->GetCursor().GetItemID() == s.second.GetItemID() &&
           s.second.GetItemCount() < AssetsManager::getInstance()
                                             .Items()
                                             .at(s.second.GetItemID())
                                             ->GetStackSize() -
                                         1)
#endif
      ) {
        destination_slot = s.first;
        break;
      }
    }
  }

  if (destination_slot == -999) {
    LOG_INFO("No available space for crafted item, will be thrown out");
  }

  if (ClickSlotInContainer(client, crafting_container_id, destination_slot, 0,
                           0) == Status::Failure) {
    LOG_WARNING("Error trying to put back output during crafting");
    return Status::Failure;
  }

  return Status::Success;
}

Status CraftBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "Craft.inputs", "Craft.allow_inventory_craft"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
#if PROTOCOL_VERSION < 350
  const std::array<std::array<std::pair<int, unsigned char>, 3>, 3> &inputs =
      blackboard
          .Get<std::array<std::array<std::pair<int, unsigned char>, 3>, 3>>(
              variable_names[0]);
#else
  const std::array<std::array<int, 3>, 3> &inputs =
      blackboard.Get<std::array<std::array<int, 3>, 3>>(variable_names[0]);
#endif

  // Optional
  const bool allow_inventory_craft =
      blackboard.Get<bool>(variable_names[1], true);

  return Craft(client, inputs, allow_inventory_craft);
}

Status CraftNamed(BehaviourClient &client,
                  const std::array<std::array<std::string, 3>, 3> &inputs,
                  const bool allow_inventory_craft) {
  const AssetsManager &assets_manager = AssetsManager::getInstance();
#if PROTOCOL_VERSION < 350
  std::array<std::array<std::pair<int, unsigned char>, 3>, 3> inputs_ids;
#else
  std::array<std::array<int, 3>, 3> inputs_ids;
#endif
  for (size_t i = 0; i < 3; ++i) {
    for (size_t j = 0; j < 3; ++j) {
#if PROTOCOL_VERSION < 350
      inputs_ids[i][j] = inputs[i][j] == ""
                             ? std::pair<int, unsigned char>{-1, 0}
                             : assets_manager.GetItemID(inputs[i][j]);
#else
      inputs_ids[i][j] =
          inputs[i][j] == "" ? -1 : assets_manager.GetItemID(inputs[i][j]);
#endif
    }
  }
  return Craft(client, inputs_ids, allow_inventory_craft);
}

Status CraftNamedBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "CraftNamed.inputs", "CraftNamed.allow_inventory_craft"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::array<std::array<std::string, 3>, 3> &inputs =
      blackboard.Get<std::array<std::array<std::string, 3>, 3>>(
          variable_names[0]);

  // Optional
  const bool allow_inventory_craft =
      blackboard.Get<bool>(variable_names[1], true);

  return CraftNamed(client, inputs, allow_inventory_craft);
}

Status HasItemInInventory(BehaviourClient &client, const std::string &item_name,
                          const int quantity) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();

  const auto item_id = AssetsManager::getInstance().GetItemID(item_name);

  int quantity_sum = 0;
  {
    std::lock_guard<std::mutex> lock_inventory_manager(
        inventory_manager->GetMutex());
    for (const auto &s : inventory_manager->GetPlayerInventory()->GetSlots()) {
      if (s.first < Window::INVENTORY_STORAGE_START) {
        continue;
      }

      if (!s.second.IsEmptySlot() &&
#if PROTOCOL_VERSION < 350
          s.second.GetBlockID() == item_id.first &&
          s.second.GetItemDamage() == item_id.second
#else
          s.second.GetItemID() == item_id
#endif
      ) {
        quantity_sum += s.second.GetItemCount();
      }

      if (quantity_sum >= quantity) {
        return Status::Success;
      }
    }
  }

  return Status::Failure;
}

Status HasItemInInventoryBlackboard(BehaviourClient &client) {
  const std::vector<std::string> variable_names = {
      "HasItemInInventory.item_name", "HasItemInInventory.quantity"};

  Blackboard &blackboard = client.GetBlackboard();

  // Mandatory
  const std::string &item_name = blackboard.Get<std::string>(variable_names[0]);

  // Optional
  const int quantity = blackboard.Get<int>(variable_names[1], 1);

  return HasItemInInventory(client, item_name, quantity);
}

Status SortInventory(BehaviourClient &client) {
  std::shared_ptr<InventoryManager> inventory_manager =
      client.GetInventoryManager();
  std::shared_ptr<Window> player_inventory =
      inventory_manager->GetPlayerInventory();

  while (true) {
    short src_index = -1;
    short dst_index = -1;
    {
      std::lock_guard<std::mutex> inventory_manager_lock(
          inventory_manager->GetMutex());
      for (short i = Window::INVENTORY_OFFHAND_INDEX;
           i > Window::INVENTORY_STORAGE_START; --i) {
        const Slot &dst_slot = player_inventory->GetSlot(i);
        if (dst_slot.IsEmptySlot()) {
          continue;
        }
        // If this slot is not empty, and not full,
        // check if "lower" slot with same items that
        // could fit in it
#if PROTOCOL_VERSION < 350
        const int available_space =
            AssetsManager::getInstance()
                .Items()
                .at(dst_slot.GetBlockID())
                .at(static_cast<unsigned char>(dst_slot.GetItemDamage()))
                ->GetStackSize() -
            dst_slot.GetItemCount();
#else
        const int available_space = AssetsManager::getInstance()
                                        .Items()
                                        .at(dst_slot.GetItemID())
                                        ->GetStackSize() -
                                    dst_slot.GetItemCount();
#endif
        if (available_space == 0) {
          continue;
        }

        for (short j = Window::INVENTORY_STORAGE_START; j < i; ++j) {
          const Slot &src_slot = player_inventory->GetSlot(j);
          if (!src_slot.IsEmptySlot() && dst_slot.SameItem(src_slot) &&
              src_slot.GetItemCount() <= available_space) {
            src_index = j;
            break;
          }
        }

        if (src_index != -1) {
          dst_index = i;
          break;
        }
      }
    }

    // Nothing to do
    if (src_index == -1 && dst_index == -1) {
      break;
    }

    // Pick slot src, put it in dst
    if (ClickSlotInContainer(client, Window::PLAYER_INVENTORY_INDEX, src_index,
                             0, 0) == Status::Failure) {
      LOG_WARNING("Error trying to pick up slot during inventory sorting");
      return Status::Failure;
    }

    if (ClickSlotInContainer(client, Window::PLAYER_INVENTORY_INDEX, dst_index,
                             0, 0) == Status::Failure) {
      LOG_WARNING("Error trying to put down slot during inventory sorting");
      return Status::Failure;
    }
  }

  return Status::Success;
}
} // namespace Botcraft
