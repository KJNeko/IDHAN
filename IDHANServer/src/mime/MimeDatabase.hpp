#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>

#include "MimeIdentifier.hpp"
#include "MimeInfo.hpp"
#include "ModuleBase.hpp"
#include "filesystem/io/IOUring.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::mime
{
class Cursor;

/** @page MimeParser Mime Parser
 *
 * `.idhanmime` files define `mime`, `extensions`, `priority`, `require_extension`, and a matcher
 * `data` array. Supported matchers are `search` (hex pattern, optional offset/limit) and `include`
 * (runs another parser's matchers). Child matchers continue from the parent match position; circular
 * includes are rejected.
 */

constexpr auto INVALID_MIME_NAME { "unknown/unknown" };

//! MIME parser database; scans hold a copy-on-write snapshot across co_awaits.
class MimeDatabase
{
	MimeDatabase();

	friend std::shared_ptr< MimeDatabase > getMimeDatabase();

	std::shared_ptr< const std::vector< MimeIdentifier > > m_identifiers {
		std::make_shared< std::vector< MimeIdentifier > >()
	};
	mutable std::mutex m_identifiers_mutex {};

	[[nodiscard]] std::shared_ptr< const std::vector< MimeIdentifier > > identifiers() const;

	drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > scan( Cursor cursor );

  public:

	[[nodiscard]] Json::Value dump() const;

	[[nodiscard]] ExpectedTask< std::string > scan( std::string_view data, std::string file_name );

	[[nodiscard]] ExpectedTask< std::string > scan( data_view data, std::string file_name );

	[[nodiscard]] ExpectedTask< std::string > scan( std::shared_ptr< FileIOUring > file_io );

	[[nodiscard]] ExpectedTask< std::string > scanFile( const std::filesystem::path& path );

	[[nodiscard]] drogon::Task< std::expected< void, drogon::HttpResponsePtr > > reloadMimeParsers();
};

[[nodiscard]] std::shared_ptr< MimeDatabase > getMimeDatabase();

[[nodiscard]] drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > > getMimeIDFromStr(
	std::string str,
	DbClientPtr db );

} // namespace idhan::mime
