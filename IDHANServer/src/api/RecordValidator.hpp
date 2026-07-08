//
// Created by kj16609 on 7/8/26.
//
#pragma once

#include "drogon/HttpFilter.h"

namespace idhan::api
{

//! Rejects requests to /records/{record_id}/... routes whose record does not exist.
//! Unknown records would otherwise surface as FK-violation 500s on mutation endpoints
//! and empty 200s on listing endpoints. Listed after IDHANAPIAuthName in ADD_METHOD_TO
//! so auth always runs first.
class RecordValidator : public drogon::HttpCoroFilter< RecordValidator >
{
  public:

	RecordValidator() = default;

	drogon::Task< drogon::HttpResponsePtr > doFilter( const drogon::HttpRequestPtr& req ) override;
};

constexpr auto IDHANRecordValidatorName { "idhan::api::RecordValidator" };

} // namespace idhan::api
