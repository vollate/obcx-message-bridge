#pragma once

#include <obcx/path_mapping.hpp>

#include <cstddef>
#include <functional>
#include <span>
#include <string>

namespace bridge {

// Bridge-owned filesystem facade. Mapping is delegated to the independent
// library; creation and eventual cleanup remain Bridge responsibilities.
class PathManager {
public:
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
  // Linux child processes read/write through this process's held descriptors.
  // Conversion scratch is private; successful output is published exclusively.
  auto convert_file(
      const std::string &host_source, const std::string &host_destination,
      const std::function<bool(const std::string &, const std::string &)>
          &convert) const -> bool;

private:
  std::string installation_;
  obcx::path_mapping::Mapper mapper_;
  std::string host_base_;
  std::string container_base_;
};

} // namespace bridge
