//
// Created by kj16609 on 6/11/25.
//
#pragma once

#include <drogon/HttpController.h>

namespace idhan::api
{

class ImportAPI final : public drogon::HttpController< ImportAPI >
{
	drogon::Task< drogon::HttpResponsePtr > importFile( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( ImportAPI::importFile, "/file/import" );

	METHOD_LIST_END
};

} // namespace idhan::api
