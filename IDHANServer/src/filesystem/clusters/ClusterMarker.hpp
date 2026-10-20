#pragma once

#include <boost/uuid/uuid.hpp>
#include <json/value.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace idhan::filesystem
{
inline constexpr std::string_view clusterMarkerFilename { ".idhan-cluster.json" };

struct ClusterMarker
{
	boost::uuids::uuid uuid;
	Json::Value document;
};

[[nodiscard]] std::expected< std::optional< ClusterMarker >, std::string > readClusterMarker( int root_fd );
// Installs a complete marker atomically without replacing any existing entry.
// A racing creator's valid marker is returned instead of being overwritten.
[[nodiscard]] std::expected< ClusterMarker, std::string > initializeClusterMarker(
	int root_fd,
	const boost::uuids::uuid& uuid );
} // namespace idhan::filesystem
