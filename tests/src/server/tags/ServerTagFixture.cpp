//
// Created by kj16609 on 8/18/25.
//

#include "ServerTagFixture.hpp"

#include "MappingFixture.hpp"
#include "ServerDBFixture.hpp"
#include "logging/format_ns.hpp"
#include "migrations.hpp"
#include "splitTag.hpp"

void ServerTagFixture::SetUp()
{
	ServerDBFixture::SetUp();
	default_domain_id = createDomain( "default" );
}

idhan::TagDomainID ServerTagFixture::createDomain( const std::string_view name ) const
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "INSERT INTO tag_domains (domain_name) VALUES ($1) ON CONFLICT DO NOTHING RETURNING tag_domain_id", pqxx::params { name } ) };

	if ( result.empty() )
	{
		const auto search_result { tx.exec_params( "SELECT tag_domain_id FROM tag_domains WHERE domain_name = $1", pqxx::params { name } ) };
		if ( search_result.empty() ) throw std::runtime_error( "Failed to create domain" );
		return search_result[ 0 ][ 0 ].as< idhan::TagDomainID >();
	}

	return result[ 0 ][ 0 ].as< idhan::TagDomainID >();
}

idhan::TagID ServerTagFixture::createTag( const std::string_view text ) const
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	const auto [ namespace_text, subtag_text ] = idhan::splitTag( text );

	std::vector< std::string > namespace_texts {};
	namespace_texts.emplace_back( namespace_text );

	std::vector< std::string > subtag_texts {};
	subtag_texts.emplace_back( subtag_text );

	const auto result { tx.exec_params( "SELECT tag_id FROM createBatchTags($1, $2) ", pqxx::params { namespace_texts, subtag_texts } ) };

	tx.commit();

	if ( result.empty() ) throw std::runtime_error( "No tags returned" );

	return result[ 0 ][ 0 ].as< idhan::TagID >();
}

void ServerTagFixture::createAlias( const TagID aliased_id, const TagID alias_id )
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	tx.exec_params( "INSERT INTO tag_aliases (aliased_id, alias_id, tag_domain_id) VALUES ($1, $2, $3)", pqxx::params { aliased_id, alias_id, default_domain_id } );

	tx.commit();
}

bool ServerTagFixture::aliasExists( const TagID aliased_id, const TagID alias_id )
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "SELECT EXISTS(SELECT 1 FROM tag_aliases WHERE aliased_id = $1 AND alias_id = $2 AND tag_domain_id = $3)", pqxx::params { aliased_id, alias_id, default_domain_id } ) };

	tx.commit();

	return result[ 0 ][ 0 ].as< bool >();
}

void ServerTagFixture::createParent( TagID parent_id, TagID child_id )
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	tx.exec_params( "INSERT INTO tag_parents (parent_id, child_id, tag_domain_id) VALUES ($1, $2, $3)", pqxx::params { parent_id, child_id, default_domain_id } );

	tx.commit();

	return;
}

bool ServerTagFixture::parentExists( TagID parent_id, TagID child_id )
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "SELECT EXISTS(SELECT 1 FROM tag_parents WHERE parent_id = $1 AND child_id = $2 AND tag_domain_id = $3)", pqxx::params { parent_id, child_id, default_domain_id } ) };

	return result[ 0 ][ 0 ].as< bool >();
}

