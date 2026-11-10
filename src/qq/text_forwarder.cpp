#include "qq/text_forwarder.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_hash.hpp"
#include "bridge_media_reader.hpp"
#include "bridge_state_repository.hpp"
#include "qq/card_formatter.hpp"
#include "qq/forward_preparer.hpp"
#include "qq/image_preparer.hpp"
#include "qq/voice_preparer.hpp"
#include <map>
#include <onebot11/bot/presentation_observation.hpp>
#include <onebot11/bot/special_content.hpp>
#include <onebot11/bot/user_target.hpp>
#include <set>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace bridge::qq {
namespace {
namespace bot = obcx::bot;

auto card_category(obcx::onebot11::bot::CardKind kind) -> std::string_view {
  using obcx::onebot11::bot::CardKind;
  switch (kind) {
  case CardKind::Json:
    return "onebot11.json";
  case CardKind::App:
    return "onebot11.app";
  case CardKind::Ark:
    return "onebot11.ark";
  case CardKind::MiniApp:
    return "onebot11.miniapp";
  case CardKind::Share:
    return "onebot11.share";
  }
  throw std::invalid_argument("invalid QQ card kind");
}
auto is_card_category(std::string_view category) -> bool {
  return category == "onebot11.json" || category == "onebot11.app" ||
         category == "onebot11.ark" || category == "onebot11.miniapp" ||
         category == "onebot11.share";
}

auto is_presentation_category(std::string_view category) -> bool {
  return category == "onebot11.music" || category == "onebot11.node";
}
auto render_presentation(
    const obcx::onebot11::bot::PresentationObservation &view) -> std::string {
  namespace qq = obcx::onebot11::bot;
  view.validate();
  std::string result;
  const auto append = [&](std::string_view text) {
    if (text.size() > bot::detail::bot_maximum_bytes - result.size())
      throw std::runtime_error("QQ presentation exceeds text budget");
    result.append(text);
  };
  if (const auto *music = std::get_if<qq::MusicObservation>(&view.value)) {
    append("[音乐分享: ");
    append(music->title ? std::string_view{*music->title} : "未知音乐");
    append("]");
  } else {
    const auto &node = std::get<qq::InlineNodeObservation>(view.value);
    if (node.form == qq::InlineNodeContentForm::Missing)
      return result;
    append("👤 ");
    append(node.display_name ? std::string_view{*node.display_name}
                             : "未知用户");
    append(": ");
    if (node.form == qq::InlineNodeContentForm::Unsupported)
      append("[未知内容]");
    for (const auto &part : node.parts) {
      if (const auto *text = std::get_if<qq::InlineNodeText>(&part)) {
        append(text->content);
      } else if (const auto *face = std::get_if<qq::FaceView>(&part)) {
        append("[表情:");
        append(face->id);
        append("]");
      } else {
        const auto &kind = std::get<qq::InlineNodeKind>(part).native_kind;
        append("[");
        append(kind == "image" ? std::string_view{"图片"}
                               : std::string_view{kind});
        append("]");
      }
    }
    append("\n");
  }
  return result;
}

template <class T> auto require(bot::BotOperationResult<T> result) -> T {
  result.validate();
  if (!result.ok())
    throw std::runtime_error(
        "bridge source/configuration reference unavailable");
  return std::move(*result.value);
}
auto same_scope(const ledger::OpaqueValue &encoded,
                const bot::ConversationRef &scope) -> bool {
  const auto bytes = bot::Json::to_cbor(bot::Json(scope));
  return encoded.codec == "obcx.bot.ConversationRef.cbor" &&
         encoded.version == 1 &&
         encoded.bytes == std::string(bytes.begin(), bytes.end());
}
// Intentionally has no gateway, resolver or UserRef input. This
// presentation-only path can consult the old cache but cannot query a sender.
auto unknown_sender_label(
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::string installation, std::string display_id, std::string group)
    -> boost::asio::awaitable<std::string> {
  auto cached =
      co_await blocking->run([repository, installation, display_id, group] {
        return repository->query_user_display_name(installation, "qq",
                                                   display_id, group);
      });
  co_return cached.value_or(display_id);
}
} // namespace

