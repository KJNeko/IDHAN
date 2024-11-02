//
// Created by kj16609 on 11/2/24.
//

#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/checkContentType.hpp"
#include "logging/log.hpp"

namespace idhan::hyapi
{

	struct HeaderInfo
	{};

	/**
	 * Arguments:
	 * `tags` a list of the tags to search (converted to id by text matching)
	 * `tag_ids` a list of tag ids to search (Recomended over tags)
	 * `tag_domains` a list of domains to use (optional, defaults to all)
	 * `file_sort_type` integer for the search method, defaults to IMPORT_TIME
	 * `file_sort_asc` boolean, default true, determines the search order. (descending by default)
	 * `return_file_ids` boolean, default true
	 * `return_hashes` boolean, default false
	 *
	 * @param request
	 * @param callback
	 * @return
	 */
	void searchFiles( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		// We expect json
		checkContentType( request, callback, { drogon::ContentType::CT_APPLICATION_JSON } );

		log::debug( "GET: /hyapi/get_files/search_files" );

		//TODO: Search

		Json::Value response {};

		response[ "file_ids" ] = Json::arrayValue;
	}

} // namespace idhan::hyapi
