#include "qq/voice_normalizer.hpp"
#include "qq/photo_normalizer.hpp"

#include <SKP_Silk_SDK_API.h>
#include <SKP_Silk_structs.h>
#include <core/bot/validation.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace bridge::qq {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
// Reuse the SDK's owned-media ceiling for decoded PCM as well as input. At
// mono 24 kHz s16 this also bounds duration; GIF duration is not a voice limit.
constexpr auto pcm_limit = obcx::bot::detail::domain_maximum_bytes;
constexpr int sample_rate = 24000;

[[noreturn]] void fail(const char *reason) {
  throw VoicePreparationFailure(reason);
}
void check_deadline(Clock::time_point deadline) {
  if (Clock::now() >= deadline)
    fail("processing deadline exceeded");
}
class TemporaryVoiceDirectory {
public:
  TemporaryVoiceDirectory() {
    auto pattern =
        (fs::absolute(fs::temp_directory_path()) / "obcx-qq-voice-XXXXXX")
            .string();
    // mkdtemp atomically creates an unpredictable, owner-only (0700) directory.
    if (!::mkdtemp(pattern.data()))
      fail("private temporary directory unavailable");
    root = std::move(pattern);
  }
  ~TemporaryVoiceDirectory() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  TemporaryVoiceDirectory(const TemporaryVoiceDirectory &) = delete;
  auto operator=(const TemporaryVoiceDirectory &)
      -> TemporaryVoiceDirectory & = delete;
  fs::path root;
};
void write_file(const fs::path &path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream)
    fail("temporary write failed");
}
auto read_file(const fs::path &path, std::size_t maximum) -> std::string {
  const auto size = fs::file_size(path);
  if (!size || size > maximum)
    fail("encoded output exceeds byte budget or is empty");
  std::string result(size, '\0');
  std::ifstream stream(path, std::ios::binary);
  stream.read(result.data(), static_cast<std::streamsize>(size));
  if (!stream || stream.peek() != std::char_traits<char>::eof())
    fail("temporary read failed");
  return result;
}
auto le(std::string_view data, std::size_t pos, std::size_t count)
    -> std::uint64_t {
  if (pos > data.size() || count > data.size() - pos)
    fail("truncated framing");
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < count; ++i)
    value |= std::uint64_t{static_cast<unsigned char>(data[pos + i])}
             << (8 * i);
  return value;
}
// Validate every page CRC, sequence, lacing/continuation, a single Opus logical
// stream and EOS. Magic alone (or an ffmpeg exit code on a clipped file) is not
// sufficient. The complete audio is independently decoded below.
void validate_ogg_opus(std::string_view data, bool require_mono,
                       Clock::time_point deadline) {
  std::size_t cursor = 0, packets = 0;
  std::uint64_t serial = 0, sequence = 0, granule = 0, preskip = 0;
  std::string packet;
  bool eos = false;
  while (cursor < data.size()) {
    check_deadline(deadline);
    const auto page = data.substr(cursor);
    if (page.size() < 27 || !page.starts_with("OggS") || page[4] != 0 || eos)
      fail("invalid Ogg framing");
    const auto flags = le(page, 5, 1);
    const auto segments = le(page, 26, 1);
    const auto header = 27 + segments;
    if (header > page.size() || (flags & ~7U) ||
        bool(flags & 1U) != !packet.empty() ||
        bool(flags & 2U) != (sequence == 0) || le(page, 18, 4) != sequence)
      fail("invalid Ogg stream sequence");
    if (sequence == 0)
      serial = le(page, 14, 4);
    if (le(page, 14, 4) != serial)
      fail("multiple Ogg streams are not voice");
    std::size_t size = header;
    for (std::size_t i = 0; i < segments; ++i)
      size += le(page, 27 + i, 1);
    if (size > page.size())
      fail("truncated Ogg page");
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
      crc ^=
          std::uint32_t{i >= 22 && i < 26 ? 0U
                                          : static_cast<unsigned char>(page[i])}
          << 24;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc << 1) ^ ((crc & 0x80000000U) ? 0x04c11db7U : 0U);
    }
    if (crc != le(page, 22, 4))
      fail("Ogg checksum mismatch");
    auto offset = header;
    for (std::size_t i = 0; i < segments; ++i) {
      const auto length = le(page, 27 + i, 1);
      packet.append(page.substr(offset, length));
      offset += length;
      if (length == 255)
        continue;
      if (packets == 0) {
        if (packet.size() != 19 || !packet.starts_with("OpusHead") ||
            packet[8] != 1 ||
            (packet[9] != 1 && (require_mono || packet[9] != 2)) ||
            packet[18] != 0)
          fail("unsupported Ogg Opus header");
        preskip = le(packet, 10, 2);
      } else if (packets == 1 && !packet.starts_with("OpusTags")) {
        fail("missing Opus tags");
      } else if (packet.empty()) {
        fail("empty Opus packet");
      }
      ++packets;
      packet.clear();
    }
    granule = le(page, 6, 8);
    eos = flags & 4U;
    cursor += size;
    ++sequence;
  }
  if (!eos || !packet.empty() || packets < 3 || granule <= preskip ||
      granule == UINT64_MAX)
    fail("incomplete Ogg Opus stream");
}

