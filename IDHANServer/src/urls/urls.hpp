#pragma once

#include <drogon/HttpResponse.h>

#include <expected>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::helpers
{
constexpr UrlID INVALID_URL_ID { 0 };

enum UrlType
{
	File,
	Post,
	Gallery,
	Unknown
};

// Extracts the bare hostname from a URL string (strips protocol, path, port).
[[nodiscard]] std::string extractDomain( const std::string& url );

[[nodiscard]] drogon::Task< std::optional< UrlID > > findUrl( const std::string& url, DbClientPtr db );

[[nodiscard]] drogon::Task< std::optional< UrlDomainID > > findUrlDomain( const std::string& domain, DbClientPtr db );

ExpectedTask< UrlID > findOrCreateUrl( std::string url, DbClientPtr db );

ExpectedTask< UrlDomainID > findOrCreateUrlDomain( std::string url, DbClientPtr db );
} // namespace idhan::helpers