TextForwarder::TextForwarder(
    std::shared_ptr<bot::BotOperationGateway> gateway,
    std::shared_ptr<const bot::ConversationResolver> references,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    const BridgeInstallationPair &pair, std::int64_t max_attempts,
    std::shared_ptr<const BridgeConfig> config)
    : gateway_(std::move(gateway)), references_(std::move(references)),
      repository_(std::move(repository)), blocking_(std::move(blocking)),
      config_(std::move(config)), pair_id_(pair.id),
      max_attempts_(max_attempts) {
  if (!gateway_ || !references_ || !repository_ || !blocking_ || !config_ ||
      max_attempts <= 0)
    throw std::invalid_argument(
        "QQ text forwarding requires owned services and budget");
  const bot::BotInstallationRef qq{pair.onebot11_installation,
                                   bot::SurfaceId{"onebot11.qq"}};
  const bot::BotInstallationRef tg{pair.telegram_installation,
                                   bot::SurfaceId{"telegram.bot_api"}};
  const auto add = [&](const std::string &source, const std::string &target,
                       std::optional<std::string> topic, bool show_sender) {
    auto from = require(
        references_->resolve(qq, configured_group(source, std::nullopt)));
    auto to =
        require(references_->resolve(tg, configured_group(target, topic)));
    if (std::ranges::any_of(
            routes_, [&](const auto &route) { return route.source == from; }))
      throw std::invalid_argument("duplicate QQ text forwarding source route");
    routes_.push_back({std::move(from), std::move(to), source, show_sender});
  };
  for (const auto &[key, mapping] : pair.group_map) {
    (void)key;
    if (mapping.mode == BridgeMode::GROUP_TO_GROUP) {
      if (mapping.enable_qq_to_tg)
        add(mapping.qq_group_id, mapping.telegram_group_id, std::nullopt,
            mapping.show_qq_to_tg_sender);
    } else {
      for (const auto &topic : mapping.topics)
        if (topic.enable_qq_to_tg)
          add(topic.qq_group_id, mapping.telegram_group_id,
              std::to_string(topic.telegram_topic_id),
              topic.show_qq_to_tg_sender);
    }
  }
}

auto TextForwarder::accepts(const bot::MessageReceived &source) -> bool {
  // Empty/reply-only bodies may still produce the configured sender prefix;
  // otherwise forward() returns no output without submitting an empty message.
  if (std::holds_alternative<bot::ConversationSender>(source.sender) ||
      source.media_group)
    return false;
  const auto supported_text = [](const auto &text) {
    return std::ranges::none_of(text, [](const auto &span) {
      return std::holds_alternative<bot::UnresolvedMention>(span);
    });
  };
  return std::ranges::all_of(source.body, [&](const auto &block) {
    if (const auto *text = std::get_if<bot::ReceivedRichText>(&block))
      return supported_text(*text);
    if (const auto *media = std::get_if<bot::ReceivedMedia>(&block))
      return supported_text(media->caption);
    const auto &unsupported = std::get<bot::UnsupportedContent>(block);
    return unsupported.category == "onebot11.image" ||
           unsupported.category == "onebot11.forward" ||
           unsupported.category == "onebot11.face" ||
           unsupported.category == "onebot11.mface" ||
           unsupported.category == "onebot11.shake" ||
           is_presentation_category(unsupported.category) ||
           is_card_category(unsupported.category);
  });
}

auto TextForwarder::display_name(bot::UserRef user, Route route)
    -> boost::asio::awaitable<std::optional<std::string>> {
  const auto id =
      std::to_string(obcx::onebot11::bot::user_identity_view(user).user_id);
  const auto installation = user.installation.installation_id;
  auto cached = co_await blocking_->run([repository = repository_, installation,
                                         id, group = route.source_group] {
    return repository->query_user_display_name(installation, "qq", id, group);
  });
  // A cached empty display name is an observed value, not a miss. Preserve
  // the predecessor's optional-presence policy and do not query/overwrite it.
  if (cached)
    co_return *cached;
  auto member = co_await bot::BotClient{*gateway_}.get_member(
      bot::GetMemberRequest{route.source, user});
  member.validate();
  if (!member.ok()) {
    if (member.error->code == bot::BotOperationErrorCode::Cancelled)
      throw BridgeBotOperationFailure(std::move(*member.error));
    co_return std::nullopt;
  }
  if (member.value->conversation != route.source || member.value->user != user)
    throw std::runtime_error("bridge member result scope mismatch");
  if (!member.value->display_name)
    co_return std::nullopt;
  auto name = *member.value->display_name;
  (void)co_await blocking_->run([repository = repository_, installation, id,
                                 group = route.source_group, name] {
    auto record = repository->get_user(installation, "qq", id, group)
                      .value_or(storage::UserInfo{});
    record.installation_id = installation;
    record.platform = "qq";
    record.user_id = id;
    record.group_id = group;
    record.nickname = name;
    record.last_updated = std::chrono::system_clock::now();
    return repository->save_or_update_user(record, true);
  });
  co_return name;
}