auto decode_silk(std::string_view data, const fs::path &pcm,
                 Clock::time_point deadline) -> std::size_t {
  const bool tencent = data.starts_with("\x02#!SILK_V3");
  std::size_t cursor = tencent ? 10 : 9;
  SKP_int32 state_size = 0;
  if (SKP_Silk_SDK_Get_Decoder_Size(&state_size) || state_size <= 0)
    fail("SILK decoder initialization failed");
  std::unique_ptr<void, decltype(&std::free)> state(
      std::calloc(1, static_cast<std::size_t>(state_size)), &std::free);
  if (!state || SKP_Silk_SDK_InitDecoder(state.get()))
    fail("SILK decoder initialization failed");
  SKP_SILK_SDK_DecControlStruct control{};
  control.API_sampleRate = sample_rate;
  std::ofstream stream(pcm, std::ios::binary | std::ios::trunc);
  std::size_t total = 0;
  bool terminated = false;
  while (cursor < data.size()) {
    check_deadline(deadline);
    const auto size = le(data, cursor, 2);
    cursor += 2;
    if (size == 0xffff && cursor == data.size()) {
      terminated = true;
      break;
    }
    // The SDK range coder reads four initial bytes and permits at most 1024.
    // Never request packet-loss concealment or ignore a decoder error.
    if (size < 4 || size > 1024 || size > data.size() - cursor)
      fail("invalid or truncated SILK packet");
    int frames = 0;
    do {
      check_deadline(deadline);
      if (++frames > SILK_MAX_FRAMES_PER_PACKET)
        fail("too many SILK frames");
      std::array<SKP_int16, sample_rate / 50> samples{};
      SKP_int16 count = samples.size();
      const auto error = SKP_Silk_SDK_Decode(
          state.get(), &control, 0,
          reinterpret_cast<const SKP_uint8 *>(data.data() + cursor),
          static_cast<int>(size), samples.data(), &count);
      // The legacy API can overwrite a range-decoder error when initializing
      // its resampler. Check the pinned SDK's state too, rather than accepting
      // concealed PCM as a successful decode.
      const auto &decoder =
          *static_cast<const SKP_Silk_decoder_state *>(state.get());
      if (error || decoder.sRC.error ||
          count != static_cast<SKP_int16>(samples.size()))
        fail("SILK decoding rejected a packet");
      if (samples.size() * 2 > pcm_limit - total)
        fail("decoded voice exceeds PCM budget");
      // Explicit little endian PCM, independent of host byte order.
      for (auto sample : samples) {
        const auto value = static_cast<std::uint16_t>(sample);
        stream.put(static_cast<char>(value & 0xff));
        stream.put(static_cast<char>(value >> 8));
      }
      if (!stream)
        fail("PCM write failed");
      total += samples.size() * 2;
    } while (control.moreInternalDecoderFrames);
    // The API stops at five frames even when the packet says there are more.
    // Reject that incomplete packet instead of silently dropping its tail.
    if (static_cast<const SKP_Silk_decoder_state *>(state.get())
            ->FrameTermination == SKP_SILK_MORE_FRAMES)
      fail("incomplete SILK packet frames");
    cursor += size;
  }
  stream.close();
  if (!stream || !total || (!tencent && !terminated))
    fail("incomplete SILK stream or PCM write failure");
  return total;
}

