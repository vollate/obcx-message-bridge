#include "path_manager.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>

TEST(PathManagerTest, OwnsFilesWithoutFollowingLinksOrOverwriting) {
  namespace fs = std::filesystem;
  std::string pattern =
      (fs::temp_directory_path() / "obcx-bridge-path-XXXXXX").string();
  const auto created = ::mkdtemp(pattern.data());
  ASSERT_NE(created, nullptr);
  const auto root = fs::canonical(created);
  const auto cleanup = [&root](int *) {
    std::error_code error;
    fs::remove_all(root, error);
  };
  int marker = 0;
  const std::unique_ptr<int, decltype(cleanup)> guard(&marker, cleanup);
  const bridge::PathManager manager{"qq-test", (root / "host").string(),
                                    "/peer files"};
  ASSERT_TRUE(manager.ensure_directory("temp"));
  const std::array<std::byte, 4> bytes{};
  const auto file = manager.write_new_file("temp/photo #.png", bytes);
  EXPECT_EQ(fs::file_size(file), bytes.size());
  EXPECT_EQ(manager.file_uri(file),
            "file:///peer%20files/temp/photo%20%23.png");
  EXPECT_THROW(manager.write_new_file("temp/photo #.png", bytes),
               std::runtime_error);
  EXPECT_EQ(fs::file_size(file), bytes.size());
  const auto converted = manager.to_host_path("converted/result.gif");
  const auto child_copy = [](const std::string &input,
                             const std::string &output) {
    return std::system(std::format("cat '{}' > '{}'", input, output).c_str()) ==
           0;
  };
  EXPECT_TRUE(manager.convert_file(file, converted, child_copy));
  EXPECT_EQ(fs::file_size(converted), bytes.size());
  EXPECT_THROW(manager.convert_file(file, converted, child_copy),
               std::runtime_error);
  const auto rejected = manager.to_host_path("converted/rejected.gif");
  EXPECT_FALSE(manager.convert_file(
      file, rejected, [&](const std::string &, const std::string &output) {
        fs::create_symlink(file, output);
        return true;
      }));
  EXPECT_FALSE(fs::exists(rejected));
  EXPECT_FALSE(manager.convert_file(
      file, rejected, [&](const std::string &input, const std::string &output) {
        (void)child_copy(input, output);
        return false;
      }));
  EXPECT_FALSE(fs::exists(rejected));
  std::size_t remaining = 0;
  for (const auto &entry : fs::directory_iterator(root / "host/converted")) {
    EXPECT_EQ(entry.path().filename(), "result.gif");
    ++remaining;
  }
  EXPECT_EQ(remaining, 1);
  fs::create_directory(root / "outside");
  fs::create_directory_symlink(root / "outside", root / "host/escape");
  EXPECT_FALSE(manager.ensure_directory("escape/nested"));
  EXPECT_THROW(manager.write_new_file("escape/file", bytes),
               std::runtime_error);
  EXPECT_TRUE(fs::is_empty(root / "outside"));
  EXPECT_THROW(
      manager.host_to_container_absolute((root / "host-other/file").string()),
      std::runtime_error);
}

TEST(PathManagerTest, NormalizesContainerPathsWithoutFilesystemAccess) {
  const bridge::PathManager manager{"qq-main", "/tmp/bridge-host/./files/",
                                    "/root/llonebot/./cache/../bridge_files/"};

  EXPECT_EQ(manager.get_host_base(), "/tmp/bridge-host/files");
  EXPECT_EQ(manager.get_container_base(), "/root/llonebot/bridge_files");
  EXPECT_EQ(manager.to_container_path("temp/../images/photo.jpg"),
            "/root/llonebot/bridge_files/images/photo.jpg");
  EXPECT_EQ(manager.container_to_host_absolute(
                "/root/llonebot/bridge_files/images/../photo.jpg"),
            "/tmp/bridge-host/files/photo.jpg");
}
