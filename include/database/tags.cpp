//
// Created by kj16609 on 5/18/22.
//

#include "tags.hpp"

namespace idhan::tags
{


	void validateTables()
	{
		Connection conn;
		pqxx::work w( conn.getConn());

		//Required extentions
		w.exec("CREATE EXTENSION IF NOT EXISTS pg_trgm");
		w.exec("CREATE EXTENSION IF NOT EXISTS pgcrypto;");

		w.exec("CREATE TABLE IF NOT EXISTS FILES (hashID BIGSERIAL PRIMARY KEY , SHA256 TEXT UNIQUE, SHA1 TEXT UNIQUE, MD5 TEXT UNIQUE, imported TIMESTAMP)");
		//Prepares
		conn.getConn().prepare("SELECTFILE", "SELECT * FROM FILES WHERE SHA256 = $1");
		conn.getConn().prepare("INSERTFILE", "INSERT INTO FILES (SHA256, imported) VALUES($1, NOW()) RETURNING hashID");
		//Indexes

		w.exec( "CREATE TABLE IF NOT EXISTS subtags (id BIGSERIAL PRIMARY KEY, text TEXT UNIQUE);" );
		//Prepares
		conn.getConn().prepare( "insertSubtag", "INSERT INTO subtags (text) VALUES ($1) RETURNING id" );
		conn.getConn().prepare( "selectSubtagText", "SELECT text FROM subtags WHERE id = $1" );
		conn.getConn().prepare( "selectSubtag", "SELECT id FROM subtags WHERE text = $1" );
		//Indexes
		w.exec("CREATE INDEX IF NOT EXISTS subtags_text_gin ON subtags USING gin (text gin_trgm_ops)");


		w.exec( "CREATE TABLE IF NOT EXISTS groups (id BIGSERIAL PRIMARY KEY, text TEXT UNIQUE);" );
		//Prepares
		conn.getConn().prepare( "insertGroup", "INSERT INTO groups (text) VALUES ($1) RETURNING id" );
		conn.getConn().prepare( "selectGroup", "SELECT id FROM groups WHERE text = $1" );
		conn.getConn().prepare( "selectGroupText", "SELECT text FROM groups WHERE id = $1" );
		//Indexes
		w.exec("CREATE INDEX IF NOT EXISTS groups_text_gin ON groups USING gin (text gin_trgm_ops)");



		w.exec( "CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT REFERENCES files ON DELETE RESTRICT, tagid BIGINT);" );
		//Prepares
		conn.getConn().prepare( "insertMapping", "INSERT INTO mappings (hashid, tagid) VALUES ($1, $2)" );
		conn.getConn().prepare( "selectMapping", "SELECT tagid FROM mappings WHERE hashID = $1" );
		//Indexes
		w.exec("CREATE INDEX IF NOT EXISTS mappings_tag_index ON mappings (tagid);");
		w.exec("CREATE INDEX IF NOT EXISTS mappings_hash_index ON mappings (hashid);");

		w.commit();
	}


	uint16_t getGroupID( const std::string& text )
	{
		static Cache<std::string, uint16_t> cache;

		auto cached = cache.get( text );

		if ( cached.has_value())
		{
			return cached.value();
		}


		Connection conn;
		pqxx::work w( conn.getConn());
		pqxx::result r = w.exec_prepared( "selectGroup", text );

		if ( r.size() == 0 )
		{
			r = w.exec_prepared( "insertGroup", text );
		}

		cache.place( text, r[0][0].as<uint16_t>());
		return r[0][0].as<uint16_t>();
	}


	uint64_t getSubtagID( const std::string& text )
	{
		static Cache<std::string, uint64_t> cache;

		auto cached = cache.get( text );

		if ( cached.has_value())
		{
			return cached.value();
		}

		Connection conn;
		pqxx::work w( conn.getConn());
		pqxx::result r = w.exec_prepared( "selectSubtag", text );

		if ( r.size() == 0 )
		{
			r = w.exec_prepared( "insertSubtag", text );
		}

		cache.place( text, r[0][0].as<uint64_t>());
		return r[0][0].as<uint64_t>();
	}