auto demuxer(std::string_view data, Clock::time_point deadline) -> std::string {
  if (data.starts_with("#!AMR\n") || data.starts_with("#!AMR-WB\n"))
    return "amr";
  if (data.size() >= 12 && data.starts_with("RIFF") &&
      data.substr(8, 4) == "WAVE") {
    if (le(data, 4, 4) + 8 != data.size())
      fail("incomplete WAV container");
    return "wav";
  }
  if (data.starts_with("OggS")) {
    validate_ogg_opus(data, false, deadline);
    return "ogg";
  }
  if (data.starts_with("fLaC"))
    return "flac";
  if (data.size() >= 12 && data.substr(4, 4) == "ftyp")
    return "mov";
  if (data.starts_with("ID3") ||
      (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xff &&
       (static_cast<unsigned char>(data[1]) & 0xe6) == 0xe2))
    return "mp3";
  fail("unsupported voice byte format");
}
void run(const std::string &ffmpeg, std::vector<std::string> arguments,
         Clock::time_point deadline) {
  check_deadline(deadline);
  std::vector<std::string> command{ffmpeg,
                                   "-nostdin",
                                   "-hide_banner",
                                   "-loglevel",
                                   "error",
                                   "-xerror",
                                   "-max_alloc",
                                   std::to_string(pcm_limit),
                                   "-threads",
                                   "1",
                                   "-filter_threads",
                                   "1",
                                   "-protocol_whitelist",
                                   "file",
                                   "-err_detect",
                                   "explode"};
  command.insert(command.end(), arguments.begin(), arguments.end());
  const auto result = PhotoNormalizer::run_process(
      command, std::chrono::duration_cast<std::chrono::milliseconds>(
                   deadline - Clock::now()));
  // Diagnostics may contain paths/metadata; never propagate them to logs.
  if (result.timed_out || result.exit_code != 0)
    fail("ffmpeg conversion or validation failed");
}
auto pcm_size(const fs::path &path) -> std::size_t {
  const auto size = fs::file_size(path);
  if (!size || size > pcm_limit || size % 2)
    fail("decoded voice exceeds PCM budget or is empty");
  return size;
}
} // namespace

auto normalize_voice(std::string_view input, const std::string &ffmpeg_path,
                     std::size_t maximum_bytes) -> NormalizedVoice {
  try {
    if (ffmpeg_path.empty() || !maximum_bytes || maximum_bytes > pcm_limit ||
        input.empty() || input.size() > maximum_bytes)
      fail("invalid service or byte budget");
    // One shared deadline, not a fresh timeout for each conversion stage.
    const auto deadline = Clock::now() + kTelegramPhotoNormalizationTimeout;
    TemporaryVoiceDirectory temporary;
    const auto pcm = temporary.root / "decoded.pcm";
    const auto encoded = temporary.root / "voice.ogg";
    std::size_t decoded_size;
    if (input.starts_with("\x02#!SILK_V3") || input.starts_with("#!SILK_V3")) {
      decoded_size = decode_silk(input, pcm, deadline);
    } else {
      const auto format = demuxer(input, deadline);
      const auto source = temporary.root / "source";
      write_file(source, input);
      std::vector<std::string> arguments{"-f", format};
      if (format == "mov") {
        // MP4 can carry external data references. Even local-file references
        // must not escape our private input; do not rely on demuxer defaults.
        arguments.insert(arguments.end(),
                         {"-enable_drefs", "0", "-use_absolute_path", "0"});
      }
      arguments.insert(arguments.end(), {"-i",
                                         source.string(),
                                         "-map",
                                         "0:a:0",
                                         "-vn",
                                         "-sn",
                                         "-dn",
                                         "-ac",
                                         "1",
                                         "-ar",
                                         "24000",
                                         "-c:a",
                                         "pcm_s16le",
                                         "-threads",
                                         "1",
                                         "-f",
                                         "s16le",
                                         "-fs",
                                         std::to_string(pcm_limit + 1),
                                         "-y",
                                         pcm.string()});
      run(ffmpeg_path, std::move(arguments), deadline);
      decoded_size = pcm_size(pcm);
    }
    run(ffmpeg_path,
        {"-f",
         "s16le",
         "-ar",
         "24000",
         "-ac",
         "1",
         "-i",
         pcm.string(),
         "-map",
         "0:a:0",
         "-map_metadata",
         "-1",
         "-c:a",
         "libopus",
         "-b:a",
         "32000",
         "-application",
         "voip",
         "-threads",
         "1",
         "-f",
         "ogg",
         "-fs",
         std::to_string(maximum_bytes + 1),
         "-y",
         encoded.string()},
        deadline);
    auto ogg = read_file(encoded, maximum_bytes);
    validate_ogg_opus(ogg, true, deadline);
    const auto verified = temporary.root / "verified.pcm";
    run(ffmpeg_path, {"-f",       "ogg",
                      "-c:a",     "opus",
                      "-i",       encoded.string(),
                      "-map",     "0:a:0",
                      "-ac",      "1",
                      "-ar",      "24000",
                      "-c:a",     "pcm_s16le",
                      "-threads", "1",
                      "-f",       "s16le",
                      "-fs",      std::to_string(pcm_limit + 1),
                      "-y",       verified.string()},
        deadline);
    if (pcm_size(verified) != decoded_size)
      fail("Opus verification changed decoded duration");
    check_deadline(deadline);
    return {std::move(ogg), decoded_size * 1000 / (2 * sample_rate)};
  } catch (const VoicePreparationFailure &) {
    throw;
  } catch (const std::exception &) {
    // Filesystem errors can embed private temp paths; keep errors explicit but
    // content/locator free. Cancellation is handled outside this worker.
    fail("decoder or temporary I/O failed");
  }
}
} // namespace bridge::qq
