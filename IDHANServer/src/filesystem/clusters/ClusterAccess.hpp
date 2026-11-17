#pragma once

#include <boost/uuid/uuid.hpp>
#include <folly/File.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "idhan/runtime/Task.hpp"

namespace idhan::filesystem
{

struct ClusterMarker;

class ClusterAccess
{
	folly::File m_root;
	boost::uuids::uuid m_uuid;
	bool m_readonly;
	ClusterAccess( folly::File root, boost::uuids::uuid uuid, bool readonly );
	[[nodiscard]] std::expected< void, std::string > verifyMarker( const std::optional< ClusterMarker >& marker ) const;

  public:

	ClusterAccess( ClusterAccess&& ) noexcept = default;
	ClusterAccess& operator=( ClusterAccess&& ) noexcept = default;
	ClusterAccess( const ClusterAccess& ) = delete;
	ClusterAccess& operator=( const ClusterAccess& ) = delete;

	[[nodiscard]] static std::expected< ClusterAccess, std::string > openSync(
		const std::filesystem::path& root,
		const boost::uuids::uuid& expected,
		bool readonly );

	[[nodiscard]] std::expected< void, std::string > verifySync() const;

	[[nodiscard]] static runtime::Task< std::expected< ClusterAccess, std::string > > open(
		std::filesystem::path root,
		boost::uuids::uuid expected,
		bool readonly );

	[[nodiscard]] runtime::Task< std::expected< void, std::string > > verify() const;
	[[nodiscard]] runtime::Task< int > close();
	[[nodiscard]] std::filesystem::path root() const;

	[[nodiscard]] bool readonly() const noexcept { return m_readonly; }
};

} // namespace idhan::filesystem
