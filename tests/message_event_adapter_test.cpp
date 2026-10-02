#include "bridge_message_event_adapter.hpp"

#include <gtest/gtest.h>

TEST(BridgeMessageEventAdapterTest,
     RebuildsTextSegmentFromStoredPayloadWhenRawMessageIsSparse) {
  obcx::core::MessageEnvelope envelope;
  envelope.id = "stored-tg-sparse";
  envelope.type = "obcx::message_store::events::MessageStored";
  envelope.source_platform = "telegram";
  envelope.source_bot = "tg-main";
  envelope.payload = {
      {"message_id", "tg-7"},
      {"sender", "user-9"},
      {"group_id", "chat-5"},
      {"message_type", "group"},
      {"payload", {{"text", "hello from payload"}}},
  };

  const auto event = bridge::message_event_from_message_stored(envelope);

  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->message_id, "tg-7");
  EXPECT_EQ(event->user_id, "user-9");
  ASSERT_TRUE(event->group_id.has_value());
  EXPECT_EQ(event->group_id.value(), "chat-5");
  EXPECT_EQ(event->message_type, "group");
  EXPECT_EQ(event->raw_message, "hello from payload");
  ASSERT_EQ(event->message.size(), 1);
  EXPECT_EQ(event->message.front().type, "text");
  EXPECT_EQ(event->message.front().data["text"], "hello from payload");
}
