#include "path_manager.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <common/logger.hpp>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace bridge {
namespace {
namespace pm = path_mapping;
namespace fs = std::filesystem;

template <typename T> auto require(std::expected<T, pm::Error> result) -> T {
  if (!result) {
    throw std::runtime_error("bridge media path: " +
                             std::string(pm::error_name(result.error())));
  }
  return std::move(*result);
}

struct OwnedDescriptor final {
  int value;
  ~OwnedDescriptor() {
    if (value >= 0) {
      ::close(value);
    }
  }
};

class Directory final {
public:
  explicit Directory(const fs::path &absolute, bool create)
      : fd_{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC)} {
    if (fd_.value < 0) {
      throw std::runtime_error("cannot open bridge media root");
    }
    for (const auto &part : absolute.relative_path()) {
      if (create && ::mkdirat(fd_.value, part.c_str(), 0777) != 0 &&
          errno != EEXIST) {
        throw std::runtime_error("cannot create bridge media directory");
      }
      const int next =
          ::openat(fd_.value, part.c_str(),
                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      ::close(std::exchange(fd_.value, next));
      if (fd_.value < 0) {
        throw std::runtime_error(
            "unsafe or inaccessible bridge media directory");
      }
    }
  }
  Directory(const Directory &) = delete;
  auto operator=(const Directory &) -> Directory & = delete;
  auto get() const -> int { return fd_.value; }

private:
  OwnedDescriptor fd_;
};

class WriteScratch {
public:
  explicit WriteScratch(int parent) : parent_(parent) {
    std::array<unsigned char, 16> random{};
    std::size_t filled = 0;
    while (filled < random.size()) {
      const auto count =
          ::getrandom(random.data() + filled, random.size() - filled, 0);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        throw std::runtime_error("cannot allocate cache scratch identity");
      filled += static_cast<std::size_t>(count);
    }
    name_ = ".prepare-";
    constexpr std::string_view hex = "0123456789abcdef";
    for (const auto byte : random) {
      name_ += hex[byte >> 4];
      name_ += hex[byte & 15];
    }
    if (::mkdirat(parent_, name_.c_str(), 0700) != 0)
      throw std::runtime_error("cannot create private cache scratch");
    directory_.value =
        ::openat(parent_, name_.c_str(),
                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory_.value < 0) {
      ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR);
      throw std::runtime_error("cannot open private cache scratch");
    }
  }
  ~WriteScratch() {
    ::unlinkat(directory_.value, "input", 0);
    ::unlinkat(parent_, name_.c_str(), AT_REMOVEDIR);
  }
  WriteScratch(const WriteScratch &) = delete;
  auto operator=(const WriteScratch &) -> WriteScratch & = delete;
  auto get() const -> int { return directory_.value; }

private:
  int parent_;
  std::string name_;
  OwnedDescriptor directory_{-1};
};
} // namespace

struct PathManager::CreatedFile::State {
  std::string path;
  std::string leaf;
  OwnedDescriptor parent;
  OwnedDescriptor file;
  bool published{false};
  bool retained{false};

  State(std::string host_path, int directory, int descriptor)
      : path(std::move(host_path)), leaf(fs::path(path).filename().string()),
        parent{::fcntl(directory, F_DUPFD_CLOEXEC, 0)},
        file{descriptor < 0 ? -1 : ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0)} {
    if (parent.value < 0 || (descriptor >= 0 && file.value < 0))
      throw std::runtime_error("cannot pin created bridge cache file");
  }
  ~State() {
    if (!published || retained)
      return;
    struct stat owned{}, entry{};
    // The cache namespace is actor-owned and immutable after publication.
    // Never unlink a replacement inode or traverse a changed directory path.
    if (::fstat(file.value, &owned) == 0 &&
        ::fstatat(parent.value, leaf.c_str(), &entry, AT_SYMLINK_NOFOLLOW) ==
            0 &&
        owned.st_dev == entry.st_dev && owned.st_ino == entry.st_ino &&
        S_ISREG(entry.st_mode) &&
        ::unlinkat(parent.value, leaf.c_str(), 0) == 0)
      return;
    try {
      OBCX_WARN("Unretained bridge cache inode could not be removed; no path "
                "fallback");
    } catch (...) {
    }
  }
};

