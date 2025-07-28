//
// Created by kj16609 on 7/28/25.
//
#pragma once
#include <drogon/HttpController.h>

namespace idhan
{

class Ui : public drogon::HttpController< Ui >
{
	drogon::Task< drogon::HttpResponsePtr > index( drogon::HttpRequestPtr );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( Ui::index, "/" );

	METHOD_LIST_END
};

} // namespace idhan