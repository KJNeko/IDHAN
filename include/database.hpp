//
// Created by kj16609 on 6/1/22.
//

#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

#include <pqxx/pqxx>


class Connection
{
	static inline bool valid {false};
	static inline pqxx::connection* conn{nullptr};
	
public:
	Connection(const std::string& args = "dbname=idhan user=idhan password=idhan host=localhost port=5432")
	{
		if(!valid)
		{
			conn = new pqxx::connection(args);
			valid = true;
			
			//Run verification pass for the tables
			pqxx::work wrk (*conn);
			
			wrk.exec("CREATE TABLE IF NOT EXISTS subtags (subtagid BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);");
			wrk.exec("CREATE TABLE IF NOT EXISTS groups (groupid SMALLSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);");
			wrk.exec("CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT, groupid SMALLINT REFERENCES groups, subtagid BIGINT REFERENCES subtags);");
			
			wrk.commit();
		}
	}
	
	static auto& getConn()
	{
		return *conn;
	}
	
	void resetDB()
	{
		pqxx::work wrk(*conn);
		
		wrk.exec("drop table if exists groups, mappings, subtags");
		
		wrk.exec("CREATE TABLE IF NOT EXISTS subtags (subtagid BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);");
		wrk.exec("CREATE TABLE IF NOT EXISTS groups (groupid SMALLSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);");
		wrk.exec("CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT, groupid SMALLINT REFERENCES groups, subtagid BIGINT REFERENCES subtags);");
		
		wrk.commit();
		
	}
};


bool addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
{
	try
	{
		Connection conn;
		pqxx::work wrk (conn.getConn());
		
		auto getSubtagID = [&wrk](std::string text)
		{
			//Try a select
			pqxx::result res = wrk.exec("SELECT subtagid FROM subtags WHERE subtag = '" + text + "'");
			
			if(res.empty())
			{
				res = wrk.exec("INSERT INTO subtags (subtag) VALUES ('" + text + "') RETURNING subtagid");
			}
			
			return res[0][0].as<uint64_t>();
		};
		
		auto getGroupID = [&wrk](std::string text)
		{
			//Try a select
			pqxx::result res = wrk.exec("SELECT groupid FROM groups WHERE \"group\" = '" + text + "'");
			
			if(res.empty())
			{
				res = wrk.exec("INSERT INTO groups (\"group\") VALUES ('" + text + "') RETURNING groupid");
			}
			
			return res[0][0].as<uint64_t>();
		};
		
		
		for(auto& [group, subtag] : tags)
		{
			uint16_t groupID = getGroupID(group);
			uint64_t subtagID = getSubtagID(subtag);
			
	
			wrk.exec("INSERT INTO mappings (hashid, groupid, subtagid) VALUES ("+ std::to_string(hashID) +","+ std::to_string(groupID) + "," + std::to_string(subtagID) + ")");
		}
		
		wrk.commit();
		}
	catch(pqxx::unique_violation& e)
	{
		return false;
	}
	catch(pqxx::foreign_key_violation& e)
	{
		return false;
	}
	catch(pqxx::not_null_violation& e)
	{
		return false;
	}
	
	return true;
}

bool removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
{
	try
	{
		Connection conn;
		pqxx::work wrk( conn.getConn());
		
		auto getSubtagID = [&wrk]( std::string text ) -> uint64_t
		{
			//Try a select
			pqxx::result res = wrk.exec(
					"SELECT subtagid FROM subtags WHERE subtag = '" + text + "'" );
			
			if ( res.empty())
			{
				return 0;
			}
			
			return res[0][0].as<uint64_t>();
		};
		
		auto getGroupID = [&wrk]( std::string text ) -> uint64_t
		{
			//Try a select
			pqxx::result res = wrk.exec(
					"SELECT groupid FROM groups WHERE \"group\" = '" + text + "'" );
			
			if ( res.empty())
			{
				return 0;
			}
			
			return res[0][0].as<uint64_t>();
		};
		
		
		for ( auto& [group, subtag] : tags )
		{
			uint16_t groupID = getGroupID( group );
			uint64_t subtagID = getSubtagID( subtag );
			
			if ( groupID == 0 || subtagID == 0 )
			{
				continue;
			}
			
			//delete from mappings
			wrk.exec(
					"DELETE FROM mappings WHERE hashid = " +
					std::to_string( hashID ) + " AND groupid = " +
					std::to_string( groupID ) + " AND subtagid = " +
					std::to_string( subtagID ));
			
			wrk.exec(
					"DELETE FROM subtags WHERE subtagid NOT IN (SELECT subtagid FROM mappings)" );
			wrk.exec(
					"DELETE FROM groups WHERE groupid NOT IN (SELECT groupid FROM mappings)" );
		}
		
		wrk.commit();
	}
	catch(pqxx::unique_violation& e)
	{
		return false;
	}
	catch(pqxx::foreign_key_violation& e)
	{
		return false;
	}
	catch(pqxx::not_null_violation& e)
	{
		return false;
	}
	
	return true;
}

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID)
{
	Connection conn;
	pqxx::work wrk(conn.getConn());
	
	pqxx::result res = wrk.exec("SELECT \"group\", subtag FROM mappings NATURAL JOIN groups NATURAL JOIN subtags WHERE hashid = " + std::to_string(hashID));
	
	std::vector<std::pair<std::string, std::string>> tags;
	
	for( pqxx::row row : res)
	{
		tags.emplace_back(row[0].as<std::string>(), row[1].as<std::string>());
	}
	
	return tags;
}








#endif //MAIN_DATABASE_HPP
