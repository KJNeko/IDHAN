#pragma once

#include <string>

namespace idhan::downloader
{

inline const std::string idhan_user_agent {
	"Mozilla/5.0 (compatible; IDHAN Client; +https://github.com/KJNeko/IDHAN; Discord: KJ16609)"
};

[[nodiscard]] bool isSensitiveHeader( std::string name );

} // namespace idhan::downloader
