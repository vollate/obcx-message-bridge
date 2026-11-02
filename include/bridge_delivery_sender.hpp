#pragma once

#include "bridge_configured_scope.hpp"
#include "bridge_delivery_ledger.hpp"
#include "bridge_message_identity.hpp"
#include <core/actor/actor_event_publisher.hpp>
#include <core/actor/blocking_executor.hpp>
#include <core/bot/bot_client.hpp>
#include <core/bot/domain_events.hpp>
#include <memory>
#include <onebot11/bot/types.hpp>
#include <stdexcept>
#include <telegram/bot/domain_operations.hpp>
#include <utility>

namespace bridge {
class BridgeStateRepository;
class BridgeCommandOperations;

// Prepared by the forwarding owner from admitted observations and the original
// configuration mapping. Structural references never renew live authority.
struct BridgeDeliveryMember {
  std::string member_id;
  obcx::bot::MessageRef message;
  obcx::bot::MessageReceived source;
  std::optional<obcx::bot::MediaGroupRef> media_group;
  std::vector<std::string> input_blocks;
};
enum class BridgeQuoteAnchor { None, FirstConfirmed };
// The existing observed-batch behavior, not a new configurable late-item
// choice. Any overlapping member suppresses that batch; a disjoint observed
// batch is not a suffix retry. Unproven contribution is never manufactured.
enum class BridgeAlbumPolicy { PreserveObservedBatch };
struct BridgeDeliveryRetryPolicy {
  std::int64_t base_interval_sec;
  std::int64_t max_interval_sec;
  void validate() const;
};
struct BridgeRetryRoute {
  std::string pair_id;
  obcx::bot::ConversationRef source;
  obcx::bot::ConversationRef target;
};
struct BridgeDeliveryPublication {
  std::string event_id;
  std::string correlation_id;
  std::string source_bot;
};
struct BridgeDeliveryRequest {
  std::string delivery_id;
  std::string attempt_id;
  std::string pair_id;
  std::vector<BridgeDeliveryMember> members;
  obcx::bot::SendMessageRequest send;
  // One entry per prepared body block. Unknown stays nullopt. These are owner
  // supplied source contributions, not guesses based on output count/order.
  std::vector<std::optional<std::vector<ledger::Provenance>>> block_origins;
  BridgeQuoteAnchor quote_anchor;
  std::int64_t admitted_at_ms;
  std::int64_t max_attempts;
  std::optional<BridgeAlbumPolicy> album_policy;
  std::optional<BridgeDeliveryRetryPolicy> retry_policy;
  BridgeDeliveryPublication publication;
  std::vector<ledger::AlbumObservation> album_observations;
};
struct BridgeDeliveryResult {
  ledger::DeliverySnapshot persisted;
  std::optional<obcx::bot::SendOutcome> outcome;
  // Only the caller that persisted a complete terminal result may publish.
  // Existing/admitted/physical-confirmation observations never set this flag.
  bool newly_completed;
};

// Uses persisted original event/correlation identity and all physical outputs.
// Only a newly persisted complete outcome may produce a logical notification.
auto bridge_delivery_notification(const BridgeDeliveryResult &result)
    -> obcx::core::MessageEnvelope;
auto publish_bridge_delivery(
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher,
    obcx::core::ActorGenerationLifecycle::TokenPtr token,
    BridgeDeliveryResult result) -> boost::asio::awaitable<bool>;

class BridgeAlbumBatchSuppressed final : public std::runtime_error {
public:
  BridgeAlbumBatchSuppressed()
      : std::runtime_error(
            "album batch overlaps existing source reservations") {}
};

class BridgeDeliveryPersistenceFailure final : public std::runtime_error {
public:
  BridgeDeliveryPersistenceFailure(
      std::string delivery_id, obcx::bot::SendOutcome outcome,
      obcx::core::MessageEnvelope notification_context)
      : std::runtime_error(
            "delivery outcome persistence failed; do not resubmit"),
        delivery_id_(std::move(delivery_id)), outcome_(std::move(outcome)),
        notification_context_(std::move(notification_context)) {}
  [[nodiscard]] auto delivery_id() const noexcept -> const std::string & {
    return delivery_id_;
  }
  [[nodiscard]] auto outcome() const noexcept
      -> const obcx::bot::SendOutcome & {
    return outcome_;
  }