auto TextForwarder::render_text(bot::ReceivedRichText text, Route route)
    -> boost::asio::awaitable<bot::RichText> {
  bot::RichText result;
  for (const auto &span : text) {
    if (const auto *literal = std::get_if<bot::Text>(&span)) {
      if (!literal->content.empty())
        result.emplace_back(*literal);
    } else if (route.show_sender) {
      if (const auto *mention = std::get_if<bot::Mention>(&span)) {
        const auto id = std::to_string(
            obcx::onebot11::bot::user_identity_view(mention->user).user_id);
        const auto name = co_await display_name(mention->user, route);
        result.emplace_back(
            bot::Text{name ? "@" + *name + " " : "[@" + id + "] ", {}});
      } else if (std::holds_alternative<bot::MentionEveryone>(span)) {
        result.emplace_back(bot::Text{"[@all] ", {}});
      }
    }
  }
  co_return result;
}

auto TextForwarder::forward(obcx::message_store::events::MessageStored stored,
                            BridgeDeliveryPublication publication)
    -> boost::asio::awaitable<BridgeForwardResult> {
  stored.message.validate();
  stored.source.validate(stored.message);
  if (has_telegram_bridge_prefix(stored.message))
    co_return BridgeForwardResult{};
  const auto selected = std::ranges::find_if(routes_, [&](const auto &route) {
    return route.source == stored.message.message.conversation;
  });
  if (selected == routes_.end())
    co_return BridgeForwardResult{};
  if (!accepts(stored.message))
    throw std::invalid_argument(
        "QQ forwarding content is unsupported; native fallback is disabled");
  const auto route = *selected;
  if (stored.source.source_platform != "qq" ||
      stored.source.conversation_id != qq_conversation_id(route.source_group))
    throw std::invalid_argument("QQ text forwarding archival scope mismatch");
  std::optional<std::string> unknown_display_id;
  if (route.show_sender &&
      std::holds_alternative<bot::UnknownSender>(stored.message.sender))
    unknown_display_id = "Unknown";
  const auto key =
      bot::Json::array({pair_id_, stored.source.source_platform,
                        stored.source.source_bot, stored.source.conversation_id,
                        stored.source.message_id, bot::Json(route.target)})
          .dump();
  const auto digest = calculate_hash(key);
  if (digest.empty())
    throw std::runtime_error("bridge delivery identity hashing failed");
  const auto delivery_id = "qq-text-v1:" + digest;
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
    const auto &admission = existing->admission;
    const auto encoded = bot::Json::to_cbor(bot::Json(stored.message.message));
    if (!admission || admission->pair_id != pair_id_ ||
        !same_scope(admission->source_scope, route.source) ||
        !same_scope(admission->target_scope, route.target) ||
        existing->members.size() != 1 ||
        existing->members.front().message_ref !=
            ledger::OpaqueValue{"obcx.bot.MessageRef.cbor", 1,
                                std::string(encoded.begin(), encoded.end())})
      throw std::runtime_error(
          "bridge delivery identity collision or corrupt scope");
    result.bot_delivery =
        BridgeDeliveryResult{std::move(*existing), std::nullopt, false};
    co_return result; // No member lookup, send or new success on replay.
  }

  std::map<std::size_t, obcx::onebot11::bot::QqForwardRef> forwards;
  const bool has_forward =
      std::ranges::any_of(stored.message.body, [](const auto &block) {
        const auto *unsupported = std::get_if<bot::UnsupportedContent>(&block);
        return unsupported && unsupported->category == "onebot11.forward";
      });
  std::map<std::size_t, std::string> display_literals;
  // Market faces without a readable image are dropped, never sent as text.
  std::set<std::size_t> omitted_market_faces;
  bool has_special_display = false;
  bool has_presentation = false;
  for (std::size_t index = 0; index < stored.message.body.size(); ++index) {
    if (const auto *unsupported =
            std::get_if<bot::UnsupportedContent>(&stored.message.body[index])) {
      has_presentation =
          has_presentation || is_presentation_category(unsupported->category);
      has_special_display = has_special_display ||
                            unsupported->category == "onebot11.face" ||
                            unsupported->category == "onebot11.mface" ||
                            is_card_category(unsupported->category);
      if (unsupported->category == "onebot11.shake")
        display_literals.emplace(index, "[戳一戳]");
    }
  }
  if (has_presentation) {
    std::optional<std::size_t> previous_native;
    for (const auto &view :
         obcx::onebot11::bot::presentation_observations(stored.message)) {
      view.validate();
      const auto index = view.normalized_block_index;
      if (view.source != stored.message.message ||
          view.received_at != stored.message.received_at ||
          index >= stored.message.body.size() ||
          (previous_native && view.native_segment_index <= *previous_native))
        throw std::runtime_error("QQ presentation source or position mismatch");
      previous_native = view.native_segment_index;
      const auto *unsupported =
          std::get_if<bot::UnsupportedContent>(&stored.message.body[index]);
      const auto expected =
          std::holds_alternative<obcx::onebot11::bot::MusicObservation>(
              view.value)
              ? "onebot11.music"
              : "onebot11.node";
      if (!unsupported || unsupported->category != expected ||
          !display_literals.emplace(index, render_presentation(view)).second)
        throw std::runtime_error("QQ presentation projection mismatch");
    }
    for (std::size_t i = 0; i < stored.message.body.size(); ++i) {
      const auto *unsupported =
          std::get_if<bot::UnsupportedContent>(&stored.message.body[i]);
      if (unsupported && is_presentation_category(unsupported->category) &&
          !display_literals.contains(i))
        throw std::runtime_error("QQ presentation lacks original observation");
    }
  }
  if (has_forward || has_special_display) {
    CardFormatter cards{config_};
    for (const auto &view :
         obcx::onebot11::bot::special_content(stored.message)) {
      if (const auto *forward =
              std::get_if<obcx::onebot11::bot::QqForwardRef>(&view.value)) {
        const auto index = view.normalized_block_index;
        if (view.source != stored.message.message ||
            forward->source != view.source ||
            index >= stored.message.body.size())
          throw std::runtime_error("forward view source mismatch");
        const auto *unsupported =
            std::get_if<bot::UnsupportedContent>(&stored.message.body[index]);
        if (!unsupported || unsupported->category != "onebot11.forward" ||
            !forwards.emplace(index, *forward).second)
          throw std::runtime_error("forward view projection mismatch");
      } else if (const auto *face =
                     std::get_if<obcx::onebot11::bot::FaceView>(&view.value)) {
        face->validate();
        const auto index = view.normalized_block_index;
        if (view.source != stored.message.message ||
            index >= stored.message.body.size())
          throw std::runtime_error("face view source mismatch");
        const auto *unsupported =
            std::get_if<bot::UnsupportedContent>(&stored.message.body[index]);
        if (!unsupported || unsupported->category != "onebot11.face" ||
            !display_literals.emplace(index, "[QQ表情:" + face->id + "]")
                 .second)
          throw std::runtime_error("face view projection mismatch");
      } else if (const auto *face =
                     std::get_if<obcx::onebot11::bot::MarketFaceView>(
                         &view.value)) {
        face->validate();
        const auto index = view.normalized_block_index;
        if (view.source != stored.message.message ||
            index >= stored.message.body.size())
          throw std::runtime_error("market face view source mismatch");
        if (const auto *media =
                std::get_if<bot::ReceivedMedia>(&stored.message.body[index]);
            media && obcx::onebot11::bot::market_face_view(*media))
          continue; // Original live ref, not the restored view, owns the read.
        const auto *unsupported =
            std::get_if<bot::UnsupportedContent>(&stored.message.body[index]);
        if (!unsupported || unsupported->category != "onebot11.mface" ||
            !omitted_market_faces.insert(index).second)
          throw std::runtime_error("market face view projection mismatch");
      } else if (const auto *card =
                     std::get_if<obcx::onebot11::bot::RawCardView>(
                         &view.value)) {
        card->validate();
        const auto index = view.normalized_block_index;
        if (view.source != stored.message.message ||
            index >= stored.message.body.size())
          throw std::runtime_error("card view source mismatch");
        const auto *unsupported =
            std::get_if<bot::UnsupportedContent>(&stored.message.body[index]);
        if (!unsupported ||
            unsupported->category != card_category(card->kind) ||
            display_literals.contains(index))
          throw std::runtime_error("card view projection mismatch");
        display_literals.emplace(index, co_await cards.render(*card));
      }
    }
    for (std::size_t i = 0; i < stored.message.body.size(); ++i) {
      const auto *unsupported =
          std::get_if<bot::UnsupportedContent>(&stored.message.body[i]);
      if (unsupported && unsupported->category == "onebot11.forward" &&
          !forwards.contains(i))
        throw std::runtime_error(
            "forward content lacks a typed resource reference");
      if (unsupported && ((unsupported->category == "onebot11.face" &&
                           !display_literals.contains(i)) ||
                          (unsupported->category == "onebot11.mface" &&
                           !omitted_market_faces.contains(i))))
        throw std::runtime_error("face content lacks a typed view");
      if (unsupported && is_card_category(unsupported->category) &&
          !display_literals.contains(i))
        throw std::runtime_error("card content lacks original metadata");
    }
  }
  if (!omitted_market_faces.empty() &&
      omitted_market_faces.size() == stored.message.body.size())
    co_return result;
  std::vector<PhotoInput> photo_refs;
  std::vector<std::size_t> photo_indices;
  std::size_t media_count = 0;
  bool has_market_face = false;
  for (std::size_t i = 0; i < stored.message.body.size(); ++i) {
    if (const auto *media =
            std::get_if<bot::ReceivedMedia>(&stored.message.body[i])) {
      ++media_count;
      has_market_face =
          has_market_face ||
          obcx::onebot11::bot::market_face_view(*media).has_value();
      if (media->kind == bot::MediaKind::Image) {
        photo_refs.push_back(media->media);
        photo_indices.push_back(i);
      }
    } else if (const auto *unsupported = std::get_if<bot::UnsupportedContent>(
                   &stored.message.body[i]);
               unsupported && unsupported->category == "onebot11.image") {
      ++media_count;
      photo_refs.emplace_back(std::nullopt);
      photo_indices.push_back(i);
    }
  }
  // Market-face captions belong to that image, not to unrelated album items.
  // The shared preparer inspects owned bytes to preserve GIF motion; the SDK
  // image-family observation itself does not assert an animation encoding.
  const bool mixed_media =
      has_forward || has_market_face || media_count != photo_refs.size();
  bot::SendLimits limits{1, 0, 0};
  std::vector<PreparedPhoto> photos;
  if (media_count || has_forward) {
    const auto per_item = config_->qq_media_download_max_bytes;
    if (per_item == 0 || per_item > bot::detail::bot_maximum_bytes)
      throw std::invalid_argument("invalid configured attachment read budget");
    const auto total =
        has_forward || media_count > bot::detail::bot_maximum_bytes / per_item
            ? bot::detail::bot_maximum_bytes
            : media_count * per_item;
    limits = {has_forward   ? bot::detail::bot_maximum_items
              : mixed_media ? stored.message.body.size()
                            : media_count,
              per_item, total};
    // Current received references are read directly. Historical URL/file hash
    // recipes and TG file-ID imports are no longer a cache input.
    if (!photo_refs.empty()) {
      photos = co_await prepare_photos(gateway_, blocking_, config_,
                                       std::move(photo_refs), limits);
    }
  }
  bot::RichText body;
  std::vector<std::string> inputs;
  std::vector<ledger::Provenance> origins;
  const auto contribute = [&](std::string block, bool text_contribution) {
    inputs.push_back(block);
    if (text_contribution)
      origins.push_back({"source", std::move(block)});
  };
  if (route.show_sender) {
    std::string label;
    if (const auto *known =
            std::get_if<bot::KnownUser>(&stored.message.sender)) {
      const auto user = known->user;
      auto name = co_await display_name(user, route);
      label = name.value_or(std::to_string(
          obcx::onebot11::bot::user_identity_view(user).user_id));
    } else {
      // An anonymous/unknown sender's predecessor display key is presentation,
      // not a UserRef. Read its existing cache without any member/self query.
      const auto display_id = unknown_display_id.value();
      // Preserve absent/empty ID and cached empty-name behavior; do not invent
      // an author or replace the original namespace/key with a placeholder.
      label = co_await unknown_sender_label(
          repository_, blocking_, route.source.installation.installation_id,
          display_id, route.source_group);
    }
    body.emplace_back(bot::Text{"[" + label + "]", {bot::TextStyle::Italic}});
    if (mixed_media || photos.size() <= 1)
      body.emplace_back(bot::Text{"\t", {}});
    contribute("sender", true);
  }
  bot::MessageBody outgoing;
  std::vector<std::optional<std::vector<ledger::Provenance>>> block_origins;
  std::optional<bot::MessageRef> reply;
  bool reply_resolved = false;
  if (mixed_media) {
    // Mixed attachments retain source order: never gather images across a
    // voice/video/file into an album or move unrelated text into its caption.
    std::size_t bytes = 0, photo_index = 0;
    for (const auto &photo : photos)
      bytes += photo.source.bytes.size();
    for (std::size_t index = 0; index < stored.message.body.size(); ++index) {
      const auto key = "body:" + std::to_string(index);
      inputs.push_back(key);
      if (omitted_market_faces.contains(index))
        continue;
      std::vector<ledger::Provenance> contribution{{"source", key}};
      const auto prefix = [&](bot::RichText &text) {
        if (!body.empty()) {
          text.insert(text.begin(), body.begin(), body.end());
          body.clear();
          contribution.insert(contribution.end(), origins.begin(),
                              origins.end());
        }
      };
      const auto *text =
          std::get_if<bot::ReceivedRichText>(&stored.message.body[index]);
      const auto literal = display_literals.find(index);
      if (text || literal != display_literals.end()) {
        auto rendered = text ? co_await render_text(*text, route)
                             : bot::RichText{bot::Text{literal->second, {}}};
        prefix(rendered);
        if (rendered.empty())
          continue;
        outgoing.emplace_back(std::move(rendered));
      } else if (const auto found = forwards.find(index);
                 found != forwards.end()) {
        if (outgoing.size() >= limits.maximum_messages)
          throw std::runtime_error("forward output budget exhausted");
        const auto remaining = limits.maximum_media_total_bytes - bytes;
        auto expanded = co_await prepare_forward(
            gateway_, blocking_, config_, found->second,
            bot::SendLimits{
                limits.maximum_messages - outgoing.size(),
                std::min(limits.maximum_media_item_bytes, remaining),
                remaining});
        prefix(std::get<bot::RichText>(expanded.front()));
        for (auto &block : expanded) {
          std::visit(
              [&](const auto &value) {
                if constexpr (!std::same_as<
                                  std::remove_cvref_t<decltype(value)>,
                                  bot::RichText>)
                  bytes +=
                      std::get<bot::BytesSource>(value.source).bytes.size();
              },
              block);
          outgoing.push_back(std::move(block));
          block_origins.emplace_back(contribution);
        }
        continue;
      } else {
        const auto *observed_media =
            std::get_if<bot::ReceivedMedia>(&stored.message.body[index]);
        const bool missing_image = observed_media == nullptr;
        // A market face's caption is its QQ summary such as "[xxx]", not
        // text the sender wrote; the sticker image alone represents it.
        const bool market_face =
            observed_media &&
            obcx::onebot11::bot::market_face_view(*observed_media).has_value();
        if (market_face && photos.at(photo_index).replaced()) {
          ++photo_index;
          continue; // Never stand in a placeholder image for a sticker.
        }
        bot::RichText caption;
        if (observed_media && !market_face)
          caption = co_await render_text(observed_media->caption, route);
        prefix(caption);
        const auto append_line = [&](std::string text) {
          if (!caption.empty())
            caption.emplace_back(bot::Text{"\n", {}});
          caption.emplace_back(bot::Text{std::move(text), {}});
        };
        if (missing_image || observed_media->kind == bot::MediaKind::Image) {
          auto &photo = photos.at(photo_index++);
          if (photo.status == PhotoPreparationStatus::Adjusted)
            append_line("ℹ️ 1 张图片尺寸已自动调整");
          if (photo.replaced())
            append_line("⚠️ 1 张图片暂时无法获取，已用占位图替换");
          if (photo.status == PhotoPreparationStatus::UnsupportedFormat)
            append_line("ℹ️ 1 张图片无法确认为受支持的静态格式");
          if (photo.kind == bot::MediaKind::Animation)
            outgoing.emplace_back(
                bot::Animation{std::move(photo.source), std::move(caption)});
          else
            outgoing.emplace_back(
                bot::Image{std::move(photo.source), std::move(caption)});
        } else {
          const auto &media = *observed_media;
          std::optional<bot::BytesSource> source;
          std::optional<bot::BotOperationError> unavailable;
          const auto remaining = limits.maximum_media_total_bytes - bytes;
          if (!remaining)
            throw std::runtime_error("attachment total byte budget exhausted");
          try {
            source = co_await read_bridge_attachment(
                gateway_, media,
                std::min(limits.maximum_media_item_bytes, remaining));
          } catch (const BridgeBotOperationFailure &failure) {
            // File read failure has always had a visible text representation.
            // It is chosen before any send, never after possible submission.
            if (media.kind != bot::MediaKind::File ||
                failure.error().code == bot::BotOperationErrorCode::Cancelled)
              throw;
            unavailable = failure.error();
          }
          if (unavailable) {
            append_line("[文件: " + media.metadata.filename.value_or("") +
                        " (" +
                        (media.metadata.size_bytes
                             ? std::to_string(*media.metadata.size_bytes)
                             : "") +
                        " bytes)]\n❌ 无法读取文件");
            outgoing.emplace_back(std::move(caption));
          } else {
            if (media.kind == bot::MediaKind::Voice)
              source = co_await prepare_voice(
                  blocking_, config_, std::move(*source),
                  std::min(limits.maximum_media_item_bytes, remaining));
            bytes += source->bytes.size();
            switch (media.kind) {
            case bot::MediaKind::Video:
              outgoing.emplace_back(
                  bot::Video{std::move(*source), std::move(caption)});
              break;
            case bot::MediaKind::Animation:
              outgoing.emplace_back(
                  bot::Animation{std::move(*source), std::move(caption)});
              break;
            case bot::MediaKind::Audio:
            case bot::MediaKind::Voice:
              outgoing.emplace_back(bot::Audio{
                  std::move(*source),
                  media.kind == bot::MediaKind::Voice ? bot::AudioKind::Voice
                                                      : bot::AudioKind::Audio,
                  std::move(caption)});
              break;
            case bot::MediaKind::File: {
              if (!source->metadata.filename)
                throw std::runtime_error(
                    "received file lacks a display filename");
              auto filename = *source->metadata.filename;
              outgoing.emplace_back(bot::File{
                  std::move(*source), std::move(filename), std::move(caption)});
              break;
            }
            default:
              throw std::invalid_argument(
                  "unexpected ordinary attachment kind");
            }
          }
        }
      }
      block_origins.emplace_back(std::move(contribution));
    }
  } else {
    for (std::size_t index = 0; index < stored.message.body.size(); ++index) {
      const auto *text =
          std::get_if<bot::ReceivedRichText>(&stored.message.body[index]);
      const auto literal = display_literals.find(index);
      contribute("body:" + std::to_string(index),
                 text != nullptr || literal != display_literals.end());
      if (!text && literal == display_literals.end())
        continue;
      auto rendered = text ? co_await render_text(*text, route)
                           : bot::RichText{bot::Text{literal->second, {}}};
      if (photos.size() > 1 && !body.empty() && !rendered.empty())
        body.emplace_back(bot::Text{"\n", {}});
      for (auto &span : rendered)
        body.push_back(std::move(span));
    }
    if (body.empty() && photos.empty())
      co_return result;
    if (photos.empty()) {
      outgoing.emplace_back(std::move(body));
      block_origins.emplace_back(std::move(origins));
    } else {
      if (stored.message.reply) {
        reply = co_await blocking_->run([repository = repository_,
                                         pair = pair_id_, target = route.target,
                                         observed = *stored.message.reply] {
          return resolve_bridge_delivery_reply(*repository, pair, target,
                                               observed);
        });
        reply_resolved = true;
      }
      // Match the installed Telegram recipe's actual grouping before copying
      // captions or computing recovery statistics. Animations are individual
      // messages; a replied-to first output is also individual. Only adjacent
      // static photos form groups (ordinary 11 photos remain 9+2).
      std::vector<std::size_t> batch_sizes;
      for (std::size_t start = 0; start < photos.size();) {
        std::size_t count = 1;
        if (photos[start].kind == bot::MediaKind::Image &&
            !(start == 0 && reply)) {
          std::size_t run = 0;
          while (start + run < photos.size() &&
                 photos[start + run].kind == bot::MediaKind::Image)
            ++run;
          count = std::min<std::size_t>(10, run);
          if (run > 10 && run - count == 1)
            --count;
        }
        batch_sizes.push_back(count);
        start += count;
      }
      // Reject a body that cannot fit the SDK wire budget before duplicating
      // its caption for each batch. This is not a resident-memory guarantee.
      const auto batches = batch_sizes.size();
      std::size_t known_bytes = 0;
      for (const auto &photo : photos)
        known_bytes += photo.source.bytes.size();
      for (const auto &span : body) {
        if (const auto *literal = std::get_if<bot::Text>(&span)) {
          if (literal->content.size() >
              (bot::detail::bot_maximum_bytes - known_bytes) / batches)
            throw std::runtime_error(
                "repeated photo caption exceeds SDK wire budget");
          known_bytes += literal->content.size() * batches;
        }
      }
      // SDK owns submission and first-output reply placement. No sends occur
      // per batch: every prepared image belongs to one logical delivery.
      std::size_t start = 0;
      for (const auto count : batch_sizes) {
        std::size_t adjusted = 0, replaced = 0, unsupported_format = 0;
        for (std::size_t i = start; i < start + count; ++i) {
          adjusted += photos[i].status == PhotoPreparationStatus::Adjusted;
          replaced += photos[i].replaced();
          unsupported_format +=
              photos[i].status == PhotoPreparationStatus::UnsupportedFormat;
        }
        for (std::size_t i = start; i < start + count; ++i) {
          const auto input = photo_indices[i];
          const auto *media =
              std::get_if<bot::ReceivedMedia>(&stored.message.body[input]);
          auto caption = media ? co_await render_text(media->caption, route)
                               : bot::RichText{};
          std::vector<ledger::Provenance> contribution{
              {"source", "body:" + std::to_string(input)}};
          const auto append_line = [&](std::string text) {
            if (!caption.empty())
              caption.emplace_back(bot::Text{"\n", {}});
            caption.emplace_back(bot::Text{std::move(text), {}});
          };
          if (i == start) {
            if (!caption.empty() && !body.empty())
              caption.emplace_back(bot::Text{"\n", {}});
            caption.insert(caption.end(), body.begin(), body.end());
            contribution.insert(contribution.end(), origins.begin(),
                                origins.end());
            if (adjusted)
              append_line("ℹ️ " + std::to_string(adjusted) +
                          " 张图片尺寸已自动调整");
            if (replaced)
              append_line("⚠️ " + std::to_string(replaced) +
                          " 张图片暂时无法获取，已用占位图替换");
            if (unsupported_format)
              append_line("ℹ️ " + std::to_string(unsupported_format) +
                          " 张图片无法确认为受支持的静态格式");
            if (adjusted || replaced)
              for (std::size_t j = start + 1; j < start + count; ++j)
                contribution.push_back(
                    {"source", "body:" + std::to_string(photo_indices[j])});
          }
          if (photos[i].kind == bot::MediaKind::Animation)
            outgoing.emplace_back(bot::Animation{std::move(photos[i].source),
                                                 std::move(caption)});
          else
            outgoing.emplace_back(
                bot::Image{std::move(photos[i].source), std::move(caption)});
          block_origins.emplace_back(std::move(contribution));
        }
        start += count;
      }
    }
  }
  if (outgoing.empty())
    co_return result;
  if (outgoing.size() > bot::detail::bot_maximum_items)
    throw std::runtime_error("prepared body exceeds output budget");
  // The expansion ceiling is not a submission allowance: admit only the
  // number of actually prepared blocks. Long text/captions are not truncated.
  if (has_forward)
    limits.maximum_messages = outgoing.size();
  if (stored.message.reply && !reply_resolved) {
    reply = co_await blocking_->run([repository = repository_, pair = pair_id_,
                                     target = route.target,
                                     observed = *stored.message.reply] {
      return resolve_bridge_delivery_reply(*repository, pair, target, observed);
    });
  }
  BridgeDeliveryRequest request{
      delivery_id,
      delivery_id + ":1",
      pair_id_,
      {{"source", stored.message.message, stored.message, std::nullopt,
        std::move(inputs)}},
      {route.target, std::move(outgoing), std::move(reply),
       media_count == 0 && !has_forward ? bot::DeliveryPolicy::SingleMessageOnly
                                        : bot::DeliveryPolicy::AllowSplit,
       limits},
      std::move(block_origins),
      BridgeQuoteAnchor::FirstConfirmed,
      stored.message.received_at.unix_milliseconds,
      max_attempts_,
      std::nullopt,
      max_attempts_ > 1 ? std::optional{BridgeDeliveryRetryPolicy{
                              config_->message_retry_base_interval_sec,
                              config_->max_retry_interval_sec}}
                        : std::nullopt,
      std::move(publication)};
  result.bot_delivery = co_await send_bridge_delivery(
      gateway_, repository_, blocking_, references_, std::move(request));
  co_return result;
}
} // namespace bridge::qq
