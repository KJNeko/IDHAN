#pragma once

#include <IDHANDownloader/parsers/ParserHostPersistence.hpp>

#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::downloader
{

[[nodiscard]] std::expected< std::string, std::string > normalizeParserHost( std::string_view host );

class DatabaseParserHosts final : public ParserHostPersistence
{
	db::DBContext& m_db;

  public:

	explicit DatabaseParserHosts( db::DBContext& db ) : m_db( db ) {}

	folly::coro::Task< std::expected< std::vector< ParserHost >, std::string > > loadAll() override;
	folly::coro::Task< std::expected< ParserHost, std::string > > discover( std::string host, std::string parser )
		override;
	[[nodiscard]] static runtime::Task< std::vector< ParserHost > > list( db::DBContext& db );
	[[nodiscard]] static runtime::Task< ParserHost > upsert( db::DBContext& db, ParserHost entry );
	[[nodiscard]] static runtime::Task< bool > erase( db::DBContext& db, std::string host );
};

} // namespace idhan::downloader
