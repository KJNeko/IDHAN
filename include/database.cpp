//
// Created by kj16609 on 6/7/22.
//

#include "database.hpp"
#include "utility/fileutils.hpp"
#include "utility/config.hpp"
#include "MrMime/mister_mime.hpp"
#include "services/thumbnailer.hpp"


#include <fstream>
#include <future>


//vips
#include <iostream>

#include <TracyBox.hpp>


void ConnectionRevolver::prepareStatements( pqxx::connection& conn )
{
	//Prepare statements
	
	//Files prepare
	conn.prepare("selectFile", "SELECT hashid FROM files WHERE sha256 = $1");
	conn.prepare("selectFileSHA256", "SELECT sha256 FROM files WHERE hashid = $1");
	conn.prepare("insertFileSHA256", "INSERT INTO files (sha256) VALUES ($1) RETURNING hashid");
	conn.prepare("insertFile", "INSERT INTO files (sha256, md5) VALUES ($1, $2) RETURNING hashid");
	
	
	//playerinfo prepare
	conn.prepare("insertPlayerInfo", "INSERT INTO playerinfo (hashid, type, frames, bytes, width, height, duration, fps) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");
	conn.prepare("insertBasicPlayerInfo", "INSERT INTO playerinfo (hashid, type) VALUES ($1, $2)");
	conn.prepare("selectFileMimeType", "SELECT type FROM playerinfo WHERE hashid = $1");
	
	
	//importinfo prepare
	conn.prepare("insertImportInfo", "INSERT INTO importinfo (hashid, filename, time) VALUES ($1, $2, NOW()::timestamp)");
	conn.prepare("selectFilename", "SELECT filename FROM importinfo WHERE hashid = $1");
}

void ConnectionRevolver::createTables()
{
	pqxx::connection conn("dbname=idhan user=idhan password=idhan host=localhost port=5432");
	pqxx::work wrk(conn);
	
	wrk.exec("CREATE TABLE IF NOT EXISTS files (hashid BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE, md5 BYTEA UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS playerinfo (hashid BIGINT PRIMARY KEY REFERENCES files ON DELETE CASCADE, type SMALLINT, frames BIGINT, bytes BIGINT, width BIGINT, height BIGINT, duration BIGINT, fps BIGINT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS importinfo (hashid BIGINT PRIMARY KEY REFERENCES files ON DELETE CASCADE, time TIMESTAMP, filename TEXT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS subtags (subtagid BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS groups (groupid SMALLSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT REFERENCES files ON DELETE CASCADE, groupid SMALLINT REFERENCES groups ON DELETE CASCADE, subtagid BIGINT REFERENCES subtags ON DELETE CASCADE);");
	
	wrk.commit();
}

void ConnectionRevolver::resetDB()
{
	pqxx::connection conn("dbname=idhan user=idhan password=idhan host=localhost port=5432");
	pqxx::work wrk(conn);
	wrk.exec("drop table if exists files, playerinfo, importinfo, subtags, groups, mappings");
	wrk.commit();
	
	createTables();
}

std::pair<pqxx::connection&, bool&> ConnectionRevolver::getConnection()
{
	//See if we have an connection available
	{
		std::lock_guard<std::mutex> lock(connLock);
		for (auto& conn : connections)
		{
			if (!conn.second)
			{
				conn.second = true;
				return {conn.first, conn.second};
			}
		}
	}
	
	TracyCPlot("connections vector", static_cast<double>(connections.size()));
	
	//No connection available, create a new one
	std::lock_guard<std::mutex> lock(connLock);
	auto& conn = connections.emplace_back(std::make_pair(pqxx::connection("dbname=idhan user=idhan password=idhan host=localhost port=5432"), false));
	//Run the prepares on it
	prepareStatements(conn.first);
	conn.second = true;
	return {conn.first, conn.second};
}

