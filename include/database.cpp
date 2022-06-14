//
// Created by kj16609 on 6/7/22.
//

#include "database.hpp"
#include "utility/fileutils.hpp"
#include "utility/config.hpp"
#include "idhanthreads.hpp"
#include "MrMime/mister_mime.hpp"
#include "services/thumbnailer.hpp"


#include <fstream>
#include <future>


//vips
#include <vips/vips8>
#include <vips/VImage8.h>
#include <iostream>

#include <TracyBox.hpp>

void Connection::createTables()
{
	pqxx::work wrk(conn);
	
	wrk.exec("CREATE TABLE IF NOT EXISTS files (hashid BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE, md5 BYTEA UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS playerinfo (hashid BIGINT PRIMARY KEY REFERENCES files ON DELETE CASCADE, type SMALLINT, frames BIGINT, bytes BIGINT, width BIGINT, height BIGINT, duration BIGINT, fps BIGINT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS importinfo (hashid BIGINT PRIMARY KEY REFERENCES files ON DELETE CASCADE, time TIMESTAMP, filename TEXT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS subtags (subtagid BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS groups (groupid SMALLSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT REFERENCES files ON DELETE CASCADE, groupid SMALLINT REFERENCES groups ON DELETE CASCADE, subtagid BIGINT REFERENCES subtags ON DELETE CASCADE);");
	
	wrk.commit();
}

void Connection::prepareStatements()
{
	//Prepare statements
	
	
	//Files prepare
	conn.prepare("selectFile", "SELECT hashid FROM files WHERE sha256 = $1");
	conn.prepare("selectFileSHA256", "SELECT sha256 FROM files WHERE hashid = $1");
	conn.prepare("insertFileSHA256", "INSERT INTO files (sha256) VALUES ($1) RETURNING hashid");
	
	
	//playerinfo prepare
	conn.prepare("insertPlayerInfo", "INSERT INTO playerinfo (hashid, type, frames, bytes, width, height, duration, fps) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");
	conn.prepare("insertBasicPlayerInfo", "INSERT INTO playerinfo (hashid, type) VALUES ($1, $2)");
	conn.prepare("selectFileMimeType", "SELECT type FROM playerinfo WHERE hashid = $1");
	
	
	//importinfo prepare
	conn.prepare("insertImportInfo", "INSERT INTO importinfo (hashid, filename, time) VALUES ($1, $2, NOW()::timestamp)");
	conn.prepare("selectFilename", "SELECT filename FROM importinfo WHERE hashid = $1");
}

void Connection::resetDB()
{
	
	pqxx::work wrk(conn);
	wrk.exec("drop table if exists files, playerinfo, importinfo, subtags, groups, mappings");
	wrk.commit();
	
	createTables();
}

