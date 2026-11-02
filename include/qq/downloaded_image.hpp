#pragma once

#include <string>

namespace bridge::qq {

// Owned input to Bridge's image inspection and normalization. This value does
// not authorize reacquiring a message attachment from original_url.
struct DownloadedImage {
  std::string type;
  std::string original_url;
  std::string filename;
  std::string mime_type;
  std::string data;
};

} // namespace bridge::qq
