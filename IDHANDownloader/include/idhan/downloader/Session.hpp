#pragma once
#include "idhan/downloader/URL.hpp"

class DownloadSession
{
  public:

	idhan::Task< DownloadReturn > submit( URL url );
};

class URLRequest
{
	URL m_url;
	//! Parser used for m_url
	std::shared_ptr< Parser > m_parser;
};
