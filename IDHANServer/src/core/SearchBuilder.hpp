//
// Created by kj16609 on 11/7/24.
//
#pragma once
#include <pqxx/pqxx>

#include <string_view>

#include "IDHANTypes.hpp"
#include "drogon/HttpRequest.h"

namespace idhan
{

struct SearchBuilder
{
	constexpr static std::string_view inital_query { "SELECT tag_id FROM file_records" };
	std::string file_records_filter {};

	pqxx::params params;
	pqxx::placeholders<> placeholders;

  public:

	SearchBuilder() = default;

	std::string construct( const bool return_ids = true, const bool return_hashes = false ) const;

	void filterTagDomain( const TagDomainID value );

	void addFileDomain( const FileDomainID value );
};

} // namespace idhan