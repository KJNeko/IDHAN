#pragma once

#include "ImportSink.hpp"

#include <optional>

namespace idhan::downloader
{

class ImportSinkFactory
{
  public:

	virtual ~ImportSinkFactory() = default;

	// Runs before opening a sink. A hit must also apply the request's metadata.
	virtual std::expected< std::optional< ImportResult >, std::string > reuse( const ImportRequest& )
	{
		return std::nullopt;
	}

	virtual std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) = 0;
};

} // namespace idhan::downloader