PathManager::CreatedFile::CreatedFile(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
PathManager::CreatedFile::CreatedFile(CreatedFile &&) noexcept = default;
auto PathManager::CreatedFile::operator=(CreatedFile &&) noexcept
    -> CreatedFile & = default;
PathManager::CreatedFile::~CreatedFile() = default;
auto PathManager::CreatedFile::path() const -> const std::string & {
  return state_->path;
}
auto PathManager::CreatedFile::read(std::size_t maximum_bytes) const
    -> std::vector<std::uint8_t> {
  return PathManager::read_descriptor_bounded(state_->file.value,
                                              maximum_bytes);
}
void PathManager::CreatedFile::retain() noexcept { state_->retained = true; }

PathManager::PathManager(std::string installation, const std::string &host_base,
                         const std::string &container_base)
    : installation_(std::move(installation)),
      mapper_(require(
          pm::Mapper::create({{installation_, host_base, container_base}}))),
      host_base_(
          require(mapper_.configuration(installation_)).host_root.string()),
      container_base_(
          require(mapper_.configuration(installation_)).peer_root.string()) {}

auto PathManager::to_host_path(const std::string &relative_path) const
    -> std::string {
  return require(mapper_.relative(installation_, relative_path)).host.string();
}

auto PathManager::to_container_path(const std::string &relative_path) const
    -> std::string {
  return require(mapper_.relative(installation_, relative_path)).peer.string();
}

auto PathManager::get_host_base() const -> const std::string & {
  return host_base_;
}
auto PathManager::get_container_base() const -> const std::string & {
  return container_base_;
}

auto PathManager::host_to_container_absolute(const std::string &path) const
    -> std::string {
  return require(mapper_.map(installation_, path)).peer.string();
}

auto PathManager::container_to_host_absolute(const std::string &path) const
    -> std::string {
  return require(mapper_.peer_to_host(installation_, path)).host.string();
}

auto PathManager::file_uri(const std::string &path) const -> std::string {
  return require(mapper_.inspect_existing_file(installation_, path)).file_uri;
}

auto PathManager::ensure_directory(const std::string &relative_path) const
    -> bool {
  try {
    const Directory directory(to_host_path(relative_path), true);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

auto PathManager::write_new_file(const std::string &relative_path,
                                 std::span<const std::byte> bytes) const
    -> std::string {
  auto owned = write_owned_file(relative_path, bytes);
  auto path = owned.path();
  owned.retain();
  return path;
}

auto PathManager::write_owned_file(const std::string &relative_path,
                                   std::span<const std::byte> bytes) const
    -> CreatedFile {
  const fs::path path = to_host_path(relative_path);
  if (path == fs::path(host_base_) || path.filename().empty())
    throw std::runtime_error("invalid bridge media filename");
  const Directory parent(path.parent_path(), false);
  // All allocating ownership setup precedes the exclusive creation.
  auto state =
      std::make_unique<CreatedFile::State>(path.string(), parent.get(), -1);
  const WriteScratch scratch(parent.get());
  state->file.value =
      ::openat(scratch.get(), "input",
               O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666);
  if (state->file.value < 0)
    throw std::runtime_error("cannot exclusively create bridge media file");
  while (!bytes.empty()) {
    const auto count = ::write(state->file.value, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      throw std::runtime_error("cannot write bridge media file");
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
  // A cache reader must never observe an original while it is being written.
  // Publish the fully written pinned inode, without replacing an old entry.
  const auto handle = "/proc/self/fd/" + std::to_string(state->file.value);
  if (::linkat(AT_FDCWD, handle.c_str(), state->parent.value,
               state->leaf.c_str(), AT_SYMLINK_FOLLOW) != 0)
    throw std::runtime_error("cannot exclusively publish bridge media file");
  state->published = true;
  return CreatedFile(std::move(state));
}
auto PathManager::read_file_bounded(const std::string &host_path,
                                    std::size_t maximum_bytes) const
    -> std::vector<std::uint8_t> {
  if (!maximum_bytes)
    throw std::invalid_argument("bridge cache read requires a byte budget");
  const auto path = require(mapper_.map(installation_, host_path)).host;
  if (path == fs::path(host_base_) || path.filename().empty())
    throw std::runtime_error("invalid bridge cache file path");
  const Directory parent(path.parent_path(), false);
  const OwnedDescriptor file{
      ::openat(parent.get(), path.filename().c_str(),
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  return read_descriptor_bounded(file.value, maximum_bytes);
}

auto PathManager::read_descriptor_bounded(int file, std::size_t maximum_bytes)
    -> std::vector<std::uint8_t> {
  if (!maximum_bytes)
    throw std::invalid_argument("bridge cache read requires a byte budget");
  struct stat before{};
  if (file < 0 || ::fstat(file, &before) != 0 || !S_ISREG(before.st_mode) ||
      before.st_size < 0)
    throw std::runtime_error("invalid bridge cache file");
  if (static_cast<std::uintmax_t>(before.st_size) > maximum_bytes)
    throw std::runtime_error("bridge cache file exceeds byte budget");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(before.st_size));
  std::array<std::uint8_t, 65536> chunk{};
  while (true) {
    // One non-retained byte probes EOF when exactly at the bound. stat alone
    // cannot prove the read size if an external writer changes the file.
    const auto remaining = maximum_bytes - bytes.size();
    const auto wanted = remaining ? std::min(remaining, chunk.size()) : 1;
    const auto count =
        ::pread(file, chunk.data(), wanted, static_cast<off_t>(bytes.size()));
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      throw std::runtime_error("cannot read bridge cache file");
    if (count == 0)
      break;
    if (static_cast<std::size_t>(count) > remaining)
      throw std::runtime_error("bridge cache file exceeds byte budget");
    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
  }
  struct stat after{};
  if (::fstat(file, &after) != 0 || before.st_size != after.st_size ||
      before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
      before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      bytes.size() != static_cast<std::uintmax_t>(after.st_size))
    throw std::runtime_error("bridge cache file changed during read");
  if (bytes.empty())
    throw std::runtime_error("bridge cache file is empty");
  return bytes;
}

auto PathManager::convert_verified_file(
    const std::string &host_source, const std::string &host_destination,
    std::span<const std::uint8_t> expected_source,
    std::size_t maximum_output_bytes,
    const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
    const std::function<bool(const std::string &, const std::string &)>
        &convert) const -> bool {
  auto created =
      convert_owned_file(host_source, host_destination, expected_source,
                         maximum_output_bytes, validate_output, convert);
  if (!created)
    return false;
  created->retain();
  return true;
}

auto PathManager::convert_owned_file(
    const std::string &host_source, const std::string &host_destination,
    std::span<const std::uint8_t> expected_source,
    std::size_t maximum_output_bytes,
    const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
    const std::function<bool(const std::string &, const std::string &)>
        &convert) const -> std::optional<CreatedFile> {
  if (expected_source.empty() || !maximum_output_bytes || !validate_output ||
      !convert)
    throw std::invalid_argument(
        "verified conversion requires bytes and output validation");
  return convert_file_impl(host_source, host_destination, expected_source,
                           maximum_output_bytes, validate_output, convert);
}

auto PathManager::convert_file_impl(
    const std::string &host_source, const std::string &host_destination,
    std::optional<std::span<const std::uint8_t>> expected_source,
    std::size_t maximum_output_bytes,
    const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
    const std::function<bool(const std::string &, const std::string &)>
        &convert) const -> std::optional<CreatedFile> {
  const auto source = require(mapper_.map(installation_, host_source)).host;
  const auto destination =
      require(mapper_.map(installation_, host_destination)).host;
  if (source == fs::path(host_base_) || destination == fs::path(host_base_)) {
    throw std::runtime_error("invalid bridge conversion path");
  }
  const Directory input_parent(source.parent_path(), false);
  const OwnedDescriptor input{
      ::openat(input_parent.get(), source.filename().c_str(),
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  struct stat status{};
  if (input.value < 0 || ::fstat(input.value, &status) != 0 ||
      !S_ISREG(status.st_mode)) {
    throw std::runtime_error("invalid bridge conversion input");
  }
  if (expected_source) {
    if (status.st_size < 0 ||
        static_cast<std::uintmax_t>(status.st_size) != expected_source->size())
      throw std::runtime_error("bridge conversion source changed");
    std::array<std::uint8_t, 65536> chunk{};
    std::size_t offset = 0;
    while (offset < expected_source->size()) {
      const auto wanted =
          std::min(chunk.size(), expected_source->size() - offset);
      const auto count = ::pread(input.value, chunk.data(), wanted,
                                 static_cast<off_t>(offset));
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0 || !std::equal(chunk.begin(), chunk.begin() + count,
                                    expected_source->begin() + offset))
        throw std::runtime_error("bridge conversion source changed");
      offset += static_cast<std::size_t>(count);
    }
    struct stat after{};
    if (::fstat(input.value, &after) != 0 || after.st_size != status.st_size ||
        after.st_mtim.tv_sec != status.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != status.st_mtim.tv_nsec)
      throw std::runtime_error("bridge conversion source changed");
  }
  const auto source_status = status;
  const Directory output_parent(destination.parent_path(), true);
  std::array<unsigned char, 16> random{};
  std::size_t filled = 0;
  while (filled < random.size()) {
    const auto count =
        ::getrandom(random.data() + filled, random.size() - filled, 0);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      throw std::runtime_error("cannot allocate bridge conversion identity");
    }
    filled += static_cast<std::size_t>(count);
  }
  std::string name = ".convert-";
  constexpr std::string_view hex = "0123456789abcdef";
  for (const auto byte : random) {
    name += hex[byte >> 4];
    name += hex[byte & 15];
  }
  if (::mkdirat(output_parent.get(), name.c_str(), 0700) != 0) {
    throw std::runtime_error(
        "cannot create private bridge conversion directory");
  }
  struct Scratch final {
    int parent;
    std::string name;
    OwnedDescriptor directory;
    ~Scratch() {
      if (directory.value >= 0) {
        ::unlinkat(directory.value, "output.gif", 0);
      }
      ::unlinkat(parent, name.c_str(), AT_REMOVEDIR);
    }
  } scratch{output_parent.get(), std::move(name), {-1}};
  scratch.directory.value =
      ::openat(output_parent.get(), scratch.name.c_str(),
               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (scratch.directory.value < 0) {
    throw std::runtime_error("cannot open private bridge conversion directory");
  }
  // Address the parent's live descriptors: child exec does not inherit them,
  // and configured paths/filenames cannot turn into subprocess arguments.
  const auto descriptors = "/proc/" + std::to_string(::getpid()) + "/fd/";
  if (!convert(descriptors + std::to_string(input.value),
               descriptors + std::to_string(scratch.directory.value) +
                   "/output.gif")) {
    return std::nullopt;
  }
  if (expected_source) {
    struct stat after{};
    if (::fstat(input.value, &after) != 0 ||
        after.st_size != source_status.st_size ||
        after.st_mtim.tv_sec != source_status.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != source_status.st_mtim.tv_nsec)
      throw std::runtime_error(
          "bridge conversion source changed during decode");
  }
  const OwnedDescriptor output{
      ::openat(scratch.directory.value, "output.gif",
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  if (output.value < 0 || ::fstat(output.value, &status) != 0 ||
      !S_ISREG(status.st_mode) || status.st_nlink != 1) {
    return std::nullopt;
  }
  // Validate while still private: a truncated/oversized/mismatched conversion
  // must not occupy the stable cache filename and poison later preparation.
  if (expected_source && !validate_output(read_descriptor_bounded(
                             output.value, maximum_output_bytes)))
    return std::nullopt;
  // Link the inspected inode, not a pathname that could be swapped. linkat
  // refuses an existing destination, including a symlink. Scratch is on the
  // destination filesystem so successful publication needs no copying.
  auto state = std::make_unique<CreatedFile::State>(
      destination.string(), output_parent.get(), output.value);
  const auto output_handle = "/proc/self/fd/" + std::to_string(output.value);
  if (::linkat(AT_FDCWD, output_handle.c_str(), output_parent.get(),
               destination.filename().c_str(), AT_SYMLINK_FOLLOW) != 0) {
    throw std::runtime_error("cannot exclusively publish bridge conversion");
  }
  state->published = true;
  return CreatedFile(std::move(state));
}
} // namespace bridge
