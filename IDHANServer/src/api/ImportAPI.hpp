#pragma once

#include <drogon/HttpController.h>

#include "APIAuth.hpp"

namespace idhan::api
{

//! Endpoint for importing files into IDHAN.
class ImportAPI final : public drogon::HttpController< ImportAPI >
{
	drogon::Task< drogon::HttpResponsePtr > importFile( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( ImportAPI::importFile, "/file/import", drogon::Post, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
