//
// Created by kj16609 on 7/28/25.
//
#pragma once
#include <drogon/HttpController.h>

namespace idhan
{

//! Serves the bundled web UI pages (index, status, webui).
class Ui : public drogon::HttpController< Ui >
{
	drogon::Task< drogon::HttpResponsePtr > index( drogon::HttpRequestPtr );
	drogon::Task< drogon::HttpResponsePtr > indexWebUI( drogon::HttpRequestPtr );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( Ui::index, "/" );
	ADD_METHOD_TO( Ui::index, "/status" );
	ADD_METHOD_TO( Ui::indexWebUI, "/webui" );

	METHOD_LIST_END
};

} // namespace idhan
