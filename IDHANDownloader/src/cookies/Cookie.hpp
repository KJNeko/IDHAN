#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace idhan::downloader
{

struct Cookie
{
	std::string name {};
	std::string value {};
	std::string domain {};
	std::string path { "/" };
	bool secure {};
	bool host_only {};
	std::optional< std::chrono::system_clock::time_point > expires { std::nullopt };

	[[nodiscard]] bool sameIdentity( const Cookie& other ) const
	{
		return name == other.name && domain == other.domain && path == other.path;
	}
};

} // namespace idhan::downloader
