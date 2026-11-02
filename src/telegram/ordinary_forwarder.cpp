#include "telegram/ordinary_forwarder.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_hash.hpp"
#include "bridge_media_reader.hpp"
#include "bridge_state_repository.hpp"
#include "telegram/media_processor.hpp"
#include <telegram/bot/domain_views.hpp>

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace bridge::telegram {
namespace {
namespace domain = obcx::bot;
template <class T> auto require(domain::BotOperationResult<T> result) -> T {
  result.validate();
  if (!result.ok())
    throw BridgeBotOperationFailure(std::move(*result.error));
  return std::move(*result.value);
}
template <class T>
auto encode(std::string codec, const T &value) -> ledger::OpaqueValue {
  auto bytes = domain::Json::to_cbor(domain::Json(value));
  return {std::move(codec), 1, {bytes.begin(), bytes.end()}};
}
auto is_qq_echo(const domain::MessageReceived &message) -> bool {
  constexpr std::string_view marker = "[QQ] ";
  std::size_t matched = 0;
  for (const auto &block : message.body) {
    const auto *text = std::get_if<domain::ReceivedRichText>(&block);
    if (!text)
      return false;
    for (const auto &span : *text) {
      const auto *literal = std::get_if<domain::Text>(&span);
      if (!literal)
        return false;
      const auto count =
          std::min(marker.size() - matched, literal->content.size());
      if (std::string_view(literal->content).substr(0, count) !=
          marker.substr(matched, count))
        return false;
      matched += count;
      if (matched == marker.size())
        return true;
    }
  }
  return false;
}
} // namespace

OrdinaryForwarder::OrdinaryForwarder(
    std::shared_ptr<domain::BotOperationGateway> gateway,
    std::shared_ptr<const domain::ConversationResolver> references,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    const BridgeInstallationPair &pair,
    std::shared_ptr<const BridgeConfig> config)
    : gateway_(std::move(gateway)), references_(std::move(references)),
      repository_(std::move(repository)), blocking_(std::move(blocking)),
      config_(std::move(config)),
      installation_{pair.telegram_installation,
                    domain::SurfaceId{"telegram.bot_api"}},
      pair_id_(pair.id) {
  if (!gateway_ || !references_ || !repository_ || !blocking_ || !config_)
    throw std::invalid_argument(
        "Telegram ordinary forwarding requires owned services");
  media_processor_ = std::make_shared<TelegramMediaProcessor>(
      std::make_shared<BridgeCommandOperations>(gateway_, pair, references_),
      config_, repository_, blocking_);
  const domain::BotInstallationRef target_installation{
      pair.onebot11_installation, domain::SurfaceId{"onebot11.qq"}};
  const auto add = [&](const std::string &group,
                       std::optional<std::string> topic, bool all_topics,
                       const std::string &qq, bool show_sender) {
    // Resolve configuration once, without treating a canonical main scope as
    // proof of absent native topic or exact topic 1 on an incoming message.
    (void)require(
        references_->resolve(installation_, configured_group(group, topic)));
    auto target = require(references_->resolve(
        target_installation, configured_group(qq, std::nullopt)));
    for (const auto &existing : routes_)
      if (existing.group == group &&
          (existing.all_topics || all_topics || existing.topic == topic))
        throw std::invalid_argument("overlapping Telegram forwarding routes");
    routes_.push_back(
        {group, std::move(topic), all_topics, std::move(target), show_sender});
  };
  for (const auto &[key, mapping] : pair.group_map) {
    (void)key;
    if (mapping.mode == BridgeMode::GROUP_TO_GROUP) {
      if (mapping.enable_tg_to_qq)
        add(mapping.telegram_group_id, std::nullopt, true, mapping.qq_group_id,
            mapping.show_tg_to_qq_sender);
    } else {
      for (const auto &topic : mapping.topics)
        if (topic.enable_tg_to_qq)
          add(mapping.telegram_group_id,
              std::to_string(topic.telegram_topic_id), false, topic.qq_group_id,
              topic.show_tg_to_qq_sender);
    }
  }
}

auto OrdinaryForwarder::handled_media_marker(
    const domain::MessageReceived &message, std::size_t index) -> bool {
  if (index == 0 || index >= message.body.size())
    return false;
  const auto *marker =
      std::get_if<domain::UnsupportedContent>(&message.body[index]);
  const auto *media =
      std::get_if<domain::ReceivedMedia>(&message.body[index - 1]);
  if (!marker || marker->description || !media ||
      media->media.installation != message.message.conversation.installation)
    return false;
  // The SDK emits these observations immediately after the actual attachment.
  // A marker by itself is not permission to reinterpret arbitrary content.
  if (marker->category == "telegram.sticker")
    return obcx::telegram::bot::sticker_view(*media).has_value();
  if (marker->category == "telegram.video-note")
    return obcx::telegram::bot::video_note_view(*media).has_value();
  return false;
}

auto OrdinaryForwarder::literal_entity_marker(
    const domain::UnsupportedContent &marker) -> bool {
  if (marker.category != "telegram.entity" || !marker.description)
    return false;
  // These classify literal text already present in the typed body/caption.
  // They neither supply hidden link targets nor authorize cross-platform pings.
  // text_link, custom_emoji, unsupported styles and future entities stay
  // closed.
  const auto &kind = *marker.description;
  return kind == "url" || kind == "email" || kind == "phone_number" ||
         kind == "hashtag" || kind == "cashtag";
}

auto OrdinaryForwarder::accepts_body(const domain::MessageReceived &message,
                                     bool allow_edit_marker) -> bool {
  if (message.body.empty())
    return false;
  const auto has_literal = [](const domain::ReceivedRichText &text) {
    return std::ranges::any_of(text, [](const auto &span) {
      const auto *literal = std::get_if<domain::Text>(&span);
      return literal && !literal->content.empty();
    });
  };
  const auto literal_present =
      std::ranges::any_of(message.body, [&](const auto &block) {
        if (const auto *text = std::get_if<domain::ReceivedRichText>(&block))
          return has_literal(*text);
        if (const auto *media = std::get_if<domain::ReceivedMedia>(&block))
          return has_literal(media->caption);
        return false;
      });
  bool content_present = false;
  std::size_t edit_markers = 0;
  for (std::size_t index = 0; index < message.body.size(); ++index) {
    const auto &block = message.body[index];
    if (std::holds_alternative<domain::ReceivedRichText>(block) ||
        std::holds_alternative<domain::ReceivedMedia>(block)) {
      content_present = true;
      continue;
    }
    if (handled_media_marker(message, index))
      continue;
    const auto *marker = std::get_if<domain::UnsupportedContent>(&block);
    if (marker && literal_present && literal_entity_marker(*marker))
      continue;
    if (allow_edit_marker && marker && !marker->description &&
        marker->category == "telegram.edited" && ++edit_markers == 1)
      continue;
    return false;
  }
  return content_present && (!allow_edit_marker || edit_markers == 1);
}

auto OrdinaryForwarder::accepts(const domain::MessageReceived &message)
    -> bool {
  // Edits and albums retain their own admission path; recognizing supported
  // special-media observations does not convert either into a new send.
  return !message.media_group && accepts_body(message, false);
}

auto OrdinaryForwarder::display_name(domain::UserRef user,
                                     domain::ConversationRef source)
    -> boost::asio::awaitable<std::optional<std::string>> {
  auto result =
      co_await domain::BotClient{*gateway_}.get_member({source, user});
  result.validate();
  if (!result.ok()) {
    if (result.error->code == domain::BotOperationErrorCode::Cancelled)
      throw BridgeBotOperationFailure(std::move(*result.error));
    co_return std::nullopt;
  }
  if (result.value->conversation != source || result.value->user != user)
    throw std::runtime_error("Telegram member result scope mismatch");
  co_return result.value->display_name;
}

auto OrdinaryForwarder::render_text(domain::ReceivedRichText text,
                                    domain::ConversationRef source)
    -> boost::asio::awaitable<domain::RichText> {
  domain::RichText rendered;
  for (const auto &span : text) {
    if (const auto *literal = std::get_if<domain::Text>(&span)) {
      // Preserve the predecessor's explicit plain-text QQ presentation. Do not
      // ask the SDK to silently drop unsupported native text styles.
      if (!literal->content.empty())
        rendered.emplace_back(domain::Text{literal->content, {}});
    } else if (const auto *mention = std::get_if<domain::Mention>(&span)) {
      auto name = co_await display_name(mention->user, source);
      const auto id = std::to_string(
          obcx::telegram::bot::user_identity_view(mention->user).user_id);
      rendered.emplace_back(
          domain::Text{name ? "@" + *name : "[@" + id + "]", {}});
    } else if (const auto *username =
                   std::get_if<domain::UnresolvedMention>(&span)) {
      rendered.emplace_back(domain::Text{"@" + username->username, {}});
    } else {
      rendered.emplace_back(domain::Text{"[@all]", {}});
    }
  }
  co_return rendered;
}

auto OrdinaryForwarder::resolve_source(
    const obcx::message_store::events::MessageStored &stored) const
    -> std::optional<ResolvedSource> {
  stored.message.validate();
  stored.source.validate(stored.message);
  if (stored.message.message.conversation.installation != installation_ ||
      stored.source.source_platform != "telegram")
    throw std::invalid_argument("Telegram forwarding installation mismatch");
  if (is_qq_echo(stored.message))
    return std::nullopt;
  const auto &facts = stored.message.context;
  if (facts.kind != "group" || facts.group_id.empty())
    return std::nullopt;
  const auto routing_topic =
      facts.topic_id ? std::optional{std::to_string(*facts.topic_id)}
                     : std::nullopt;
  const auto route_it = std::ranges::find_if(routes_, [&](const auto &route) {
    return route.group == facts.group_id &&
           (route.all_topics || route.topic == routing_topic);
  });
  if (route_it == routes_.end())
    return std::nullopt;
  const auto route = *route_it;
  // Persist the old chat: key, never the group: configuration alias.
  if (stored.source.conversation_id != telegram_conversation_id(route.group))
    throw std::invalid_argument("Telegram archival conversation key mismatch");
  std::optional<std::string> observed_sender_name;
  if (route.show_sender && facts.sender_profile) {
    const auto &profile = *facts.sender_profile;
    // Preserve the actor's predecessor first/last -> @username -> Unknown
    // policy, including empty fields and original username spelling. The SDK
    // only supplies fields. A compatibility `from` name on sender_chat does
    // not authorize get_member, invent a user author or consult cached self.
    if (profile.first_name) {
      observed_sender_name = *profile.first_name;
      if (profile.last_name)
        *observed_sender_name += " " + *profile.last_name;
    } else if (profile.username) {
      observed_sender_name = "@" + *profile.username;
    }
  }
  const bool genuine_reply = stored.message.reply &&
                             stored.message.reply->relation() ==
                                 domain::ReplyRelation::SameConversation &&
                             !facts.topic_root_reply;
  return ResolvedSource{route, std::move(observed_sender_name), genuine_reply};
}

auto OrdinaryForwarder::forward(
    obcx::message_store::events::MessageStored stored,
    BridgeDeliveryPublication publication)
    -> boost::asio::awaitable<BridgeForwardResult> {
  auto resolved = resolve_source(stored);
  if (!resolved)
    co_return BridgeForwardResult{};
  if (!accepts(stored.message))
    throw std::invalid_argument(
        "Telegram forwarding requires ordinary typed content");
  const auto &route = resolved->route;
  const auto source = stored.message.message.conversation;
  const auto digest = calculate_hash(
      domain::Json::array(
          {pair_id_, stored.source.source_platform, stored.source.source_bot,
           stored.source.conversation_id, stored.source.message_id,
           domain::Json(source), domain::Json(route.target)})
          .dump());
  if (digest.empty())
    throw std::runtime_error("Telegram delivery identity hashing failed");
  const auto delivery_id = "tg-ordinary-v1:" + digest;
  BridgeForwardResult result;
  auto [echo, existing] = co_await blocking_->run(
      [repository = repository_, delivery_id, pair = pair_id_,
       observed = stored.message.message] {
        if (is_bridge_delivery_output(*repository, pair, observed))
          return std::pair{true, std::optional<ledger::DeliverySnapshot>{}};
        return std::pair{false, repository->load_delivery(delivery_id)};
      });
  if (echo)
    co_return result;
  if (existing) {
    if (!existing->admission || existing->admission->pair_id != pair_id_ ||
        existing->admission->source_scope !=
            encode("obcx.bot.ConversationRef.cbor", source) ||
        existing->admission->target_scope !=
            encode("obcx.bot.ConversationRef.cbor", route.target) ||
        existing->members.size() != 1 ||
        existing->members.front().message_ref !=
            encode("obcx.bot.MessageRef.cbor", stored.message.message))
      throw std::runtime_error(
          "Telegram delivery identity collision or corrupt scope");
    result.domain_delivery =
        BridgeDeliveryResult{std::move(*existing), std::nullopt, false};
    co_return result;
  }
  // Recipe declarations are local, not a claim of current provider permission.
  // Reject absent target kinds before source downloads; never relabel ordinary
  // audio as Voice or a file as Image to bypass the installed recipe boundary.
  const auto capabilities = require(
      co_await domain::BotClient{*gateway_}.capabilities({route.target}));
  if (capabilities.conversation != route.target)
    throw std::runtime_error(
        "Telegram forwarding target capability scope mismatch");
  check_media_capabilities(stored.message, capabilities);
  const auto media_count =
      std::ranges::count_if(stored.message.body, [](const auto &block) {
        return std::holds_alternative<domain::ReceivedMedia>(block);
      });
  domain::SendLimits limits{1, 0, 0};
  if (media_count) {
    const auto item = config_->qq_media_download_max_bytes;
    if (!item || item > domain::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid configured Telegram read budget");
    const auto count = static_cast<std::size_t>(media_count);
    limits = {1, item,
              count > domain::detail::domain_maximum_bytes / item
                  ? domain::detail::domain_maximum_bytes
                  : count * item};
  }
  const auto target = route.target;
  auto prepared = co_await prepare_member(stored, std::move(*resolved),
                                          "source", true, false, false, limits);
  if (prepared.body.empty())
    co_return result;
  BridgeDeliveryRequest request{
      delivery_id,
      delivery_id + ":1",
      pair_id_,
      {std::move(prepared.member)},
      {target, std::move(prepared.body), std::move(prepared.reply),
       domain::DeliveryPolicy::SingleMessageOnly, limits},
      std::move(prepared.origins),
      BridgeQuoteAnchor::FirstConfirmed,
      stored.message.received_at.unix_milliseconds,
      config_->enable_retry_queue ? config_->message_retry_max_attempts : 1,
      std::nullopt,
      config_->enable_retry_queue && config_->message_retry_max_attempts > 1
          ? std::optional{BridgeDeliveryRetryPolicy{
                config_->message_retry_base_interval_sec,
                config_->max_retry_interval_sec}}
          : std::nullopt,
      std::move(publication)};
  result.domain_delivery = co_await send_bridge_delivery(
      gateway_, repository_, blocking_, references_, std::move(request));
  co_return result;
}

void OrdinaryForwarder::check_media_capabilities(
    const domain::MessageReceived &message,
    const domain::ConversationCapabilities &capabilities) {
  for (const auto &block : message.body) {
    if (const auto *media = std::get_if<domain::ReceivedMedia>(&block)) {
      const auto supports = [&](domain::MediaKind kind) {
        return std::ranges::any_of(
            capabilities.media, [&](const auto &capability) {
              return capability.kind == kind &&
                     capability.source == domain::MediaSourceKind::Bytes;
            });
      };
      const auto sticker = obcx::telegram::bot::sticker_view(*media);
      if (sticker &&
          sticker->format == obcx::telegram::bot::StickerFormat::AnimatedTgs)
        continue; // Explicit text marker only; no TGS acquisition/decoder.
      const auto target_kind =
          !sticker ? media->kind
          : sticker->format == obcx::telegram::bot::StickerFormat::StaticWebp
              ? domain::MediaKind::Image
              : domain::MediaKind::Animation;
      if (!supports(target_kind) ||
          (!sticker && media->kind == domain::MediaKind::Animation &&
           !supports(domain::MediaKind::Video)))
        throw std::runtime_error(
            "target has no installed owned-byte recipe for source media kind");
    }
  }
}

auto OrdinaryForwarder::prepare_member(
    const obcx::message_store::events::MessageStored &stored,
    ResolvedSource resolved, std::string member_id, bool include_context,
    bool defer_captions, bool allow_edit_marker, domain::SendLimits limits)
    -> boost::asio::awaitable<PreparedMember> {
  limits.validate();
  domain::OperationCorrelation{member_id}.validate();
  if (!accepts_body(stored.message, allow_edit_marker))
    throw std::invalid_argument("unsupported Telegram preparation content");
  const auto &route = resolved.route;
  const auto source = stored.message.message.conversation;
  std::optional<domain::MessageRef> reply;
  if (include_context && stored.message.reply)
    reply = co_await blocking_->run([repository = repository_, pair = pair_id_,
                                     target = route.target,
                                     observed = *stored.message.reply] {
      return resolve_bridge_delivery_reply(*repository, pair, target, observed);
    });
  std::vector<std::string> inputs;
  std::vector<ledger::Provenance> prefix_origins;
  domain::RichText prefix;
  if (include_context && route.show_sender) {
    std::optional<std::string> name;
    if (const auto *sender =
            std::get_if<domain::KnownUser>(&stored.message.sender))
      name = co_await display_name(sender->user, source);
    // Successful common member normalization remains authoritative for known
    // users. Missing/failed profiles use the original observed presentation;
    // conversation and unknown senders never trigger a user lookup.
    if (!name)
      name = resolved.observed_sender_name;
    prefix.emplace_back(
        domain::Text{"[" + name.value_or("Unknown") + "]\t", {}});
    inputs.push_back("sender");
    prefix_origins.push_back({member_id, "sender"});
  }
  if (include_context && !reply && resolved.genuine_reply) {
    prefix.emplace_back(domain::Text{"[回复一条消息] ", {}});
    inputs.push_back("reply");
    prefix_origins.push_back({member_id, "reply"});
  }
  domain::MessageBody body;
  std::vector<std::optional<std::vector<ledger::Provenance>>> origins;
  // QQ serializes attachment captions after their media segment. Keep the
  // predecessor's sender/reply prefix before media as an explicit text block.
  if (!prefix.empty()) {
    body.emplace_back(std::move(prefix));
    origins.emplace_back(std::move(prefix_origins));
  }
  std::size_t bytes = 0;
  std::vector<std::pair<domain::RichText, std::vector<ledger::Provenance>>>
      trailing_captions;
  for (std::size_t index = 0; index < stored.message.body.size(); ++index) {
    if (const auto *marker = std::get_if<domain::UnsupportedContent>(
            &stored.message.body[index])) {
      if (allow_edit_marker && marker->category == "telegram.edited")
        continue; // Control observation, not a fabricated/renumbered body.
      if (handled_media_marker(stored.message, index))
        continue; // Its original position contributes to the preceding media.
      if (literal_entity_marker(*marker)) {
        // Retain the observed position without inventing a new output block or
        // changing the original literal text. This is classification metadata.
        inputs.push_back("body:" + std::to_string(index));
        continue;
      }
      throw std::invalid_argument("unsupported Telegram preparation content");
    }
    auto key = "body:" + std::to_string(index);
    inputs.push_back(key);
    std::vector<ledger::Provenance> contribution{{member_id, key}};
    if (handled_media_marker(stored.message, index + 1)) {
      auto marker_key = "body:" + std::to_string(index + 1);
      inputs.push_back(marker_key);
      contribution.push_back({member_id, std::move(marker_key)});
    }
    if (const auto *text = std::get_if<domain::ReceivedRichText>(
            &stored.message.body[index])) {
      auto rendered = co_await render_text(*text, source);
      if (rendered.empty())
        continue;
      body.emplace_back(std::move(rendered));
    } else {
      const auto &media =
          std::get<domain::ReceivedMedia>(stored.message.body[index]);
      auto caption = co_await render_text(media.caption, source);
      if (defer_captions && !caption.empty()) {
        trailing_captions.emplace_back(std::move(caption), contribution);
        caption.clear();
      }
      const auto remaining = limits.maximum_media_total_bytes - bytes;
      const auto sticker = obcx::telegram::bot::sticker_view(media);
      const bool text_only_sticker =
          sticker &&
          sticker->format == obcx::telegram::bot::StickerFormat::AnimatedTgs;
      if (!remaining && !text_only_sticker)
        throw std::runtime_error("Telegram media total byte budget exhausted");
      if (sticker) {
        auto prepared = co_await media_processor_->prepare_sticker(
            media, std::move(caption),
            std::min(limits.maximum_media_item_bytes, remaining));
        bytes += prepared.prepared_bytes;
        body.push_back(std::move(prepared.block));
        origins.emplace_back(std::move(contribution));
        continue;
      }
      if (media.kind == domain::MediaKind::Animation) {
        auto prepared = co_await media_processor_->prepare_animation(
            media, std::move(caption),
            std::min(limits.maximum_media_item_bytes, remaining));
        bytes += prepared.prepared_bytes;
        body.push_back(std::move(prepared.block));
        origins.emplace_back(std::move(contribution));
        continue;
      }
      // Ask the typed extension to identify video-notes. Bridge deliberately
      // chooses ordinary Video presentation, as its predecessor did.
      const auto video_note = obcx::telegram::bot::video_note_view(media);
      auto data = co_await read_bridge_attachment(
          gateway_, video_note ? *video_note : media,
          std::min(limits.maximum_media_item_bytes, remaining));
      bytes += data.bytes.size();
      switch (media.kind) {
      case domain::MediaKind::Image:
        body.emplace_back(domain::Image{std::move(data), std::move(caption)});
        break;
      case domain::MediaKind::Video:
        // The predecessor explicitly presents video-notes as ordinary video.
        // This is Bridge's conversion, not an SDK claim of round presentation.
        body.emplace_back(domain::Video{std::move(data), std::move(caption)});
        break;
      case domain::MediaKind::Audio:
      case domain::MediaKind::Voice:
        body.emplace_back(domain::Audio{std::move(data),
                                        media.kind == domain::MediaKind::Voice
                                            ? domain::AudioKind::Voice
                                            : domain::AudioKind::Audio,
                                        std::move(caption)});
        break;
      case domain::MediaKind::File: {
        if (!data.metadata.filename)
          throw std::runtime_error("Telegram file lacks display filename");
        auto filename = *data.metadata.filename;
        body.emplace_back(domain::File{std::move(data), std::move(filename),
                                       std::move(caption)});
        break;
      }
      default:
        throw std::invalid_argument(
            "specialized Telegram media requires its dedicated path");
      }
    }
    origins.emplace_back(std::move(contribution));
  }
  co_return PreparedMember{{std::move(member_id), stored.message.message,
                            stored.message, stored.message.media_group,
                            std::move(inputs)},
                           std::move(body),
                           std::move(origins),
                           std::move(reply),
                           bytes,
                           std::move(trailing_captions)};
}
} // namespace bridge::telegram
