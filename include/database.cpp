//
// Created by kj16609 on 6/7/22.
//

#include "database.hpp"

#include <fstream>

void Connection::createTables()
{
	pqxx::work wrk(*conn);
	
	wrk.exec("CREATE TABLE IF NOT EXISTS files (hashid BIGSERIAL PRIMARY KEY, sha256 BYTEA UNIQUE, md5 BYTEA UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS playerinfo (hashid BIGINT PRIMARY KEY REFERENCES files, type SMALLINT, frames BIGINT, bytes BIGINT, width BIGINT, height BIGINT, duration BIGINT, fps BIGINT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS importinfo (hashid BIGINT PRIMARY KEY REFERENCES files, time TIMESTAMP, filename TEXT)");
	wrk.exec("CREATE TABLE IF NOT EXISTS subtags (subtagid BIGSERIAL PRIMARY KEY, subtag TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS groups (groupid SMALLSERIAL PRIMARY KEY, \"group\" TEXT UNIQUE);");
	wrk.exec("CREATE TABLE IF NOT EXISTS mappings (hashid BIGINT REFERENCES files, groupid SMALLINT REFERENCES groups, subtagid BIGINT REFERENCES subtags);");
	
	wrk.commit();
}

void Connection::prepareStatements()
{
	//Prepare statements
	conn->prepare("selectFile", "SELECT hashid FROM files WHERE sha256 = $1");
	conn->prepare("insertFile", "INSERT INTO files (sha256, md5) VALUES ($1, $2) RETURNING hashid");
	conn->prepare("insertPlayerInfo", "INSERT INTO playerinfo (hashid, type, frames, bytes, width, height, duration, fps) VALUES ($1, $2, $3, $4, $5, $6, $7, $8)");
	conn->prepare("insertImportInfo", "INSERT INTO importinfo (hashid, filename, time) VALUES ($1, $2, NOW()::timestamp)");
}

Connection::Connection(const std::string& args)
{
	if(!valid)
	{
		conn = new pqxx::connection(args);
		valid = true;
		
		//Run verification pass for the tables
		createTables();
		prepareStatements();
	}
}

void Connection::resetDB()
{
	
	pqxx::work wrk(*conn);
	wrk.exec("drop table if exists files, playerinfo, importinfo, subtags, groups, mappings");
	wrk.commit();
	
	createTables();
}
	


uint64_t addFile(std::filesystem::path path)
{
	using MrMime::header_data_buffer_t;
	if(!std::filesystem::exists(path))
	{
		std::string currentpwd = std::filesystem::current_path().string();
		throw std::runtime_error("Unable to find file: " + path.string() + " in " + currentpwd);
	}
	
	const std::size_t size{std::filesystem::file_size(path)};
	
	if (size < sizeof(header_data_buffer_t))
	{
		return 0;
	}
	
	MrMime::header_data_buffer_t buffer;
	auto ifs{std::ifstream(path, std::ios::binary)};
	ifs.exceptions(std::ifstream::badbit | std::ifstream::failbit | std::ifstream::eofbit);
	ifs.read(reinterpret_cast<char*>(&buffer), sizeof(buffer));
	ifs.seekg(std::ifstream::beg);
	
	auto MIMEType = MrMime::deduceFileType(buffer);
	
	switch(MIMEType)
	{
		
		case MrMime::IMAGE_JPEG: [[fallthrough]];
		case MrMime::IMAGE_PNG: [[fallthrough]];
		case MrMime::IMAGE_GIF: [[fallthrough]];
		case MrMime::IMAGE_BMP:
		{
			//Load the rest of the file into memory
			std::vector<uint8_t> image_data;
			image_data.resize(size);
			ifs.read(reinterpret_cast<char*>(image_data.data()), static_cast<long>(size) );
			
			auto md5 = MD5(image_data);
			auto sha256 = SHA256(image_data);
			
			std::basic_string_view<std::byte> sha256_view = std::basic_string_view(reinterpret_cast<std::byte*>(sha256.data()), sha256.size());
			std::basic_string_view<std::byte> md5_view = std::basic_string_view(reinterpret_cast<std::byte*>(md5.data()), md5.size());
			
			
			pqxx::work wrk(Connection::getConn());
			//Check if we have already imported the file before
			auto res = wrk.exec_prepared("selectFile", sha256_view);
			if(res.size() == 0)
			{
				//Insert the file into the database
				res = wrk.exec_prepared("insertFile", sha256_view, md5_view);
				
				//Insert the player info
				uint64_t hashid = res[0][0].as<uint64_t>();
				
				//Check if Animated
				
				
				//Open image from array
				
				
				
				
				wrk.exec_prepared("insertPlayerInfo", hashid, static_cast<uint16_t>(MIMEType), 1, size, img.rows, img.cols, 0, 0);
				
				//Insert the import info
				wrk.exec_prepared("insertImportInfo", hashid, path.filename().string());
			}
			
			
			
			wrk.commit();
			return res[0][0].as<uint64_t>();
		}
			break;
		case MrMime::APPLICATION_FLASH:
			break;
		case MrMime::IMAGE_ICON:
			break;
		case MrMime::VIDEO_FLV:
			break;
		case MrMime::APPLICATION_PDF:
			break;
		case MrMime::APPLICATION_ZIP:
			break;
		case MrMime::APPLICATION_HYDRUS_ENCRYPTED_ZIP:
			break;
		case MrMime::VIDEO_MP4:
			break;
		case MrMime::AUDIO_FLAC:
			break;
		case MrMime::UNDETERMINED_WM:
			break;
		case MrMime::IMAGE_APNG:
			break;
		case MrMime::VIDEO_MOV:
			break;
		case MrMime::VIDEO_AVI:
			break;
		case MrMime::APPLICATION_RAR:
			break;
		case MrMime::APPLICATION_7Z:
			break;
		case MrMime::IMAGE_WEBP:
			break;
		case MrMime::IMAGE_TIFF:
			break;
		case MrMime::APPLICATION_PSD:
			break;
		case MrMime::APPLICATION_CLIP:
			break;
		case MrMime::AUDIO_WAVE:
			break;
		case MrMime::APPLICATION_UNKNOWN:
			break;
	}
	
	return 0;
}


void addTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
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

void removeTag(uint64_t hashID, std::vector<std::pair<std::string, std::string>> tags)
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
		
		wrk.exec("DELETE FROM subtags WHERE subtagid NOT IN (SELECT subtagid FROM mappings)" );
		wrk.exec("DELETE FROM groups WHERE groupid NOT IN (SELECT groupid FROM mappings)" );
	}
	
	wrk.commit();
}

std::vector<std::pair<std::string, std::string>> getTags(uint64_t hashID)
{
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
	std::cout << "Replacing tag " << origin.first << ":" << origin.second << " with " << replacement.first << ":" << replacement.second << std::endl;
	
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
	
	std::cout << "Command: " << "UPDATE mappings SET groupid = " +
								std::to_string( replacegroup ) + ", subtagid = " +
								std::to_string( replacesubtag ) + " WHERE groupid = " +
								std::to_string( origingroup ) + " AND subtagid = " +
								std::to_string( originsubtag ) << std::endl;
	
	wrk.commit();
}


nlohmann::json getFileinfo(uint64_t hashID)
{
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