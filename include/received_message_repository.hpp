#pragma once

#include <core/bot/bot_events.hpp>
#include <core/infrastructure/db_manager.hpp>

#include <optional>
#include <string>

namespace bridge {

// Projection for recall and reply commands, not a restored media capability.
struct ReceivedMessageFacts {
  std::optional<obcx::bot::UserRef> sender;
  bool has_image;
};

class ReceivedMessageRepository {
public:
  ReceivedMessageRepository(obcx::core::DbManager &db_manager,
                            std::string db_instance, std::string db_namespace);

  auto get_message(const obcx::bot::MessageRef &message) const
      -> std::optional<ReceivedMessageFacts>;

private:
  obcx::core::DbManager &db_manager_;
  std::string db_instance_;
  std::string db_namespace_;
};

} // namespace bridge
