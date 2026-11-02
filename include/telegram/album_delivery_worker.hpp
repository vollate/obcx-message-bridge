#pragma once
#include "telegram/media_group_buffer.hpp"
#include "telegram/ordinary_forwarder.hpp"

namespace bridge::telegram {
// Admission occurs while the generation is active. Each accepted batch owns
// its timer/flush/send/completion drain lease; stop flushes accepted work and
// refuses new members, preserving the existing shutdown flush contract.
class AlbumDeliveryWorker final
    : public std::enable_shared_from_this<AlbumDeliveryWorker> {
public:
  static auto prepare(
      boost::asio::any_io_executor executor,
      std::shared_ptr<OrdinaryForwarder> forwarder,
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<obcx::core::ActorGenerationLifecycle> lifecycle,
      std::shared_ptr<obcx::core::ActorEventPublisher> publisher)
      -> std::shared_ptr<AlbumDeliveryWorker>;
  void enqueue(BufferedTelegramMessage message);
  void stop() noexcept;

private:
  using Lifecycle = obcx::core::ActorGenerationLifecycle;
  AlbumDeliveryWorker(
      boost::asio::any_io_executor executor,
      std::shared_ptr<OrdinaryForwarder> forwarder,
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<Lifecycle> lifecycle,
      std::shared_ptr<obcx::core::ActorEventPublisher> publisher);
  void start(Lifecycle::TokenPtr token) noexcept;
  static auto flush(std::shared_ptr<AlbumDeliveryWorker> self,
                    std::vector<BufferedTelegramMessage> messages,
                    Lifecycle::TokenPtr token) -> boost::asio::awaitable<void>;
  boost::asio::any_io_executor executor_;
  std::shared_ptr<TGMediaGroupBuffer> buffer_;
  std::shared_ptr<OrdinaryForwarder> forwarder_;
  std::shared_ptr<BridgeCommandOperations> operations_;
  std::shared_ptr<const obcx::bot::ConversationResolver> references_;
  std::shared_ptr<Lifecycle> lifecycle_;
  std::shared_ptr<obcx::core::ActorEventPublisher> publisher_;
  std::mutex mutex_;
  Lifecycle::TokenPtr token_;
};
} // namespace bridge::telegram
