#include "qq/voice_preparer.hpp"
#include "qq/voice_normalizer.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>

namespace bridge::qq {
auto prepare_voice(std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                   std::shared_ptr<const BridgeConfig> config,
                   obcx::bot::BytesSource source, std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::bot::BytesSource> {
  if (!blocking || !config || !maximum_bytes ||
      maximum_bytes > config->qq_media_download_max_bytes)
    throw VoicePreparationFailure("owned services or budget unavailable");
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  if (cancellation.cancelled() != boost::asio::cancellation_type::none)
    throw boost::system::system_error(boost::asio::error::operation_aborted);
  auto prepared = co_await blocking->run(
      [config, maximum_bytes, source = std::move(source)]() mutable {
        const std::string_view input{
            reinterpret_cast<const char *>(source.bytes.data()),
            source.bytes.size()};
        auto normalized =
            normalize_voice(input, config->ffmpeg_path, maximum_bytes);
        obcx::bot::MediaMetadata metadata;
        metadata.filename = "qq-voice.ogg";
        metadata.mime_type = "audio/ogg";
        metadata.size_bytes = normalized.ogg.size();
        metadata.duration_ms = normalized.duration_ms;
        obcx::bot::BytesSource result{
            {normalized.ogg.begin(), normalized.ogg.end()},
            std::move(metadata)};
        result.validate();
        return result;
      });
  // BlockingExecutor drains accepted work; do not publish its result after
  // cancellation. No detached work or actor captures outlive this await.
  if (cancellation.cancelled() != boost::asio::cancellation_type::none)
    throw boost::system::system_error(boost::asio::error::operation_aborted);
  co_return prepared;
}
} // namespace bridge::qq
