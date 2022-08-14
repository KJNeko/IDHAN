//
// Created by kj16609 on 8/13/22.
//

#include "apibase.hpp"

namespace api
{

void api_info::api( const drogon::HttpRequestPtr& req, api::api_info::CallBack_Type&& callback ) {}

void api_info::get_v1( const drogon::HttpRequestPtr& req, api::api_info::CallBack_Type&& callback ) {}

void api_info::get_versions( const drogon::HttpRequestPtr& req, api::api_info::CallBack_Type&& callback ) {}

}  // namespace api