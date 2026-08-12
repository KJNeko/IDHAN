#pragma once

#include "drogon/HttpController.h"
#include "drogon/HttpMiddleware.h"

/**
 * @page IDHANAuth IDHAN Authorization
 * @warning This document is purely for reference for later, The only thing true in this document is the concept of
 * `Access Key`
 *
 * IDHAN uses a key system to authorize access to the API. There are various ways to do so however.
 * First, Some terminology.
 * - `Access Key`: A key that is set and capable of being used until it's deleted. Permanant life.
 *
 * IDHAN does not model per-key permissions: any accepted key has full access.
 */

/**
 *
 * @page IDHANAuth Auth Tables
 * @warning This document is purely for reference for later.
 *
 * There are currently 3 tables that deal with authorization for the API:
 * `access_keys`, `hydrus_keys`, and `access_domains`
 *
 * @subpage access_keys "Access Keys Table"
 * This table contains the access key and it's internal id.
 *
 * Generation query:\n
 * @code
	CREATE TABLE access_keys
	(
		access_key_id SERIAL PRIMARY KEY,
		access_key    BYTEA UNIQUE NOT NULL
	);
 * @endcode
 *
 *
 */

namespace idhan::api
{

class APIAuth : public drogon::HttpCoroFilter< APIAuth >
{
  public:

	APIAuth() = default;

	drogon::Task< drogon::HttpResponsePtr > doFilter( const drogon::HttpRequestPtr& req ) override;
};

constexpr auto IDHANAPIAuthName { "idhan::api::APIAuth" };

//! Endpoints for API-key authentication: verifying an access key and generating API keys.
class AuthEndpoint final : public drogon::HttpController< AuthEndpoint >
{
	drogon::Task< drogon::HttpResponsePtr > verifyAccessKey( drogon::HttpRequestPtr req );
	drogon::Task< drogon::HttpResponsePtr > generateApiKey( drogon::HttpRequestPtr req );

	//! Confirms the presented API key is valid; used by the client on boot. Reaching the handler at
	//! all means the auth filter accepted the key.
	drogon::Task< drogon::HttpResponsePtr > verifyKey( drogon::HttpRequestPtr req );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( AuthEndpoint::verifyAccessKey, "/hyapi/verify_access_key", drogon::Get );
	ADD_METHOD_TO( AuthEndpoint::generateApiKey, "/generate_api_key" );
	ADD_METHOD_TO( AuthEndpoint::verifyKey, "/auth/verify", drogon::Get, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
