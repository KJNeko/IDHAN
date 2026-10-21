#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
struct AuthKeySnapshot
{
	std::uint64_t generation { 0 };
	std::vector< std::string > keys;
};

class AuthKeyCache
{
	std::atomic< std::shared_ptr< const AuthKeySnapshot > > m_snapshot;

  public:

	AuthKeyCache();
	~AuthKeyCache();
	AuthKeyCache( const AuthKeyCache& ) = delete;
	AuthKeyCache& operator=( const AuthKeyCache& ) = delete;

	[[nodiscard]] std::shared_ptr< const AuthKeySnapshot > snapshot() const noexcept;
	[[nodiscard]] bool contains( std::string_view key ) const noexcept;
	[[nodiscard]] runtime::Task<> load();
	[[nodiscard]] runtime::Task< std::string > generateBootstrapKey();
	[[nodiscard]] runtime::Task<> replace( std::span< const std::string > keys );
};
} // namespace idhan::server
