#include "bridge_bot_failures.hpp"
#include "bridge_hash.hpp"
#include "bridge_state_repository.hpp"
#include "telegram/ordinary_forwarder.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>

namespace bridge::telegram {
namespace {
namespace domain = obcx::bot;
template <class T> auto require(domain::BotOperationResult<T> value) -> T {
  value.validate();
  if (!value.ok())
    throw BridgeBotOperationFailure(std::move(*value.error));
  return std::move(*value.value);
}
template <class T>
auto encode(std::string name, const T &value) -> ledger::OpaqueValue {
  auto bytes = domain::Json::to_cbor(domain::Json(value));
  return {
      "obcx.bot." + std::move(name) + ".cbor", 1, {bytes.begin(), bytes.end()}};
}
} // namespace

auto OrdinaryForwarder::forward_album(
    std::vector<obcx::message_store::events::MessageStored> stored,
    std::vector<BridgeDeliveryPublication> publications)
    -> boost::asio::awaitable<BridgeForwardResult> {
  if (stored.empty() || stored.size() != publications.size() ||
      stored.size() > domain::detail::domain_maximum_items)
    throw std::invalid_argument("invalid Telegram album batch or policy");
  const auto group = stored.front().message.media_group;
  if (!group)
    throw std::invalid_argument("Telegram album lacks original group evidence");
  std::set<std::string> members;
  for (const auto &item : stored) {
    item.message.validate();
    item.source.validate(item.message);
    if (item.message.media_group != group ||
        !members.insert(domain::Json(item.message.message).dump()).second)
      throw std::invalid_argument("Telegram album mixed or duplicate source");
  }

  std::vector<ResolvedSource> sources;
  sources.reserve(stored.size());
  for (const auto &item : stored) {
    const auto &publication = publications[sources.size()];
    if (publication.event_id.empty() ||
        publication.source_bot != item.source.source_bot)
      throw std::invalid_argument("album publication source mismatch");
    auto resolved = resolve_source(item);
    if (!resolved)
      co_return BridgeForwardResult{};
    if (!sources.empty() &&
        (resolved->route.target != sources.front().route.target ||
         resolved->route.show_sender != sources.front().route.show_sender))
      throw std::invalid_argument("Telegram album target route mismatch");
    sources.push_back(std::move(*resolved));
  }
  // Unmatched batches remain no-ops, just like ordinary messages. Validate
  // presentation only after every member has a matching, consistent route.
  for (const auto &item : stored)
    if (!accepts_body(item.message, false))
      throw std::invalid_argument(
          "Telegram album contains unsupported content");
  const auto route = sources.front().route;
  const auto source = group->conversation;
  std::vector<domain::MessageRef> refs;
  std::vector<ledger::AlbumObservation> observations;
  for (const auto &item : stored) {
    refs.push_back(item.message.message);
    const auto &publication = publications[observations.size()];
    observations.push_back({encode("MessageRef", item.message.message),
                            encode("MediaGroupRef", *group),
                            publication.event_id, publication.correlation_id,
                            publication.source_bot});
  }
  auto eligible = co_await blocking_->run(
      [repository = repository_, pair = pair_id_, source, observations,
       target = route.target, refs = std::move(refs)] {
        std::vector<std::size_t> unseen;
        const auto source_scope = encode("ConversationRef", source);
        const auto target_scope = encode("ConversationRef", target);
        bool echo = false;
        for (std::size_t i = 0; i < refs.size(); ++i) {
          if (is_bridge_delivery_output(*repository, pair, refs[i])) {
            echo = true;
            continue;
          }
          unseen.push_back(i);
        }
        if (echo)
          return std::vector<std::size_t>{};
        // One transaction classifies all members and records suppressed
        // associations before media reads. Preparation races are rechecked
        // by the final album admission transaction.
        if (repository->observe_album_batch(pair, source_scope, target_scope,
                                            observations))
          return std::vector<std::size_t>{};
        return unseen;
      });
  if (eligible.empty())
    co_return BridgeForwardResult{};

  const auto capabilities = require(
      co_await domain::BotClient{*gateway_}.capabilities({route.target}));
  if (capabilities.conversation != route.target)
    throw std::runtime_error("album capability scope mismatch");
  std::size_t media_count = 0;
  std::size_t blocks = 0;
  auto identity = domain::Json::array();
  for (const auto index : eligible) {
    const auto &item = stored[index];
    check_media_capabilities(item.message, capabilities);
    if (item.message.body.size() >
        domain::detail::domain_maximum_items - blocks)
      throw std::invalid_argument("album input block bound exceeded");
    blocks += item.message.body.size();
    media_count += static_cast<std::size_t>(
        std::ranges::count_if(item.message.body, [](const auto &block) {
          return std::holds_alternative<domain::ReceivedMedia>(block);
        }));
    identity.push_back(domain::Json::array(
        {item.source.source_platform, item.source.source_bot,
         item.source.conversation_id, item.source.message_id,
         domain::Json(item.message.message)}));
  }
  domain::SendLimits limits{1, 0, 0};
  if (media_count) {
    const auto item = config_->qq_media_download_max_bytes;
    if (!item || item > domain::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid configured album byte budget");
    limits = {1, item,
              media_count > domain::detail::domain_maximum_bytes / item
                  ? domain::detail::domain_maximum_bytes
                  : media_count * item};
  }
  const auto digest =
      calculate_hash(domain::Json::array({pair_id_, domain::Json(*group),
                                          domain::Json(route.target), identity})
                         .dump());
  if (digest.empty())
    throw std::runtime_error("album delivery identity hashing failed");
  const auto delivery_id = "tg-album-v1:" + digest;
  std::vector<BridgeDeliveryMember> prepared_members;
  domain::MessageBody body;
  std::vector<std::optional<std::vector<ledger::Provenance>>> origins;
  std::vector<std::pair<domain::RichText, std::vector<ledger::Provenance>>>
      captions;
  std::optional<domain::MessageRef> reply;
  std::size_t bytes = 0;
  for (std::size_t position = 0; position < eligible.size(); ++position) {
    const auto index = eligible[position];
    const auto remaining = limits.maximum_media_total_bytes - bytes;
    const domain::SendLimits member_limits{
        1, std::min(limits.maximum_media_item_bytes, remaining), remaining};
    auto prepared =
        co_await prepare_member(stored[index], std::move(sources[index]),
                                "member:" + std::to_string(position),
                                position == 0, true, false, member_limits);
    if (prepared.prepared_bytes > remaining)
      throw std::runtime_error("album preparation exceeded total byte budget");
    bytes += prepared.prepared_bytes;
    if (position == 0)
      reply = std::move(prepared.reply);
    prepared_members.push_back(std::move(prepared.member));
    body.insert(body.end(), std::make_move_iterator(prepared.body.begin()),
                std::make_move_iterator(prepared.body.end()));
    origins.insert(origins.end(),
                   std::make_move_iterator(prepared.origins.begin()),
                   std::make_move_iterator(prepared.origins.end()));
    captions.insert(captions.end(),
                    std::make_move_iterator(prepared.trailing_captions.begin()),
                    std::make_move_iterator(prepared.trailing_captions.end()));
  }
  // Preserve the actor's combined caption display, in arrival order, while
  // retaining each caption's actual contributing source member separately.
  bool first_caption = true;
  for (auto &[caption, contribution] : captions) {
    if (!first_caption)
      caption.insert(caption.begin(), domain::Text{"\n", {}});
    first_caption = false;
    body.emplace_back(std::move(caption));
    origins.emplace_back(std::move(contribution));
  }
  if (body.empty())
    throw std::invalid_argument("album prepared no outbound content");
  const auto &primary = stored[eligible.front()];
  BridgeDeliveryRequest request{
      delivery_id,
      delivery_id + ":1",
      pair_id_,
      std::move(prepared_members),
      {route.target, std::move(body), std::move(reply),
       domain::DeliveryPolicy::SingleMessageOnly, limits},
      std::move(origins),
      BridgeQuoteAnchor::FirstConfirmed,
      primary.message.received_at.unix_milliseconds,
      config_->enable_retry_queue ? config_->message_retry_max_attempts : 1,
      BridgeAlbumPolicy::PreserveObservedBatch,
      config_->enable_retry_queue && config_->message_retry_max_attempts > 1
          ? std::optional{BridgeDeliveryRetryPolicy{
                config_->message_retry_base_interval_sec,
                config_->max_retry_interval_sec}}
          : std::nullopt,
      std::move(publications[eligible.front()])};
  request.album_observations = std::move(observations);
  BridgeForwardResult result;
  try {
    result.domain_delivery = co_await send_bridge_delivery(
        gateway_, repository_, blocking_, references_, std::move(request));
  } catch (const BridgeAlbumBatchSuppressed &) {
    co_return result; // Concurrent overlap is durably associated, never sent.
  }
  co_return result;
}
} // namespace bridge::telegram
