//
// Created by kj16609 on 5/7/25.
//
#include "TagSearch.hpp"

#include <expected>

#include "threading/ExpectedTask.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan
{

ExpectedTask< TagID > TagSearch::idealize( const TagID id )
{
	const auto id_check { co_await m_db->execSqlCoro( "SELECT tag_id FROM tags WHERE tag_id = $1", id ) };
	const auto result { co_await m_db->execSqlCoro(
		"SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2", id, m_domain ) };

	if ( id_check.empty() ) co_return std::unexpected( createBadRequest( "Invalid tag ID: {}", id ) );

	if ( result.empty() ) co_return id;

	co_return result[ 0 ][ "alias_id" ].as< TagID >();
}

TagSearch::TagSearch( const TagDomainID tag_domain_id, DbClientPtr db ) : m_db( db ), m_domain( tag_domain_id )
{}

ExpectedTask< void > TagSearch::addID( const TagID id )
{
	const auto idealized_id { co_await idealize( id ) };

	if ( !idealized_id ) co_return std::unexpected( idealized_id.error() );

	m_ids.emplace_back( *idealized_id );

	const auto children_result { co_await addChildren( *idealized_id ) };
	if ( !children_result ) co_return std::unexpected( children_result.error() );

	// const auto siblings_result { co_await removeSiblings( *idealized_id ) };
	// if ( !siblings_result ) co_return std::unexpected( siblings_result.error() );

	co_return {};
}

ExpectedTask< void > TagSearch::addChildren( TagID tag_id )
{
	std::vector< TagID > searched {};
	std::queue< TagID > queue {};

	queue.push( tag_id );

	do
	{
		const auto children { co_await m_db->execSqlCoro(
			"SELECT DISTINCT child_id FROM aliased_parents WHERE parent_id = $1 AND tag_domain_id = $2",
			queue.front(),
			m_domain ) };

		for ( auto& row : children )
		{
			const auto child_id { row[ "child_id" ].as< TagID >() };

			if ( std::ranges::find( searched, child_id ) != searched.end() )
			{
				// TODO: Log cyclic better
				co_return std::unexpected(
					createBadRequest( "Cyclic tag parents in tag search: {} <-> {}", tag_id, child_id ) );
			}

			queue.push( child_id );
			searched.emplace_back( child_id );
			m_ids.emplace_back( child_id );
		}

		queue.pop();
	}
	while ( !queue.empty() );

	std::ranges::sort( m_ids );
	std::ranges::unique( m_ids );

	co_return {};
}

ExpectedTask< std::vector< TagID > > TagSearch::findSiblings( const TagID id )
{
	std::vector< TagID > found {};
	std::queue< TagID > queue {};

	queue.push( id );

	do
	{
		const auto siblings { co_await m_db->execSqlCoro(
			"SELECT DISTINCT younger_id FROM aliased_siblings WHERE older_id = $1 AND tag_domain_id = $2",
			queue.front(),
			m_domain ) };

		for ( auto& row : siblings )
		{
			const auto sibling_id { row[ "younger_id" ].as< TagID >() };

			if ( std::ranges::find( found, sibling_id ) != found.end() )
			{
				co_return std::unexpected(
					createBadRequest( "Cyclic tag siblings in tag search: {} <-> {}", id, sibling_id ) );
			}

			found.emplace_back( sibling_id );
			queue.push( sibling_id );
		}

		queue.pop();
	}
	while ( !queue.empty() );

	std::ranges::unique( found );

	co_return found;
}

ExpectedTask< void > TagSearch::removeSiblings()
{
	std::vector< TagID > to_remove {};

	for ( const auto& id : m_ids )
	{
		const auto siblings { co_await findSiblings( id ) };

		if ( !siblings ) co_return std::unexpected( siblings.error() );

		to_remove.insert( to_remove.end(), siblings->begin(), siblings->end() );
	}

	std::ranges::unique( to_remove );

	for ( auto& id : to_remove )
	{
		std::ranges::remove_if( m_ids, [ &id ]( const auto& i ) noexcept -> bool { return i == id; } );
	}

	co_return {};
}

ExpectedTask< std::vector< RecordID > > TagSearch::search()
{
	co_await removeSiblings();

	std::vector< RecordID > found {};

	for ( auto& id : m_ids )
	{
		const auto result { co_await m_db->execSqlCoro(
			"SELECT DISTINCT record_id FROM tag_mappings WHERE tag_id = $1 AND tag_domain_id = $2", id, m_domain ) };

		std::vector< RecordID > result_records;
		for ( const auto& row : result )
		{
			result_records.push_back( row[ "record_id" ].as< RecordID >() );
		}

		if ( found.empty() )
		{
			found = result_records;
		}
		else
		{
			std::vector< RecordID > intersection {};
			intersection.reserve( found.size() );
			std::ranges::set_intersection( found, result_records, std::back_inserter( intersection ) );
			found = intersection;
		}
	}

	co_return found;
}

} // namespace idhan
