#include "path_manager.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace bridge {
namespace {
namespace pm = obcx::path_mapping;
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
} // namespace

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
  const fs::path path = to_host_path(relative_path);
  if (path == fs::path(host_base_) || path.filename().empty()) {
    throw std::runtime_error("invalid bridge media filename");
  }
  const Directory parent(path.parent_path(), false);
  const auto leaf = path.filename();
  int fd = ::openat(parent.get(), leaf.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0666);
  if (fd < 0) {
    throw std::runtime_error("cannot exclusively create bridge media file");
  }
  try {
    while (!bytes.empty()) {
      const auto count = ::write(fd, bytes.data(), bytes.size());
      if (count < 0 && errno == EINTR) {
        continue;
      }
      if (count <= 0) {
        throw std::runtime_error("cannot write bridge media file");
      }
      bytes = bytes.subspan(static_cast<std::size_t>(count));
    }
    if (::close(std::exchange(fd, -1)) != 0) {
      throw std::runtime_error("cannot close bridge media file");
    }
  } catch (...) {
    if (fd >= 0) {
      ::close(fd);
    }
    ::unlinkat(parent.get(), leaf.c_str(), 0);
    throw;
  }
  return path.string();
}
auto PathManager::convert_file(
    const std::string &host_source, const std::string &host_destination,
    const std::function<bool(const std::string &, const std::string &)>
        &convert) const -> bool {
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
    return false;
  }
  const OwnedDescriptor output{
      ::openat(scratch.directory.value, "output.gif",
               O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  if (output.value < 0 || ::fstat(output.value, &status) != 0 ||
      !S_ISREG(status.st_mode) || status.st_nlink != 1) {
    return false;
  }
  // Link the inspected inode, not a pathname that could be swapped. linkat
  // refuses an existing destination, including a symlink. Scratch is on the
  // destination filesystem so successful publication needs no copying.
  const auto output_handle = "/proc/self/fd/" + std::to_string(output.value);
  if (::linkat(AT_FDCWD, output_handle.c_str(), output_parent.get(),
               destination.filename().c_str(), AT_SYMLINK_FOLLOW) != 0) {
    throw std::runtime_error("cannot exclusively publish bridge conversion");
  }
  return true;
}
} // namespace bridge
