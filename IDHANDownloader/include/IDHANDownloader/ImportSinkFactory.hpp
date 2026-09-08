#pragma once

#include "ImportSink.hpp"

namespace idhan::downloader
{

class ImportSinkFactory
{
  public:

	virtual ~ImportSinkFactory() = default;

	virtual std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& request ) = 0;
};

} // namespace idhan::downloader
