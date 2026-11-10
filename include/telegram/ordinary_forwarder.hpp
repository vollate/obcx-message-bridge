#pragma once

#include "bridge_forwarder.hpp"
#include "config.hpp"

namespace bridge::telegram {
class TelegramMediaProcessor;

// Typed forwarding owner. Albums and edits select their own admission path;
// specialized media select their preparer before submission. No raw fallback.
class OrdinaryForwarder final {
public:
  OrdinaryForwarder(
      std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<BridgeStateRepository> repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking,
      const BridgeInstallationPair &pair,
      std::shared_ptr<const BridgeConfig> config);
  [[nodiscard]] static auto accepts(const obcx::bot::MessageReceived &source)
      -> bool;
  auto forward(obcx::message_store::events::MessageStored stored,
               BridgeDeliveryPublication publication)
      -> boost::asio::awaitable<BridgeForwardResult>;
  auto forward_edit(obcx::message_store::events::MessageStored stored,
                    BridgeDeliveryPublication publication)
      -> boost::asio::awaitable<BridgeForwardResult>;
  struct PreparedEdit {
    BridgeDeliveryRequest request;
    ledger::RevisionAdmission admission;
  };
  using EditPreparation =
      std::variant<std::monostate, ledger::RevisionSnapshot, PreparedEdit>;
  // Prepares the original typed update, retaining all native positions. No
  // deletion/submission happens here, and an archive never becomes live input.
  auto prepare_edit(obcx::message_store::events::MessageStored stored,
                    BridgeDeliveryPublication publication)
      -> boost::asio::awaitable<EditPreparation>;
  // A flush is an observed batch, never proof of complete native membership.
  // The caller must choose late-item policy; no policy/config default exists.
  auto forward_album(
      std::vector<obcx::message_store::events::MessageStored> stored,
      std::vector<BridgeDeliveryPublication> publications)
      -> boost::asio::awaitable<BridgeForwardResult>;

private:
  struct Route {
    std::string group;
    std::optional<std::string> topic;
    bool all_topics;
    obcx::bot::ConversationRef target;
    bool show_sender;
  };
  struct ResolvedSource {
    Route route;
    std::optional<std::string> observed_sender_name;
    bool genuine_reply;
  };
  struct PreparedMember {
    BridgeDeliveryMember member;
    obcx::bot::MessageBody body;
    std::vector<std::optional<std::vector<ledger::Provenance>>> origins;
    std::optional<obcx::bot::MessageRef> reply;
    std::size_t prepared_bytes;
    std::vector<std::pair<obcx::bot::RichText, std::vector<ledger::Provenance>>>
        trailing_captions;
  };
  // Pure archive/configuration validation, also usable for album/edit sources.
  // Restoration never replaces the original observation used for acquisition.
  auto resolve_source(const obcx::message_store::events::MessageStored &stored)
      const -> std::optional<ResolvedSource>;
  // Ordinary messages/edits remain strict. Albums retain failed inputs and
  // captions but may omit unavailable attachments, matching the predecessor.
  enum class AttachmentFailurePolicy { FailClosed, OmitUnavailable };
  auto prepare_member(const obcx::message_store::events::MessageStored &stored,
                      ResolvedSource resolved, std::string member_id,
                      bool include_context, bool defer_captions,
                      bool allow_edit_marker, obcx::bot::SendLimits limits,
                      AttachmentFailurePolicy attachment_failure_policy)
      -> boost::asio::awaitable<PreparedMember>;
  // Only exact SDK markers paired with their original typed media are handled.
  // Unknown markers and detached/contradictory special-media markers fail
  // closed.
  [[nodiscard]] static auto handled_media_marker(
      const obcx::bot::MessageReceived &message, std::size_t index) -> bool;
  [[nodiscard]] static auto literal_entity_marker(
      const obcx::bot::UnsupportedContent &marker) -> bool;
  [[nodiscard]] static auto accepts_body(
      const obcx::bot::MessageReceived &message, bool allow_edit_marker)
      -> bool;
  static void check_media_capabilities(
      const obcx::bot::MessageReceived &message,
      const obcx::bot::ConversationCapabilities &capabilities);
  auto display_name(obcx::bot::UserRef user, obcx::bot::ConversationRef source)
      -> boost::asio::awaitable<std::optional<std::string>>;
  auto render_text(obcx::bot::ReceivedRichText text,
                   obcx::bot::ConversationRef source)
      -> boost::asio::awaitable<obcx::bot::RichText>;

  std::shared_ptr<obcx::bot::BotOperationGateway> gateway_;
  std::shared_ptr<const obcx::bot::ConversationResolver> references_;
  std::shared_ptr<BridgeStateRepository> repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_;
  std::shared_ptr<const BridgeConfig> config_;
  std::shared_ptr<TelegramMediaProcessor> media_processor_;
  obcx::bot::BotInstallationRef installation_;
  std::string pair_id_;
  std::vector<Route> routes_;
};
} // namespace bridge::telegram
