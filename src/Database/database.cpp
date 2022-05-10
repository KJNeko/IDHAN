//
// Created by kj16609 on 5/10/22.
//

#include "include/Database/database.hpp"
#include "include/Database/config.hpp"

#include <pqxx/pqxx>
#include <string>


std::string IDHANDatabase::createConnStr()
{
	IDHANConfig config;

	std::string connStr;

	auto dbName = config.getValue<std::string>( "database_name" );
	auto dbUser = config.getValue<std::string>( "database_user" );
	auto dbPass = config.getValue<std::string>( "database_password" );
	auto dbHost = config.getValue<std::string>( "database_host" );
	auto dbPort = config.getValue<std::string>( "database_port" );

	if ( dbName.has_value() && dbUser.has_value() && dbPass.has_value() && dbHost.has_value() && dbPort.has_value())
	{
		connStr = "dbname=" + dbName.value() + " user=" + dbUser.value() + " password=" + dbPass.value() + " host=" +
				dbHost.value() + " port=" + dbPort.value();
	}
	else
	{
		connStr = "dbname=idhan user=idhan password=idhan host=localhost port=5432";

		//Set config values
		config.setValue<std::string>( "database_name", "idhan" );
		config.setValue<std::string>( "database_user", "idhan" );
		config.setValue<std::string>( "database_password", "idhan" );
		config.setValue<std::string>( "database_host", "localhost" );
		config.setValue<std::string>( "database_port", "5432" );
	}

	std::cout << "Connection string: " << connStr << std::endl;

	return connStr;
}

IDHANDatabase::IDHANDatabase() : conn( createConnStr())
{
	try
	{
		//Create work
		pqxx::work work( conn );

		//Hash table
		work.exec(
				"CREATE TABLE IF NOT EXISTS hashes (hash_id BIGSERIAL PRIMARY KEY , hash_sha256 VARCHAR(32) UNIQUE, hash_md5 VARCHAR(16) UNIQUE, hash_sha1 VARCHAR(40) UNIQUE);" );

		//Metadata table
		work.exec(
				"CREATE TABLE IF NOT EXISTS metadata (hash_id BIGINT PRIMARY KEY NOT NULL, import_date TIMESTAMP, type SMALLINT);" );

		//Mappings table
		work.exec(
				"CREATE TABLE IF NOT EXISTS mappings (hash_id BIGINT PRIMARY KEY NOT NULL, tag_id BIGINT NOT NULL, group_id BIGINT NOT NULL);" );

		//Tag table
		work.exec(
				"CREATE TABLE IF NOT EXISTS tags (tag_id BIGSERIAL PRIMARY KEY NOT NULL, subtag_id BIGINT NOT NULL, namespace_id BIGINT NOT NULL);" );

		//Namespace table
		work.exec(
				"CREATE TABLE IF NOT EXISTS namespaces (namespace_id BIGSERIAL PRIMARY KEY NOT NULL, namespace TEXT);" );

		//Subtag table
		work.exec( "CREATE TABLE IF NOT EXISTS subtags (subtag_id BIGSERIAL PRIMARY KEY NOT NULL, subtag TEXT);" );

		work.commit();

	}
	catch ( ... )
	{
		std::cerr << "Error creating tables" << std::endl;
	}
}

std::string IDHANDatabase::getSHA256( uint64_t hashID )
{
	pqxx::work work( conn );

	pqxx::result res = work.exec( "SELECT hash_sha256 FROM hashes WHERE hash_id = " + std::to_string( hashID ));

	work.commit();

	if ( res.size() == 0 )
	{
		return std::string();
	}

	return res[0][0].as<std::string>();
}

uint64_t IDHANDatabase::insertHash( std::string hashSHA256, std::string hashMD5, std::string hashSHA1 )
{
	try
	{
		pqxx::work work( conn );

		work.exec( "INSERT INTO hashes (hash_sha256, hash_md5, hash_sha1) VALUES ('" + hashSHA256 + "', '" + hashMD5 +
				"', '" + hashSHA1 + "');" );
		work.commit();
	}
	catch ( pqxx::unique_violation const& e )
	{
		//Catch unique_violation and silence it
	}


	pqxx::work work2( conn );

	//Get the ID of the inserted hash
	pqxx::result res = work2.exec( "SELECT hash_id FROM hashes WHERE hash_sha256 = '" + hashSHA256 + "';" );

	work2.commit();

	auto hashID = res[0][0].as<uint64_t>();

	if ( hashID == 0 )
	{
		throw std::runtime_error( "Could not get hash ID" );
	}


	return res[0][0].as<uint64_t>();
}

