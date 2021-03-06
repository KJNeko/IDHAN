//
// Created by kj16609 on 6/28/22.
//

#include "database.hpp"
#include "databaseExceptions.hpp"
#include "files.hpp"
#include "metadata.hpp"

#include <QByteArray>
#include <QSettings>
#include <QString>

#include "TracyBox.hpp"
#include <fgl/types/ctypes.hpp>
#include <QCache>


uint64_t addFile( const Hash32& sha256 )
{
	ZoneScoped;
	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query = "INSERT INTO files (sha256) VALUES ($1) RETURNING hash_id";

	const pqxx::result res = work.exec_params( query, sha256.getView() );

	if ( res.empty() )
	{
		spdlog::error( "Failed to insert file into database" );
	}

	work.commit();

	return res[ 0 ][ "hash_id" ].as< uint64_t >();
}


uint64_t getFileID( const Hash32& sha256, const bool add )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query = "SELECT hash_id FROM files WHERE sha256 = $1";

	const pqxx::result res = work.exec_params( query, sha256.getView() );

	if ( res.empty() )
	{
		if ( add )
		{
			return addFile( sha256 );
		}
		else
		{
			return 0;
		}
	}


	return res[ 0 ][ 0 ].as< uint64_t >();
}


Hash32 getHash( const uint64_t hash_id )
{
	ZoneScoped;
	constexpr size_t size_unit { sizeof( uint64_t ) + sizeof( std::array< uint8_t, 32 > ) };
	constexpr size_t size_max { 1024 * 1024 * 256 }; //256MB
	constexpr size_t size_num { size_max / size_unit };

	static QCache< uint64_t, std::shared_ptr< Hash32>> cache( size_num );

	if ( cache.contains( hash_id ) )
	{
		return **cache.object( hash_id );
	}

	Connection conn;
	pqxx::work work { conn() };

	constexpr pqxx::zview query { "SELECT sha256 FROM files WHERE hash_id = $1" };

	const pqxx::result res { work.exec_params( query, hash_id ) };

	if ( res.empty() )
	{
		spdlog::warn( "No file with ID {}", hash_id );
		throw IDHANError(
			ErrorNo::DATABASE_DATA_NOT_FOUND, "No file with ID " + std::to_string( hash_id ) + " found."
		);
	}

	// From db -> \xabcdef12354587979 -> abcdef12354587979 -> RAW BYES
	auto hexstring_to_qbytearray = []( std::string sv ) -> QByteArray
	{
		return QByteArray::fromHex(
			QString::fromUtf8(
				sv.data() + 2, static_cast<qsizetype>( sv.size() - 2 )
			).toUtf8()
		);
	};

	const Hash32 hash { hexstring_to_qbytearray( res[ 0 ][ 0 ].as< std::string >() ) };

	//Create a copy of the data as a shared pointer
	std::shared_ptr< Hash32 > hash_ptr = std::make_shared< Hash32 >( hash );

	//Add the hash to the cache
	cache.insert( hash_id, new std::shared_ptr< Hash32 >( hash_ptr ) );

	work.commit();

	return Hash32 { hexstring_to_qbytearray( res[ 0 ][ 0 ].as< std::string >() ) };
}


// Filepath from hash_id
std::filesystem::path getThumbnailpath( const uint64_t hash_id )
{
	ZoneScoped;

	Connection conn;
	pqxx::work work { conn() };

	const Hash32 hash { getHash( hash_id ) };

	work.commit();

	return getThumbnailpathFromHash( hash );
}


std::filesystem::path getFilepath( const uint64_t hash_id )
{
	ZoneScoped;
	const Hash32 hash = getHash( hash_id );

	auto path = getFilepathFromHash( hash );

	const auto ext = getFileExtention( hash_id );

	if ( ext.size() > 0 )
	{
		path += ".";
		path += ext;
	}

	return path;
}


namespace internal
{
	// This returns the directory
	[[nodiscard]] inline std::filesystem::path path_from_settings(
		const fgl::cstring setting, const fgl::cstring default_path )
	{
		ZoneScoped;
		QSettings s;
		std::string p { s.value( setting, "invalid" ).toString().toStdString() };
		if ( p == "invalid" )
		{
			spdlog::warn(
				"{} not set. Setting to default location of: {}", setting, default_path
			);
			p = default_path;
		}
		return p;
	}


// example: subfolder_and_filename(Hash32{"1234"}, 't') -> "t12/1234"
	[[nodiscard]] inline std::string subfolder_and_filename( const char symbol, const Hash32& sha256 )
	{
		ZoneScoped;
		// Use QByteArray to be lazy and convert to hex
		const std::string hash_str = sha256.getQByteArray().toHex().toStdString();

		constexpr auto sep { std::filesystem::path::preferred_separator };

		std::string path;
		path.reserve( sizeof( symbol ) + 2 + sizeof( sep ) + hash_str.size() );
		path += symbol;
		path += hash_str[ 0 ]; // first char of hash
		path += hash_str[ 1 ]; // second
		path += sep;
		path += hash_str;

		return path;
	}


	[[nodiscard]] inline std::filesystem::path generate_path_to_file(
		const fgl::cstring setting, const fgl::cstring default_path, const char symbol, const Hash32& sha256 )
	{
		ZoneScoped;
		return path_from_settings( setting, default_path ) / subfolder_and_filename( symbol, sha256 );
	}
} // namespace internal

// Filepath from only hash
std::filesystem::path getThumbnailpathFromHash( const Hash32& sha256 )
{
	ZoneScoped;
	return internal::generate_path_to_file( "paths/thumbnail_path", "./db/thumbnails", 't', sha256 ).string() + ".jpg";
}


std::filesystem::path getFilepathFromHash( const Hash32& sha256 )
{
	ZoneScoped;
	return internal::generate_path_to_file( "paths/file_path", "./db/file_paths", 'f', sha256 );
}
