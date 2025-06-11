//
// Created by kj16609 on 2/20/25.
//

#include "IDHANClient.hpp"
#include "IDHANTypes.hpp"
#include "splitTag.hpp"

namespace idhan
{

QFuture< TagID > IDHANClient::createTag( const std::string&& namespace_text, const std::string&& subtag_text )
{
	return createTags( { std::make_pair( namespace_text, subtag_text ) } )
	    .then( []( const std::vector< TagID >& tag_ids ) -> TagID { return tag_ids.at( 0 ); } );
}

QFuture< TagID > IDHANClient::createTag( const std::string& tag_text )
{
	const auto [ namespace_i, subtag_i ] { splitTag( tag_text ) };

	return createTag( std::move( namespace_i ), std::move( subtag_i ) );
}

} // namespace idhan