  [[nodiscard]] auto notification_context() const noexcept
      -> const obcx::core::MessageEnvelope & {
    return notification_context_;
  }

private:
  std::string delivery_id_;
  obcx::bot::SendOutcome outcome_;
  obcx::core::MessageEnvelope notification_context_;
};

namespace detail {
// Prepare only original identity/trace metadata before invoking the provider.
// Never retain the outbound body, archived payload or private media locators.
auto prepare_delivery_failure_notification(const ledger::Admission &admission,
                                           const std::string &attempt_id)
    -> obcx::core::MessageEnvelope;
} // namespace detail
// Observed physical evidence, not a committed outcome or retry permission.
auto bridge_delivery_failure_notification(
    const BridgeDeliveryPersistenceFailure &failure)
    -> obcx::core::MessageEnvelope;

// First submission only. Replayed admission never invokes the provider.
// Retry-enabled sends persist their owning typed payload before the attempt.
// Paths/URLs without a durable owner are never replayed as if they were bytes.
auto send_bridge_delivery(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::shared_ptr<const obcx::bot::ConversationResolver> references,
    BridgeDeliveryRequest request)
    -> boost::asio::awaitable<BridgeDeliveryResult>;

// A scheduler supplies a currently permitted full source/target route. The
// transaction rechecks the exact snapshot/payload, due time and zero effects.
// This does not import or authorize unbound legacy retries.
auto retry_bridge_delivery(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::string delivery_id, BridgeRetryRoute route,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeDeliveryResult>;

// Blocking, pure ledger lookup. Never queries provider history or treats an
// unavailable/ambiguous mapping as permission to submit. Exact scopes include
// topics; historical unbound rows cannot manufacture a quote target.
// True only for an exact persisted physical output. No source-primary or
// complete-delivery requirement: partial and confirmation-only outputs count.
// Unknown/corrupt/ambiguous evidence is never permission to send again.
auto is_bridge_delivery_output(BridgeStateRepository &repository,
                               const std::string &pair_id,
                               const obcx::bot::MessageRef &message) -> bool;
// Reverse-only lookup for original-source attribution. Missing means no
// observed output in this pair; a known output with unknown ownership or a
// different original source scope fails closed, never falls back to v3 rows.
auto resolve_bridge_delivery_source(
    BridgeStateRepository &repository, const std::string &pair_id,
    const obcx::bot::ConversationRef &original_scope,
    const obcx::bot::ReplyInfo &reply) -> std::optional<obcx::bot::MessageRef>;
auto resolve_bridge_delivery_reply(BridgeStateRepository &repository,
                                   const std::string &pair_id,
                                   const obcx::bot::ConversationRef &target,
                                   const obcx::bot::ReplyInfo &reply)
    -> std::optional<obcx::bot::MessageRef>;
auto bridge_delivery_output_messages(const ledger::DeliverySnapshot &snapshot)
    -> std::vector<obcx::bot::MessageRef>;
// Pure member-level delete/edit selection, not authorization. Unknown
// provenance or an output shared with the requested member fails the whole
// plan before any effects. Returns every proven exclusive output, not an
// anchor; caller must still admit mutations durably and retain each result.
auto bridge_delivery_member_outputs(const ledger::DeliverySnapshot &snapshot,
                                    const obcx::bot::MessageRef &source)
    -> std::vector<obcx::bot::MessageRef>;
auto bridge_delivery_quote_anchor(const ledger::DeliverySnapshot &snapshot)
    -> std::optional<obcx::bot::MessageRef>;
// Includes historical/revision physical observations without treating them as
// current content. Member selection separately checks the latest revision.
auto bridge_delivery_find_output(const ledger::DeliverySnapshot &snapshot,
                                 const obcx::bot::MessageRef &message)
    -> const ledger::PhysicalOutput &;
auto bridge_delivery_member_quote_anchor(
    const ledger::DeliverySnapshot &snapshot,
    const obcx::bot::MessageRef &source)
    -> std::optional<obcx::bot::MessageRef>;

struct BridgeRecallTarget {
  obcx::bot::MessageRef message;
  std::optional<obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>>
      outcome;
  bool persisted;
};
struct BridgeRecallResult {
  std::vector<BridgeRecallTarget> targets;
  bool replayed;
  bool interrupted;
  bool original_source;
  // Cancellation may arrive as an exception without setting the Asio slot.
  // Carry it alongside owned receipts, after journal settlement.
  bool cancelled = false;
};
// TG source -> QQ physical outputs, or confirmed TG output -> QQ original
// source. Distinct journals/target roles; nullopt means no v4 binding. Known
// reverse/source ownership must never be silently replaced with a legacy row.
// All selection/preparation precedes effects; a stable first-operation fence
// blocks command replay/suffix continuation. Results remain owned on failure.
auto recall_bridge_delivery(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    obcx::bot::ReplyInfo reply, std::string qq_group_id)
    -> boost::asio::awaitable<std::optional<BridgeRecallResult>>;

using BridgeRecallPropagationOutcome = std::variant<
    obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>,
    obcx::bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>>;
struct BridgeRecallPropagationTarget {
  obcx::bot::MessageRef message;
  std::optional<BridgeRecallPropagationOutcome> outcome;
  bool persisted;
  bool matched_receipt;
};
struct BridgeRecallPropagationResult {
  std::vector<BridgeRecallPropagationTarget> targets;
  bool replayed;
  bool interrupted;
  bool completeness_known;
};
class BridgeRecallPropagationFailure final : public std::runtime_error {
public:
  BridgeRecallPropagationFailure(
      std::string diagnostic,
      std::optional<BridgeRecallPropagationResult> result)
      : std::runtime_error(std::move(diagnostic)), result_(std::move(result)) {}
  [[nodiscard]] auto result() const noexcept
      -> const std::optional<BridgeRecallPropagationResult> & {
    return result_;
  }

private:
  std::optional<BridgeRecallPropagationResult> result_;
};
// QQ deletion observation -> all proven exclusive Telegram outputs. The
// source-only lookup recovers stored target scopes, never current-config topic
// guesses. Only fresh durable admission permits an edit/delete. Repeated
// notices (including different receive times) cannot resume a suffix.
auto propagate_bridge_recall(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    obcx::bot::MessageDeleted deleted, bool delete_outputs,
    obcx::telegram::bot::FormattedText replacement)
    -> boost::asio::awaitable<std::optional<BridgeRecallPropagationResult>>;

struct BridgeReactionTarget {
  obcx::bot::MessageRef message;
  std::optional<obcx::bot::BotOperationResult<
      obcx::onebot11::bot::OneBotMessageEmojiLikeResult>>
      outcome;
  bool persisted;
  bool matched_receipt;
};
struct BridgeReactionResult {
  std::vector<BridgeReactionTarget> targets;
  bool replayed;
  bool interrupted;
  bool original_source;
  bool command_persisted;
  bool cancelled = false;
};
// Current typed source/output selection, independent command fence and full
// reaction results. Missing durable ownership never falls back to old maps.
auto react_bridge_delivery(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    obcx::bot::ReplyInfo reply, std::string qq_group_id, std::string emoji_id,
    bool set) -> boost::asio::awaitable<std::optional<BridgeReactionResult>>;

// Pure namespace check; correlation is routing evidence, not authorization.
auto is_bridge_delivery_confirmation(const obcx::bot::SendConfirmation &)
    -> bool;
// Blocking repository path: no forwarding, provider call or success
// publication. False means another sender or an unknown delivery, not
// permission to submit.
auto record_bridge_delivery_confirmation(BridgeStateRepository &,
                                         const obcx::bot::SendConfirmation &)
    -> bool;
} // namespace bridge