testing::AssertionResult dumpParents( testing::AssertionResult result, pqxx::work& tx )
{
	const auto table_printout { tx.exec( "SELECT * FROM tag_parents" ) };

	result << "Parents (tag_parents):\n";
	result << "(parent_id, child_id, ideal_parent_id, ideal_child_id)\n";
	for ( const auto& row : table_printout )
	{
		const auto parent_id { row[ "parent_id" ].as< TagID >() };
		const auto child_id { row[ "child_id" ].as< TagID >() };
		const auto is_parent_ideal { !row[ "ideal_parent_id" ].is_null() };
		const auto is_child_ideal { !row[ "ideal_child_id" ].is_null() };

		if ( is_parent_ideal && is_child_ideal )
		{
			result << format_ns::format( "({}, {}, {}, {})\n", parent_id, child_id, row[ "ideal_parent_id" ].as< TagID >(), row[ "ideal_child_id" ].as< TagID >() );
		}
		else if ( is_parent_ideal )
		{
			result << format_ns::format( "({}, {}, {}, NULL)\n", parent_id, child_id, row[ "ideal_parent_id" ].as< TagID >() );
		}
		else if ( is_child_ideal )
		{
			result << format_ns::format( "({}, {}, NULL, {})\n", parent_id, child_id, row[ "ideal_child_id" ].as< TagID >() );
		}
		else
		{
			result << format_ns::format( "({}, {}, NULL, NULL)\n", parent_id, child_id );
		}
	}

	return result;
}

testing::AssertionResult dumpMappings( testing::AssertionResult result, pqxx::work& tx )
{
	const auto table_printout { tx.exec( "SELECT * FROM active_tag_mappings" ) };

	result << "Mappings (active_tag_mappings):\n";
	result << "(record_id, tag_id, tag_domain_id, ideal_tag_id)\n";
	for ( const auto& row : table_printout )
	{
		const auto record_id { row[ "record_id" ].as< RecordID >() };
		const auto tag_id { row[ "tag_id" ].as< TagID >() };
		const auto tag_domain_id { row[ "tag_domain_id" ].as< TagDomainID >() };

		const bool is_ideal { !row[ "ideal_tag_id" ].is_null() };

		if ( is_ideal )
			result << format_ns::format( "({}, {}, {}, {})\n", record_id, tag_id, tag_domain_id, row[ "ideal_tag_id" ].as< TagID >() );
		else
			result << format_ns::format( "({}, {}, {}, NULL)\n", record_id, tag_id, tag_domain_id );
	}

	if ( table_printout.size() == 0 )
		result << "\t\t\tNo rows found\n";

	return result;
}

testing::AssertionResult dumpParentMappings( testing::AssertionResult result, pqxx::work& tx )
{
	result = dumpMappings( result, tx );
	result = dumpParents( result, tx );

	const auto table_printout { tx.exec( "SELECT * FROM active_tag_mappings_parents" ) };

	result << "Parent mappings (active_tag_mappings_parents):\n";
	result << "(record_id, tag_id, origin_id, tag_domain_id, internal, internal_count)\n";

	for ( const auto& row : table_printout )
	{
		const auto record_id { row[ "record_id" ].as< RecordID >() };
		const auto tag_id { row[ "tag_id" ].as< TagID >() };
		const auto origin_id { row[ "origin_id" ].as< TagID >() };
		const auto tag_domain_id { row[ "tag_domain_id" ].as< TagDomainID >() };
		const bool internal { row[ "internal" ].as< bool >() };
		const auto internal_count { row[ "internal_count" ].as< std::uint32_t >() };

		result << format_ns::format( "({}, {}, {}, {}, {}, {})\n", record_id, tag_id, origin_id, tag_domain_id, internal, internal_count );
	}

	if ( table_printout.size() == 0 )
		result << "\t\t\tNo rows found\n";

	return result;
}

testing::AssertionResult ServerTagFixture::parentInternalExists( const RecordID record_id, TagID parent_id, TagID child_id, std::uint32_t count )
{
	if ( !conn ) throw std::runtime_error( "Connection was nullptr" );
	pqxx::work tx { *conn };

	const auto result { tx.exec_params( "SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $5 AND tag_id = $1 AND origin_id = $2 AND tag_domain_id = $3 AND internal_count = $4)", pqxx::params { parent_id, child_id, default_domain_id, count, record_id } ) };

	if ( result[ 0 ][ 0 ].as< bool >() == false )
	{
		return dumpParentMappings( testing::AssertionFailure(), tx );
	}

	return testing::AssertionSuccess();
}