uint64_t addFile(std::filesystem::path path)
{
	uint64_t hashID = 0;
	{
		ZoneScopedN( "addFile" );
		using MrMime::header_data_buffer_t;
		if ( !std::filesystem::exists( path ))
		{
			std::string currentpwd = std::filesystem::current_path().string();
			throw std::runtime_error(
					"Unable to find file: " + path.string() + " in " +
					currentpwd );
		}
		
		const std::size_t size { std::filesystem::file_size( path ) };
		
		if ( size < sizeof( header_data_buffer_t ))
		{
			return 0;
		}
		
		TracyCZoneN( readfile, "ReadFile", true );
		//Read the rest of the file data
		std::vector<uint8_t> data;
		data.resize( size );
		
		auto ifs { std::ifstream( path, std::ios::binary ) };
		ifs.exceptions(
				std::ifstream::badbit | std::ifstream::failbit |
				std::ifstream::eofbit );
		ifs.read(
				reinterpret_cast<char*>(data.data()),
				static_cast<long int>(size));
		TracyCZoneEnd( readfile );
		
		TracyCZoneN( bufferRead, "MimeParsing", true );
		MrMime::header_data_buffer_t buffer;
		memcpy( buffer.data(), data.data(), sizeof( buffer ));
		TracyCZoneN( deduce, "DeduceFiletype", true );
		const auto MIMEType = MrMime::deduceFileType( buffer );
		TracyCZoneEnd( deduce );
		TracyCZoneEnd( bufferRead );
		
		TracyCZoneN( sha256Tracy, "SHA256", true );
		//Calculate SHA256 of the file
		//const std::vector<uint8_t> sha256 = std::move(SHA256(data));
		const std::vector<uint8_t> sha256 = SHA256( data );
		const std::basic_string_view<std::byte> sha256_view {
				reinterpret_cast<const std::byte*>(sha256.data()),
				sha256.size() };
		TracyCZoneEnd( sha256Tracy );
		
		TracyCZoneN( DBTrans, "DatabaseTransaction", true );
		//Check if the file already exists in the database records
		Connection conn;
		pqxx::work wrk( conn.getConn());
		TracyCZoneN( selectFileQuery, "selectFile", true );
		pqxx::result res = wrk.exec_prepared( "selectFile", sha256_view );
		TracyCZoneEnd( selectFileQuery );
		
		if ( res.size() > 0 )
		{
			hashID = res[0][0].as<uint64_t>();
		}
		else
		{
			TracyCZoneN( insertFileSHA256, "insertFileSHA256", true );
			//Insert the file into the database
			res = wrk.exec_prepared( "insertFileSHA256", sha256_view );
			TracyCZoneEnd( insertFileSHA256 );
			if ( res.size() == 0 )
			{
				wrk.abort();
				return 0;
			}
			
			//Insert quick mime data
			hashID = res[0][0].as<uint64_t>();
			
			TracyCZoneN( insertPlayerBasicInfo, "insertBasicPlayerInfo", true );
			wrk.exec_prepared(
					"insertBasicPlayerInfo", hashID,
					static_cast<uint16_t>(MIMEType));
			wrk.exec_prepared( "insertImportInfo", hashID, path.string());
			TracyCZoneEnd( insertPlayerBasicInfo );
		}
		TracyCZoneEnd( DBTrans );
		
		//Calculate the filename and path
		TracyCZoneN( movefile, "MoveFile", true );
		const std::string fileHex = idhan::utils::toHex( sha256 );
		
		auto modifiedPath = idhan::config::fileconfig::file_path;
		modifiedPath /= "f" + fileHex.substr( 0, 2 );
		modifiedPath /= fileHex;
		
		modifiedPath.replace_extension( path.extension());
		
		//Create the directory if it doesn't exist
		if ( !std::filesystem::exists( modifiedPath ))
		{
			if(!idhan::config::debug)
			{
				std::filesystem::create_directories( modifiedPath.parent_path());
			}
		}
		
		//Move the file
		std::filesystem::rename( path, modifiedPath );
		
		TracyCZoneEnd( movefile );
		wrk.commit();
	}
	
	if(idhan::config::thumbnail_active)
	{
		idhan::services::Thumbnailer::enqueue( hashID );
	}
	return hashID;
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

void replaceTag(std::pair<std::string, std::string> origin, std::pair<std::string, std::string> replacement)
{
	ZoneScopedN("replaceTag");
	Connection conn;
	pqxx::work wrk( conn.getConn());
	
	auto selectSubtagID = [&wrk]( std::string text )
	{
		//Try a select
		pqxx::result res = wrk.exec(
				"SELECT subtagid FROM subtags WHERE subtag = '" + text +
				"'" );
		
		if ( res.empty())
		{
			res = wrk.exec(
					"INSERT INTO subtags (subtag) VALUES ('" + text +
					"') RETURNING subtagid" );
		}
		
		return res[0][0].as<uint64_t>();
	};
	
	auto selectGroupID = [&wrk]( std::string text )
	{
		//Try a select
		pqxx::result res = wrk.exec(
				"SELECT groupid FROM groups WHERE \"group\" = '" + text +
				"'" );
		
		if ( res.empty())
		{
			res = wrk.exec(
					"INSERT INTO groups (\"group\") VALUES ('" + text +
					"') RETURNING groupid" );
		}
		
		return res[0][0].as<uint64_t>();
	};
	
	auto getSubtagID = [&wrk]( std::string text )
	{
		//Try a select
		pqxx::result res = wrk.exec(
				"SELECT subtagid FROM subtags WHERE subtag = '" + text +
				"'" );
		
		if ( res.empty())
		{
			res = wrk.exec(
					"INSERT INTO subtags (subtag) VALUES ('" + text +
					"') RETURNING subtagid" );
		}
		
		return res[0][0].as<uint64_t>();
	};
	
	auto getGroupID = [&wrk]( std::string text )
	{
		//Try a select
		pqxx::result res = wrk.exec(
				"SELECT groupid FROM groups WHERE \"group\" = '" + text +
				"'" );
		
		if ( res.empty())
		{
			res = wrk.exec(
					"INSERT INTO groups (\"group\") VALUES ('" + text +
					"') RETURNING groupid" );
		}
		
		return res[0][0].as<uint64_t>();
	};
	
	auto [origingroup, originsubtag] = std::make_pair(
			selectGroupID( origin.first ), selectSubtagID( origin.second ));
	auto [replacegroup, replacesubtag] = std::make_pair(
			getGroupID( replacement.first ),
			getSubtagID( replacement.second ));
	
	wrk.exec(
			"UPDATE mappings SET groupid = " +
			std::to_string( replacegroup ) + ", subtagid = " +
			std::to_string( replacesubtag ) + " WHERE groupid = " +
			std::to_string( origingroup ) + " AND subtagid = " +
			std::to_string( originsubtag ));
	
	wrk.commit();
}


nlohmann::json getFileinfo(uint64_t hashID)
{
	ZoneScopedN("getFileinfo");
	Connection conn;
	pqxx::work wrk(conn.getConn());
	
	nlohmann::json j;
	
	//mappings
	{
		pqxx::result res = wrk.exec("SELECT \"group\", subtag FROM mappings NATURAL JOIN subtags NATURAL JOIN groups WHERE hashid = " + std::to_string(hashID));
		
		//Combine
		std::vector<std::string> tags;
		for( pqxx::row row : res)
		{
			tags.emplace_back(row[0].as<std::string>() + ":" + row[1].as<std::string>());
		}
		
		j["mappings"] = tags;
	}
	
	//playerinfo
	{
		pqxx::result res = wrk.exec("SELECT type, frames, bytes, width, height, (CAST(fps as float) * CAST(frames as float)) as duration, fps FROM playerinfo WHERE hashid = " + std::to_string(hashID));
		
		if(res.size() == 1)
		{
			pqxx::row row = res[0];
			
			j["playerinfo"]["type"] = row[0].as<uint64_t>();
			j["playerinfo"]["frames"] = row[1].as<uint64_t>();
			j["playerinfo"]["bytes"] = row[2].as<uint64_t>();
			j["playerinfo"]["width"] = row[3].as<uint64_t>();
			j["playerinfo"]["height"] = row[4].as<uint64_t>();
			j["playerinfo"]["duration"] = row[5].as<uint64_t>();
			j["playerinfo"]["fps"] = row[6].as<uint64_t>();
		}
	}
	
	//importinfo
	{
		pqxx::result res = wrk.exec("SELECT time, filename FROM importinfo WHERE hashid = " + std::to_string(hashID));
		
		if(res.size() == 1)
		{
			j["importinfo"]["time"] = res[0][0].as<std::string>();
			j["importinfo"]["filename"] = res[0][1].as<std::string>();
		}
	}
	
	return j;
}