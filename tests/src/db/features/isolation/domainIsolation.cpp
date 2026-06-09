#include "db/fixtures/MappingFixture.hpp"

class DomainIsolationFixture : public MappingFixture
{
  protected:

	TagDomainID domain_a { 0 };
	TagDomainID domain_b { 0 };

	void SetUp() override
	{
		MappingFixture::SetUp();
		domain_a = createDomain( "domain_a" );
		domain_b = createDomain( "domain_b" );
	}

	void createAliasInDomain( TagID aliased_id, TagID alias_id, TagDomainID domain )
	{
		pqxx::work tx { *conn };
		tx.exec_params(
			"INSERT INTO tag_aliases (aliased_id, alias_id, tag_domain_id) VALUES ($1, $2, $3)",
			pqxx::params { aliased_id, alias_id, domain } );
		tx.commit();
	}

	void createParentInDomain( TagID parent_id, TagID child_id, TagDomainID domain )
	{
		pqxx::work tx { *conn };
		tx.exec_params(
			"INSERT INTO tag_parents (parent_id, child_id, tag_domain_id) VALUES ($1, $2, $3)",
			pqxx::params { parent_id, child_id, domain } );
		tx.commit();
	}

	void createMappingInDomain( TagID tag_id, RecordID record_id, TagDomainID domain )
	{
		pqxx::work tx { *conn };
		tx.exec_params(
			"INSERT INTO tag_mappings (tag_id, record_id, tag_domain_id) VALUES ($1, $2, $3)",
			pqxx::params { tag_id, record_id, domain } );
		tx.commit();
	}

	bool activeMappingExistsInDomain( RecordID record_id, TagID tag_id, TagDomainID domain )
	{
		pqxx::work tx { *conn };
		const auto result { tx.exec_params(
			"SELECT EXISTS(SELECT 1 FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3)",
			pqxx::params { record_id, tag_id, domain } ) };
		return result[ 0 ][ 0 ].as< bool >();
	}
};

TEST_F( DomainIsolationFixture, AliasDifferentDomains )
{
	const auto tag_a { createTag( "isolate:alias_a" ) };
	const auto tag_b { createTag( "isolate:alias_b" ) };
	const auto tag_c { createTag( "isolate:alias_c" ) };

	createAliasInDomain( tag_a, tag_b, domain_a );
	createAliasInDomain( tag_a, tag_c, domain_b );

	// Verify each domain has the correct alias
	{
		pqxx::work tx { *conn };
		auto result_a { tx.exec_params(
			"SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2",
			pqxx::params { tag_a, domain_a } ) };
		ASSERT_EQ( result_a[ 0 ][ 0 ].as< TagID >(), tag_b );
	}

	{
		pqxx::work tx { *conn };
		auto result_b { tx.exec_params(
			"SELECT alias_id FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2",
			pqxx::params { tag_a, domain_b } ) };
		ASSERT_EQ( result_b[ 0 ][ 0 ].as< TagID >(), tag_c );
	}
}

TEST_F( DomainIsolationFixture, ParentDifferentDomains )
{
	const auto tag_child { createTag( "isolate:child" ) };
	const auto tag_parent_a { createTag( "isolate:parent_a" ) };
	const auto tag_parent_b { createTag( "isolate:parent_b" ) };

	createParentInDomain( tag_parent_a, tag_child, domain_a );
	createParentInDomain( tag_parent_b, tag_child, domain_b );

	const auto record { createRecord( "domain_record" ) };

	// Map in domain_a
	createMappingInDomain( tag_child, record, domain_a );

	{
		pqxx::work tx { *conn };
		auto result { tx.exec_params(
			"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3)",
			pqxx::params { record, tag_parent_a, domain_a } ) };
		ASSERT_TRUE( result[ 0 ][ 0 ].as< bool >() );
	}

	// tag_parent_b should NOT appear in domain A
	{
		pqxx::work tx { *conn };
		auto result { tx.exec_params(
			"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3)",
			pqxx::params { record, tag_parent_b, domain_a } ) };
		ASSERT_FALSE( result[ 0 ][ 0 ].as< bool >() );
	}

	// Now map in domain_b
	createMappingInDomain( tag_child, record, domain_b );

	{
		pqxx::work tx { *conn };
		auto result { tx.exec_params(
			"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3)",
			pqxx::params { record, tag_parent_b, domain_b } ) };
		ASSERT_TRUE( result[ 0 ][ 0 ].as< bool >() );
	}
}

TEST_F( DomainIsolationFixture, SameRecordDifferentDomains )
{
	const auto tag { createTag( "isolate:shared_tag" ) };
	const auto record { createRecord( "shared_record" ) };

	createMappingInDomain( tag, record, domain_a );
	createMappingInDomain( tag, record, domain_b );

	ASSERT_TRUE( activeMappingExistsInDomain( record, tag, domain_a ) );
	ASSERT_TRUE( activeMappingExistsInDomain( record, tag, domain_b ) );
}

TEST_F( DomainIsolationFixture, DomainDeletion )
{
	const auto tag { createTag( "isolate:delete_tag" ) };
	const auto record { createRecord( "delete_record" ) };

	createMappingInDomain( tag, record, domain_a );
	ASSERT_TRUE( activeMappingExistsInDomain( record, tag, domain_a ) );

	// Delete the domain
	{
		pqxx::work tx { *conn };
		tx.exec_params( "DELETE FROM tag_domains WHERE tag_domain_id = $1", pqxx::params { domain_a } );
		tx.commit();
	}

	// Domain A's mapping should have been cascaded away
	ASSERT_FALSE( activeMappingExistsInDomain( record, tag, domain_a ) );

	// Record and tag should still exist
	{
		pqxx::work tx { *conn };
		const auto rec_exists {
			tx.exec_params( "SELECT EXISTS(SELECT 1 FROM records WHERE record_id = $1)", pqxx::params { record } )
		};
		ASSERT_TRUE( rec_exists[ 0 ][ 0 ].as< bool >() );
	}
	{
		pqxx::work tx { *conn };
		const auto tag_exists {
			tx.exec_params( "SELECT EXISTS(SELECT 1 FROM tags WHERE tag_id = $1)", pqxx::params { tag } )
		};
		ASSERT_TRUE( tag_exists[ 0 ][ 0 ].as< bool >() );
	}
}