std::filesystem::path IDHANDatabase::getFile( uint64_t hashID )
{
	//Process the hash into the filepath we want (first two characters of the hash)


	std::vector<uint8_t> hash;

	std::string hashStr = getSHA256( hashID );
	hash.resize( hashStr.size());
	memcpy( hash.data(), hashStr.c_str(), hashStr.size());


	std::vector<char> hashStart( hash.begin(), hash.begin() + 1 );

	//convert hashStart to hex in a string
	std::string hashStartStr = std::string( hashStart.begin(), hashStart.end());

	std::stringstream ss;
	for ( const auto& chr: hashStartStr )
	{
		ss << std::hex << static_cast<unsigned int>(chr);
	}

	ss << '/';

	//Add the hash to the filepath
	for ( const auto& chr: hashStr )
	{
		ss << std::hex << static_cast<unsigned int>(chr);
	}


	IDHANConfig config;
	auto path = config.getValue<std::string>( "file_path" );

	std::filesystem::path filePath;

	if ( path.has_value())
	{
		filePath = path.value();

		filePath.append( ss.str());

		std::cout << "Filepath: " << filePath << std::endl;
	}
	else
	{
		std::cout << "Could not find filepath" << std::endl;
		return std::filesystem::path();
	}

	return filePath;
}


/*
 * TAGS
 */

uint64_t IDHANDatabase::insertNamespace( std::string namespaceName )
{
	pqxx::work work( conn );

	work.exec( "INSERT INTO namespaces (namespace) VALUES ('" + namespaceName + "');" );

	work.commit();

	pqxx::work work2( conn );

	pqxx::result res = work2.exec( "SELECT namespace_id FROM namespaces WHERE namespace = '" + namespaceName + "';" );

	work2.commit();

	if ( res.size() == 0 )
	{
		return 0;
	}

	return res[0][0].as<uint64_t>();
}

uint64_t IDHANDatabase::getNamespaceID( std::string namespaceName )
{
	pqxx::work work( conn );

	pqxx::result res = work.exec( "SELECT namespace_id FROM namespaces WHERE namespace = '" + namespaceName + "';" );

	work.commit();

	if ( res.size() == 0 )
	{
		return insertNamespace( namespaceName );
	}

	return res[0][0].as<uint64_t>();
}

uint64_t IDHANDatabase::insertSubtag( std::string subtagName )
{
	pqxx::work work( conn );

	work.exec( "INSERT INTO subtags (subtag) VALUES ('" + subtagName + "');" );

	work.commit();

	pqxx::work work2( conn );

	pqxx::result res = work2.exec( "SELECT subtag_id FROM subtags WHERE subtag = '" + subtagName + "';" );

	work2.commit();

	if ( res.size() == 0 )
	{
		return 0;
	}

	return res[0][0].as<uint64_t>();
}


uint64_t IDHANDatabase::getSubtagID( std::string subtagName )
{
	pqxx::work work( conn );

	pqxx::result res = work.exec( "SELECT subtag_id FROM subtags WHERE subtag = '" + subtagName + "';" );

	work.commit();

	if ( res.size() == 0 )
	{
		return insertSubtag( subtagName );
	}

	return res[0][0].as<uint64_t>();
}

uint64_t IDHANDatabase::insertTag( std::string namespaceStr, std::string subtagStr )
{
	uint64_t namespaceID = getNamespaceID( namespaceStr );
	uint64_t subtagID = getSubtagID( subtagStr );

	pqxx::work work( conn );

	work.exec( "INSERT INTO tags (subtag_id, namespace_id) VALUES (" + std::to_string( subtagID ) + ", " +
			std::to_string( namespaceID ) + ");" );

	work.commit();

	pqxx::work work2( conn );

	pqxx::result res = work2.exec(
			"SELECT tag_id FROM tags WHERE subtag_id = " + std::to_string( subtagID ) + " AND namespace_id = " +
					std::to_string( namespaceID ) + ";" );

	work2.commit();

	if ( res.size() == 0 )
	{
		return 0;
	}

	return res[0][0].as<uint64_t>();
}

uint64_t IDHANDatabase::getTagID( std::string namespaceStr, std::string subtagStr )
{
	uint64_t namespaceID = getNamespaceID( namespaceStr );
	uint64_t subtagID = getSubtagID( subtagStr );

	pqxx::work work( conn );

	pqxx::result res = work.exec(
			"SELECT tag_id FROM tags WHERE subtag_id = " + std::to_string( subtagID ) + " AND namespace_id = " +
					std::to_string( namespaceID ) + ";" );

	work.commit();

	if ( res.size() == 0 )
	{
		return insertTag( namespaceStr, subtagStr );
	}

	return res[0][0].as<uint64_t>();
}

void IDHANDatabase::addTagToHash( uint64_t hashID, uint64_t groupID, std::string namespaceStr, std::string subtagStr )
{
	try
	{


		const uint64_t tagID = getTagID( namespaceStr, subtagStr );

		pqxx::work work( conn );

		work.exec( "INSERT INTO mappings (hash_id, group_id, tag_id) VALUES (" + std::to_string( hashID ) + ", " +
				std::to_string( groupID ) + ", " + std::to_string( tagID ) + ");" );

		work.commit();

		return;
	}
	catch ( pqxx::unique_violation const& e )
	{
		return;
	}
}

void IDHANDatabase::addFile( std::filesystem::path filePath )
{
	//Read the file and determine MimeType



}