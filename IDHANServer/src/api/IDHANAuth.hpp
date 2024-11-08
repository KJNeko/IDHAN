//
// Created by kj16609 on 11/8/24.
//
#pragma once
#include "drogon/HttpMiddleware.h"

/**
 * @page IDHANAuth IDHAN Authorization
 * IDHAN uses a key system to authorize access to the API. There are various ways to do so however.
 * First, Some terminology.
 * - `Access Key`: A key that is set and capable of being used until it's deleted. Permanant life.
 * - `Session Key`: A key that is temporarily issued using an access key. Temporary life.
 * - `Permissions`: The integer that represents various permissions (masked using `idhan::KeyPermissions`)
 */

/**
 *
 * @page IDHANAuth Auth Tables
 * There are currently 4 tables that deal with authorization for the API:
 * `access_keys`, `session_keys`, `hydrus_keys`, and `access_domains`
 *
 * @subpage access_keys "Access Keys Table"
 * This table contains the access key, it's internal id, and the @ref idhan::KeyPermissions "permissions" applied to it.
 *
 * Generation query:\n
 * @code
	CREATE TABLE access_keys
	(
		access_key_id SERIAL PRIMARY KEY,
		access_key    BYTEA UNIQUE NOT NULL,
		permissions   INT          NOT NULL DEFAULT 0
	);
 * @endcode
 *
 *
 */

namespace idhan::api
{

class IDHANAuth : public drogon::HttpMiddleware< IDHANAuth >
{
  public:

	IDHANAuth() = default;

	void invoke(
		const drogon::HttpRequestPtr& req,
		drogon::MiddlewareNextCallback&& nextCb,
		drogon::MiddlewareCallback&& mcb ) override;
};

} // namespace idhan::api
