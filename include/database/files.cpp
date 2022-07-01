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


uint64_t addFile( const Hash32& sha256, Database db )
{
	spdlog::debug( "addFile()" );
	ZoneScoped;
	pqxx::work& work { db.getWork() };

	pqxx::params values;

	constexpr pqxx::zview query = "INSERT INTO files (sha256) VALUES ($1) RETURNING hash_id";

	values.append( sha256.getView() );

	const pqxx::result res = work.exec_params( query, values );
	work.commit();
	return res[ 0 ][ "hash_id" ].as< uint64_t >();
}


uint64_t getFileID( const Hash32& sha256, const bool add, Database db )
{
	ZoneScoped;

	pqxx::work& work { db.getWork() };

	pqxx::params values;

	constexpr pqxx::zview query = "SELECT hash_id FROM files WHERE sha256 = $1";
	values.append( sha256.getView() );

	const pqxx::result res = work.exec_params( query, values );
	work.commit();


	if ( res.empty() )
	{
		if ( add )
		{ return addFile( sha256, db ); }
		else
		{
			QString hash_str = sha256.getQByteArray().toHex();
			throw EmptyReturnException(
				"No file with hash " + hash_str.toStdString() + " found."
			);
		}
	}

	return res[ 0 ][ 0 ].as< uint64_t >();
}


Hash32 getHash( const uint64_t hash_id, Database db )
{
	ZoneScoped;
	pqxx::work& work { db.getWork() };

	const pqxx::result res = work.exec(
		"SELECT sha256 FROM files WHERE hash_id = " + std::to_string( hash_id )
	);

	work.commit();

	if ( res.empty() )
	{
		throw EmptyReturnException(
			"No file with hash_id " + std::to_string( hash_id ) + " found."
		);
	}

	// From db -> \xabcdef12354587979 -> abcdef12354587979 -> RAW BYES
	auto hexstring_to_qbytearray = []( std::string_view sv ) -> QByteArray
	{
		return QByteArray::fromHex(
			QString::fromUtf8(
				sv.data() + 2, static_cast<qsizetype>( sv.size() - 2 )
			).toUtf8()
		);
	};

	return Hash32 { hexstring_to_qbytearray( res[ 0 ][ 0 ].as< std::string_view >() ) };
}


// Filepath from hash_id
std::filesystem::path getThubmnailpath( const uint64_t hash_id, Database db )
{
	ZoneScoped;
	const Hash32 hash = getHash( hash_id, db );
	return getThumbnailpath( hash );
}


std::filesystem::path getFilepath( const uint64_t hash_id, Database db )
{
	ZoneScoped;
	const Hash32 hash = getHash( hash_id, db );

	auto path = getFilepath( hash );
	path += getFileExtention( hash_id, db );

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
std::filesystem::path getThumbnailpath( const Hash32& sha256 )
{
	ZoneScoped;
	return internal::generate_path_to_file( "paths/thumbnail_path", "./db/thumbnails", 't', sha256 );
}


std::filesystem::path getFilepath( const Hash32& sha256 )
{
	ZoneScoped;
	return internal::generate_path_to_file( "paths/file_path", "./db/file_paths", 'f', sha256 );
}
