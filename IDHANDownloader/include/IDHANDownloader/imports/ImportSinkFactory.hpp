#pragma once

#include <optional>

#include "IDHANDownloader/imports/ImportSink.hpp"

namespace idhan::downloader
{

class ImportSinkFactory
{
  public:

	virtual ~ImportSinkFactory() = default;

	//! Looks up a locally present file without importing it or applying metadata.
	virtual folly::coro::Task< std::expected< std::optional< RecordID >, std::string > > findStoredRecord(
		[[maybe_unused]] ImportRequest request )
	{
		co_return std::nullopt;
	}

	virtual std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) = 0;
};

} // namespace idhan::downloader
