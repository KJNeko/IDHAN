#pragma once
#include <drogon/HttpController.h>

namespace idhan
{

//! Serves the WebUI entry point. Deep links are handled by the SPA fallback in ServerContext.
class Ui : public drogon::HttpController< Ui >
{
	drogon::Task< drogon::HttpResponsePtr > index( drogon::HttpRequestPtr );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( Ui::index, "/" );
	ADD_METHOD_TO( Ui::index, "/status" );

	METHOD_LIST_END
};

} // namespace idhan
