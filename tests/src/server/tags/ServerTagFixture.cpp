//
// Created by kj16609 on 8/18/25.
//

#include "ServerTagFixture.hpp"

#include "ServerDBFixture.hpp"
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