uint64_t addFileMapping(std::array<uint8_t, 32>& SHA256)
{
	std::basic_string_view<std::byte> SHA256_view(reinterpret_cast<std::byte*>(SHA256.data()), SHA256.size());
	
	Connection conn;
	pqxx::work wrk(conn.getConn());
	
	//Check if the file is already in the database
	pqxx::result res = wrk.exec_prepared("selectFile", SHA256_view);
	if (res.size() > 0)
	{
		return res[0][0].as<uint64_t>();
	}
	else
	{
		//Insert the file
		res = wrk.exec_prepared("insertFileSHA256", SHA256_view);
		return res[0][0].as<uint64_t>();
	}
}


void removeFile(uint64_t id)
{
	ZoneScopedN("removeFile");
	
	Connection conn;
	pqxx::work wrk(conn.getConn());
	
	//Get the SHA256 of the file
	pqxx::result res = wrk.exec_prepared("selectFileSHA256", id);
	if(res.size() == 0)
	{
		wrk.abort();
		return;
	}
	
	auto sha256_view = res[0][0].as<std::string_view>();
	
	const std::vector<uint8_t> sha256 = std::vector<uint8_t>(sha256_view.begin(), sha256_view.end());
	
	//Calculate the filepath
	const std::string fileHex = idhan::utils::toHex(sha256);
	
	auto modifiedPath = idhan::config::fileconfig::file_path;
	modifiedPath /= "f" + fileHex.substr(0,2);
	modifiedPath /= fileHex;
	
	//Remove the file
	if(!idhan::config::debug)
	{
		std::filesystem::remove(modifiedPath);
	}
	
	//Remove all DB mappings to the file
	wrk.exec("DELETE FROM files WHERE hashid = " + std::to_string(id));
	
	wrk.commit();
}


void addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
{
	ZoneScopedN("addTag");
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
		
		return res[0][0].as<uint16_t>();
	};
	
	
	for(auto& [group, subtag] : tags)
	{
		uint16_t groupID = getGroupID(group);
		uint64_t subtagID = getSubtagID(subtag);
		
		
		wrk.exec("INSERT INTO mappings (hashid, groupid, subtagid) VALUES ("+ std::to_string(hashID) +","+ std::to_string(static_cast<unsigned int>(groupID)) + "," + std::to_string(subtagID) + ")");
	}
	
	wrk.commit();
}

void removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
{
	ZoneScopedN("removeTag");
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
	
	auto getGroupID = [&wrk]( std::string text ) -> uint16_t
	{
		//Try a select
		pqxx::result res = wrk.exec(
				"SELECT groupid FROM groups WHERE \"group\" = '" + text + "'" );
		
		if ( res.empty())
		{
			return 0;
		}
		
		return res[0][0].as<uint16_t>();
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
				std::to_string( static_cast<unsigned int>(groupID) ) + " AND subtagid = " +
				std::to_string( subtagID ));
		
		wrk.exec("DELETE FROM subtags WHERE subtagid NOT IN (SELECT subtagid FROM mappings)" );
		wrk.exec("DELETE FROM groups WHERE groupid NOT IN (SELECT groupid FROM mappings)" );
	}
	
	wrk.commit();
}

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID)
{
	ZoneScopedN("getTags");
	Connection conn;
	pqxx::work wrk(conn.getConn());
	
	pqxx::result exists = wrk.exec("SELECT hashid FROM files WHERE hashid = " + std::to_string(hashID));
	if(exists.empty())
	{
		throw std::runtime_error("HashID does not exist");
	}
	
	pqxx::result res = wrk.exec("SELECT \"group\", subtag FROM mappings NATURAL JOIN groups NATURAL JOIN subtags WHERE hashid = " + std::to_string(hashID));
	
	std::vector<std::pair<std::string, std::string>> tags;
	
	for( pqxx::row row : res)
	{
		tags.emplace_back(row[0].as<std::string>(), row[1].as<std::string>());
	}
	
	return tags;
}
