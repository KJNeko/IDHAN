//
// Created by kj16609 on 11/7/24.
//

#include "SearchBuilder.hpp"

namespace idhan
{

void SearchBuilder::filterFileDomain( const FileDomainID value )
{
	// records should be filtered with 'WHERE file_domain = $1'
	file_records_filter += "";
}

std::string SearchBuilder::construct()
{
	return "";
}

void SearchBuilder::filterTagDomain( const TagDomainID value )
{
	//any searches with `tags` should be filtered.
}

} // namespace idhan
