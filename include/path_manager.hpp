#pragma once

#include <obcx/path_mapping.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bridge {

// Bridge-owned filesystem facade. Mapping is delegated to the independent
// library; creation and eventual cleanup remain Bridge responsibilities.
class PathManager {
public:
  // Worker-local ownership of an exclusively created inode and its anchored
  // directory entry. Never return this object across an await: finish reads,
  // conversion and optional cache persistence in the same blocking callable.
  // Unless retained after persistence, destruction removes our own entry only.
  class CreatedFile {
  public:
    CreatedFile(CreatedFile &&) noexcept;
    auto operator=(CreatedFile &&) noexcept -> CreatedFile &;
    ~CreatedFile();
    CreatedFile(const CreatedFile &) = delete;
    auto operator=(const CreatedFile &) -> CreatedFile & = delete;
    [[nodiscard]] auto path() const -> const std::string &;
    [[nodiscard]] auto read(std::size_t maximum_bytes) const
        -> std::vector<std::uint8_t>;
    void retain() noexcept;

  private:
    friend class PathManager;
    struct State;
    explicit CreatedFile(std::unique_ptr<State>);
    std::unique_ptr<State> state_;
  };
  PathManager(std::string installation, const std::string &host_base,
              const std::string &container_base);

  auto to_host_path(const std::string &relative_path) const -> std::string;
  auto to_container_path(const std::string &relative_path) const -> std::string;
  auto get_host_base() const -> const std::string &;
  auto get_container_base() const -> const std::string &;
  auto host_to_container_absolute(const std::string &host_absolute_path) const
      -> std::string;
  auto container_to_host_absolute(
      const std::string &container_absolute_path) const -> std::string;
  auto file_uri(const std::string &existing_host_file) const -> std::string;

  // Anchored, no-follow host operations. Preserve Bridge's existing umask
  // policy; never overwrite an existing media file or follow directory links.
  auto ensure_directory(const std::string &relative_path) const -> bool;
  auto write_new_file(const std::string &relative_path,
                      std::span<const std::byte> bytes) const -> std::string;
  auto write_owned_file(const std::string &relative_path,
                        std::span<const std::byte> bytes) const -> CreatedFile;
  // Worker-only bounded read of an actor-owned cache file. Pin its inode and
  // reject symlinks/non-regular files; never expose its path to the SDK.
  auto read_file_bounded(const std::string &host_path,
                         std::size_t maximum_bytes) const
      -> std::vector<std::uint8_t>;
  // Linux child processes read/write through this process's held descriptors.
  // Conversion scratch is private; successful output is published exclusively.
  // Verify the pinned source descriptor against the bytes inspected by the
  // caller before decoding. Does not replace the caller's immutable-file rule.
  auto convert_verified_file(
      const std::string &host_source, const std::string &host_destination,
      std::span<const std::uint8_t> expected_source,
      std::size_t maximum_output_bytes,
      const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
      const std::function<bool(const std::string &, const std::string &)>
          &convert) const -> bool;

  auto convert_owned_file(
      const std::string &host_source, const std::string &host_destination,
      std::span<const std::uint8_t> expected_source,
      std::size_t maximum_output_bytes,
      const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
      const std::function<bool(const std::string &, const std::string &)>
          &convert) const -> std::optional<CreatedFile>;

private:
  auto convert_file_impl(
      const std::string &host_source, const std::string &host_destination,
      std::optional<std::span<const std::uint8_t>> expected_source,
      std::size_t maximum_output_bytes,
      const std::function<bool(std::span<const std::uint8_t>)> &validate_output,
      const std::function<bool(const std::string &, const std::string &)>
          &convert) const -> std::optional<CreatedFile>;
  static auto read_descriptor_bounded(int file, std::size_t maximum_bytes)
      -> std::vector<std::uint8_t>;
  std::string installation_;
  path_mapping::Mapper mapper_;
  std::string host_base_;
  std::string container_base_;
};

} // namespace bridge
