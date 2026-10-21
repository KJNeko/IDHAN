#pragma once

#include <boost/uuid/uuid.hpp>
#include <folly/File.h>

#include <expected>
#include <filesystem>
#include <string>

namespace idhan::filesystem
{

class ClusterAccess
{
	folly::File m_root;
	boost::uuids::uuid m_uuid;
	bool m_readonly;
	ClusterAccess( folly::File root, boost::uuids::uuid uuid, bool readonly );

  public:

	ClusterAccess( ClusterAccess&& ) noexcept = default;
	ClusterAccess& operator=( ClusterAccess&& ) noexcept = default;
	ClusterAccess( const ClusterAccess& ) = delete;
	ClusterAccess& operator=( const ClusterAccess& ) = delete;
	[[nodiscard]] static std::expected< ClusterAccess, std::string > open(
		const std::filesystem::path& root,
		const boost::uuids::uuid& expected,
		bool readonly );
	[[nodiscard]] std::expected< void, std::string > verify() const;
	// Pins the verified directory rather than resolving its configured mount path again.
	// The returned path is usable only while this access object remains alive.
	[[nodiscard]] std::filesystem::path root() const;

	[[nodiscard]] bool readonly() const noexcept { return m_readonly; }
};

} // namespace idhan::filesystem
