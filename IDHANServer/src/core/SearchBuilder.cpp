//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

namespace idhan
{

std::string SearchBuilder::construct( const bool return_ids, const bool return_hashes ) const
{
	/*
	SELECT [record_id],[1:sha256] FROM tag_mappings [1:NATURAL JOIN records ON tag_mappings.record_id = records.record_id]
	*/

	constexpr std::string_view select_record_id { "SELECT record_id FROM tag_mappings" };
	constexpr std::string_view select_sha256 {
		"SELECT sha256 FROM tag_mappings NATURAL JOIN records ON tag_mappings.record_id = records.record_id"
	};
	constexpr std::string_view select_both {
		"SELECT record_id, sha256 FROM tag_mappings NATURAL JOIN records ON tag_mappings.record_id = records.record_id"
	};

	std::string query {};

	if ( return_ids && return_hashes )
		query += select_both;
	else if ( return_ids )
		query += select_record_id;
	else if ( return_hashes )
		query += select_sha256;
	{
		return "";
	}

	return "";
}

void SearchBuilder::filterTagDomain( const TagDomainID value )
{
	//any searches with `tags` should be filtered.
}

void SearchBuilder::addFileDomain( const FileDomainID value )
{
	params.append( value );
	placeholders.next();
}

} // namespace idhan
