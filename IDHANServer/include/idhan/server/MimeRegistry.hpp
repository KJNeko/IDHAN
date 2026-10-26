#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
struct MimeInfo
{
	MimeID id { 0 };
	std::string mime;
	std::string extension;
	SimpleMimeType category { SimpleMimeType::NONE };
};

class MimeRegistry
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	MimeRegistry();
	~MimeRegistry();
	MimeRegistry( const MimeRegistry& ) = delete;
	MimeRegistry& operator=( const MimeRegistry& ) = delete;

	[[nodiscard]] runtime::Task<> synchronize();
	[[nodiscard]] runtime::Task< MimeInfo > detect( std::span< const std::byte > prefix, std::string_view filename )
		const;
	[[nodiscard]] std::vector< MimeInfo > list() const;
};
} // namespace idhan::server
