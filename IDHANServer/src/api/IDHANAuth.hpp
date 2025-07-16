//
// Created by kj16609 on 11/8/24.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpMiddleware.h"
#pragma GCC diagnostic pop

/**
 * @page IDHANAuth IDHAN Authorization
 * @warning This document is purely for reference for later, The only thing true in this document is the concept of `Access Key`
 *
 * IDHAN uses a key system to authorize access to the API. There are various ways to do so however.
 * First, Some terminology.
 * - `Access Key`: A key that is set and capable of being used until it's deleted. Permanant life.
 * - `Session Key`: A key that is temporarily issued using an access key. Temporary life.
 * - `Permissions`: The integer that represents various permissions (masked using `idhan::KeyPermissions`)
 */

/**
 *
 * @page IDHANAuth Auth Tables
 * @warning This document is purely for reference for later.
 *
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
