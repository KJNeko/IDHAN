//
// Created by kj16609 on 3/20/25.
//
#pragma once

#include <drogon/HttpController.h>

namespace idhan::api
{

class IDHANMaintnanceAPI : public drogon::HttpController< IDHANMaintnanceAPI >
{
  public:

	METHOD_LIST_BEGIN

	METHOD_LIST_END
};

} // namespace idhan::api