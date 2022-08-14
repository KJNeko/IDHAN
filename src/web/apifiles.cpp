//
// Created by kj16609 on 8/14/22.
//

#include "apifiles.hpp"

namespace api
{
namespace v1
{
	void Files::getInfo( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id ) {}

	void Files::addFile( const HttpRequestPtr& req, CallBack_Type&& callback ) {}

	void Files::addTag( const drogon::HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id ) {}

	void Files::removeTag( const drogon::HttpRequestPtr& req, api::v1::Files::CallBack_Type&& callback, uint64_t id ) {}

	void Files::deleteFile( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id ) {}

	void Files::undeleteFile( const HttpRequestPtr& req, CallBack_Type&& callback, uint64_t id ) {}

}  // namespace v1
}  // namespace api