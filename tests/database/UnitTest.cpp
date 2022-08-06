//
// Created by kj16609 on 8/5/22.
//

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/files/files.hpp"
#include "DatabaseModule/tags/tags.hpp"
#include "DatabaseModule/tags/mappings.hpp"
#include "DatabaseModule/files/metadata.hpp"

#include <gtest/gtest.h>

#include <iostream>


#define CONNECTION_STR "host=localhost dbname=idhan_test user=postgres"

TEST( DatabaseTestMeta, CreateTestingArea )
{
	try
	{
		{
			pqxx::connection connection { "dbname=idhan_test user=postgres password=postgres" };
		}
		//idhan_test already exists. Drop it
		pqxx::connection connection2 { "dbname=template1 user=postgres" };

		pqxx::nontransaction work2 { connection2 };
		work2.exec( "DROP DATABASE idhan_test" );
	}
	catch ( ... )
	{
		//In this case the table does not exist
	}

	pqxx::connection conn { "host=localhost dbname=template1 user=postgres" };

	pqxx::nontransaction work { conn };
	work.exec( "CREATE DATABASE idhan_test" );

	pqxx::connection conn_test { CONNECTION_STR };

	SUCCEED();
}


TEST( DatabaseTest, InitalizationTest )
{
	Database::initalizeConnection( CONNECTION_STR );

	SUCCEED();
}


TEST( DatabaseTest, WrongArguments )
{
	EXPECT_ANY_THROW( Database::initalizeConnection( "derpmfuck" ) );

	SUCCEED();
}


TEST( DatabaseTest, AquireConnection )
{

	Database::initalizeConnection( CONNECTION_STR );

	Connection test;
	auto work { test.getWork() };

	work->exec( "CREATE TABLE IF NOT EXISTS test (id INTEGER PRIMARY KEY, text TEXT);" );

	work->exec( "DROP TABLE IF EXISTS test;" );

	work->commit();

	SUCCEED();
}


static constexpr std::array< uint8_t, 32 > HASH_8 { 0x62, 0x7a, 0x26, 0xd4, 0x8e, 0xbe, 0xbb, 0x98, 0xcd, 0x9f, 0x13,
	0x41, 0x19, 0xe9, 0x0f, 0xe1, 0x3e, 0xff, 0xc4, 0x70, 0x31, 0xe5, 0x1f, 0xbc, 0x81, 0xda, 0xa9, 0x95, 0x26, 0xeb,
	0x03, 0xb4 };

TEST( files, HashConversion )
{
	const QByteArray hash_test { reinterpret_cast<const char*>(HASH_8.data()), HASH_8.size() };

	ASSERT_EQ( hash_test, Hash32(HASH_8).getQByteArray() );
}


TEST( files, DualConversion )
{
	const QByteArray hash_test { reinterpret_cast<const char*>(HASH_8.data()), HASH_8.size() };

	ASSERT_EQ( Hash32( HASH_8 ), Hash32( hash_test ) );
}


TEST( files, AddFile )
{
	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	EXPECT_GT( files::async::addFile( Hash32( HASH_8 ) ).result(), static_cast<uint64_t>(0) );

	SUCCEED();
}


TEST( files, GetFileID )
{

	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t hash_id { files::addFile( Hash32( HASH_8 ) ) };

	EXPECT_EQ( files::async::getFileID( Hash32( HASH_8 ) ).result(), hash_id );

	SUCCEED();
}


TEST( files, getHash )
{
	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t hash_id { files::addFile( Hash32( HASH_8 ) ) };

	EXPECT_EQ( files::getHash( hash_id ), Hash32( HASH_8 ) );

	SUCCEED();
}


inline static const Group group { "character" };
inline static const Subtag subtag { "toujou koneko" };

TEST( tags, AddTag )
{

	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	EXPECT_EQ( tags::createTag( group, subtag ), 1 );

	SUCCEED();
}


TEST( tags, getTag )
{
	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t tag_id { tags::createTag( group, subtag ) };
	EXPECT_EQ( tags::getTag( tag_id ), Tag( group, subtag, tag_id ) );
}


TEST( tags, getTagID )
{
	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t tag_id { tags::createTag( group, subtag ) };
	EXPECT_EQ( tags::getTagID( group, subtag ), tag_id );
}


TEST( tags, deleteTagID )
{
	Database::initalizeConnection( CONNECTION_STR );

	const Connection conn;
	auto work { conn.getWork() };

	const uint64_t tag_id { tags::createTag( group, subtag ) };
	tags::deleteTagFromID( tag_id );

	//Verify everything was deleted
	constexpr pqxx::zview select_subtags { "SELECT count(*) FROM subtags" };
	constexpr pqxx::zview select_tags { "SELECT count(*) FROM tags" };
	constexpr pqxx::zview select_groups { "SELECT count(*) FROM groups" };

	EXPECT_EQ( work->exec( select_subtags ).at( 0 ).at( 0 ).as< uint64_t >(), 0 );
	EXPECT_EQ( work->exec( select_tags ).at( 0 ).at( 0 ).as< uint64_t >(), 0 );
	EXPECT_EQ( work->exec( select_groups ).at( 0 ).at( 0 ).as< uint64_t >(), 0 );

	EXPECT_ANY_THROW( tags::getTagID( group, subtag ) );
}


TEST( mappings, addMapping )
{
	Database::initalizeConnection( CONNECTION_STR );
	const Connection conn;
	auto work { conn.getWork() };
	const uint64_t tag_id { tags::createTag( group, subtag ) };
	const uint64_t file_id { files::addFile( Hash32( HASH_8 ) ) };
	mappings::addMapping( file_id, tag_id );

	EXPECT_GT( work->exec( "SELECT count(*) FROM mappings" ).affected_rows(), 0 );

	SUCCEED();
}


TEST( mappings, getMappings )
{
	Database::initalizeConnection( CONNECTION_STR );
	const Connection conn;
	auto work { conn.getWork() };
	const uint64_t tag_id { tags::createTag( group, subtag ) };
	const uint64_t file_id { files::addFile( Hash32( HASH_8 ) ) };
	mappings::addMapping( file_id, tag_id );
	EXPECT_EQ( mappings::getMappings( file_id ), std::vector< Tag > { tags::getTag( tag_id ) } );
	SUCCEED();
}


TEST( mappings, deleteMapping )
{
	Database::initalizeConnection( CONNECTION_STR );
	const Connection conn;
	auto work { conn.getWork() };
	const uint64_t tag_id { tags::createTag( group, subtag ) };
	const uint64_t file_id { files::addFile( Hash32( HASH_8 ) ) };
	mappings::addMapping( file_id, tag_id );
	mappings::deleteMapping( file_id, tag_id );
	EXPECT_EQ( mappings::getMappings( file_id ), std::vector< Tag > {} );
	SUCCEED();
}


TEST( DatabaseTestMetaFinal, Cleanup )
{
	//idhan_test already exists. Drop it
	pqxx::connection connection2 { "dbname=template1 user=postgres" };

	pqxx::nontransaction work2 { connection2 };
	work2.exec( "DROP DATABASE idhan_test" );

	SUCCEED();
}