	uint64_t getTag( const std::string& group, const std::string& subtag )
	{
		uint16_t groupID = getGroupID( group );
		uint64_t subtagID = getSubtagID( subtag );

		return ( uint64_t( groupID ) << ( 32 + 16 )) | subtagID;
	}

	std::string getGroup( uint64_t tag )
	{
		Connection conn;
		pqxx::work w( conn.getConn());

		uint16_t groupID = static_cast<uint16_t>(tag >> ( 32 + 16 ));

		pqxx::result r = w.exec_prepared( "selectGroupText", groupID );

		return r[0][0].as<std::string>();
	}

	std::string getSubtag( uint64_t tag )
	{
		Connection conn;
		pqxx::work w( conn.getConn());

		uint64_t subtagID{ tag & 0xFFFFFFFFFFFF };

		pqxx::result r = w.exec_prepared( "selectSubtagText", subtagID );

		return r[0][0].as<std::string>();
	}

	std::pair<std::string, std::string> getTagPair( uint64_t tagID )
	{
		Cache<uint64_t, std::pair<std::string, std::string>> cache;

		auto cached = cache.get( tagID );

		if ( cached.has_value())
		{
			return cached.value();
		}

		cache.place( tagID, std::make_pair( getGroup( tagID ), getSubtag( tagID )));
		return std::make_pair( getGroup( tagID ), getSubtag( tagID ));
	}

	std::pair<std::string, std::string> getTagText( uint64_t id )
	{
		return std::make_pair( getGroup( id ), getSubtag( id ));
	}


	namespace tagStream
	{
		void streamAddGroups( const std::vector<std::string>& groups )
		{
			Connection conn;
			pqxx::work w( conn.getConn());

			w.exec( "CREATE TABLE IF NOT EXISTS groupstemp (id BIGSERIAL PRIMARY KEY, text TEXT)" );

			auto stream = pqxx::stream_to::table( w, { "groupstemp" }, { "text" } );

			for ( auto& group: groups )
			{
				stream << group;
			}

			stream.complete();
			w.exec( "INSERT INTO groups (text) SELECT text FROM groupstemp ON CONFLICT DO NOTHING" );
			w.exec( "DROP TABLE groupstemp" );
			w.commit();
		}

		void streamAddSubtags( const std::vector<std::string>& subtags )
		{
			Connection conn;
			pqxx::work w( conn.getConn());

			w.exec( "DROP TABLE IF EXISTS subtagstemp" );
			w.exec( "CREATE TABLE IF NOT EXISTS subtagstemp (id BIGSERIAL PRIMARY KEY, text TEXT)" );

			auto stream = pqxx::stream_to::table( w, { "subtagstemp" }, { "text" } );

			for ( auto& subtag: subtags )
			{
				stream << subtag;
			}

			stream.complete();
			w.exec( "INSERT INTO subtags (text) SELECT text FROM subtagstemp ON CONFLICT DO NOTHING" );
			w.exec( "DROP TABLE subtagstemp" );
			w.commit();
		}

		void streamAddMappings( const std::vector<std::pair<uint64_t, std::vector<uint64_t>>>& mappings )
		{
			Connection conn;
			pqxx::work w( conn.getConn());

			//Ensure the table didn't exist beforehand
			w.exec( "DROP TABLE IF EXISTS mappingstemp" );
			w.exec( "CREATE TABLE IF NOT EXISTS mappingstemp (hashid BIGINT, tagid BIGINT)" );

			auto stream = pqxx::stream_to::table( w, { "mappingstemp" }, { "hashid", "tagid" } );

			for ( auto& [ hashID, tagpairs ]: mappings )
			{
				for(auto& tag : tagpairs)
				{
					stream << std::make_tuple( hashID, tag );
				}

			}

			stream.complete();
			//w.exec( "INSERT INTO mappings (hashid, tagid) SELECT hashid, tagid from mappingstemp ON CONFLICT (hashid) DO UPDATE SET tagid = array( select distinct unnest(mappings.tagid || EXCLUDED.tagid));" );
			w.exec("INSERT INTO mappings (hashid, tagid) SELECT hashid, tagid from mappingstemp ON CONFLICT DO NOTHING;");
			w.exec( "DROP TABLE mappingstemp" );
			w.commit();
		}


	}


}













