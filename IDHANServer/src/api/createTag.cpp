//
// Created by kj16609 on 11/9/24.
//

#include <oneapi/tbb/detail/_range_common.h>

#include <ranges>

#include "IDHANApi.hpp"
#include "core/tags.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

void IDHANApi::tagInfo( const drogon::HttpRequestPtr& request, ResponseFunction&& callback, TagID tag_id )
{
	log::info( "/tag/{}/info", tag_id );
}

void IDHANApi::createTagFromText(
	const drogon::HttpRequestPtr& request, ResponseFunction&& callback, const std::string_view tag_text )
{
	const auto [ n_str, s_str ] = tags::split( tag_text );
	createTagFromComponents( request, std::move( callback ), n_str, s_str );
}

void IDHANApi::createTagFromComponents(
	const drogon::HttpRequestPtr& request,
	ResponseFunction&& callback,
	std::string_view namespace_text,
	std::string_view subtag_text )
{
	// createTagFromBody();
}

} // namespace idhan::api