//
// Created by kj16609 on 7/20/22.
//

#include "importer.hpp"

#include "DatabaseModule/utility/databaseExceptions.hpp"


#include "DatabaseModule/files/metadata.hpp"
#include "DatabaseModule/tags/mappings.hpp"
#include "DatabaseModule/tags/tags.hpp"

#include <fstream>

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QSettings>
#include <QImage>


#ifdef _WIN32
// TODO find an alternative to unistd fsync for windows
#elif __linux__


#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>


#else
#ifndef FGL_ACKNOWLEDGE_INCOMPATIBLE_OS
#error "Unsupported platform for IDHAN (Requirement: fsync implementation or similar) - please define FGL_ACKNOWLEDGE_INCOMPATIBLE_OS to fallback to std::ostream"
#endif
#endif


std::vector< std::byte > readFile( const std::filesystem::path& path )
{
	ZoneScoped;

	if ( std::ifstream ifs( path ); ifs )
	{
		const size_t file_size { std::filesystem::file_size( path ) };
		std::vector< std::byte > data;
		data.resize( file_size );
		ifs.read( reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(file_size) );
		return data;
	}

	throw IDHANError( ErrorNo::FILE_NOT_FOUND, path.string() );
}


Hash32 getSHA256Hash( const std::vector< std::byte >& data )
{
	ZoneScoped;

	const QByteArrayView bytes_view(
		data.data(), static_cast<qsizetype>( data.size())
	);

	const Hash32 sha256 { QCryptographicHash::hash( bytes_view, QCryptographicHash::Sha256 ) };

	return sha256;
}


QMimeType determineMime( const std::vector< std::byte >& data )
{
	ZoneScoped;

	QMimeDatabase mime_db;

	//Stupid fucking dance of data because Qt is retarded
	const QByteArray stupid_array { reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()) };

	const auto mime_type = mime_db.mimeTypeForData( stupid_array );

	return mime_type;
}


#ifndef NOSECURE_WRITE


int startSecureWrite( const std::filesystem::path& path, const std::vector< std::byte >& data )
{
	ZoneScoped;

	std::filesystem::create_directories( path.parent_path() );

	#ifdef __linux__
	// Notes
	// O_LARGEFILE can be used if the file would exceed the
	// off_t (32bit) limit in size

	// Active
	// O_CREAT creats the file if it does not exist
	// O_WRONLY opens the file for writing only
	// O_SYNC ensures that the data is written to disk before
	// returning from the write call

	int val { ::open( path.c_str(), O_WRONLY | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR ) };

	if ( val == -1 )
	{
		IDHANError err { ErrorNo::FSYNC_FILE_OPENEN_FAILED,
			"Failed to open file error code: " + std::to_string( val ) };

	}

	::write( val, data.data(), data.size() );

	return val;
	#else
		#error "startSecureWrite not implemented for this OS"
	#endif
}


void finalizeSecureWrite( [[maybe_unused]] const int id )
{
	ZoneScoped;

	#ifdef __linux__
	::fsync( id );
	::close( id );
	#else
		#error "finalizeSecureWrite not implemented for this OS"
	#endif
}


#endif


std::vector< std::pair< std::string, std::string>> parseSidecarTags( const std::filesystem::path& path )
{
	std::vector< std::pair< std::string, std::string>> tags_vec;
	//Check to see if a file exists with the tags

	if ( std::ifstream ifs( path.string() + ".txt" ); ifs )
	{
		constexpr uint64_t max_count { 1024 };
		uint64_t counter { 0 };
		while ( !ifs.eof() && !ifs.bad() && !ifs.fail() && ifs.good() )
		{
			++counter;
			if ( counter > 1024 )
			{
				spdlog::warn( "While importing tags for {} we went over the counter of {}", path.string(), max_count );
				break;
			}

			std::string name;

			std::getline( ifs, name );

			std::vector< std::string > split_strings;

			constexpr std::string_view delim { ":" };

			const std::string_view sv { name };

			for ( const auto& word: std::views::split( sv, delim ) )
			{
				if ( split_strings.empty() )
				{
					//Push back just the group
					split_strings.push_back( std::string( word.data(), word.size() ) );
				}
				else
				{
					//Push back the rest of the tag
					split_strings.push_back( std::string( word.data(), word.size() ) );
				}
			}


			if ( split_strings.size() >= 2 )
			{
				const size_t group_size { split_strings[ 0 ].size() };
				//Combine all but the first string
				std::string combined_string;

				for ( size_t i = group_size + 1; i < name.size(); ++i )
				{
					combined_string += name[ i ];
				}

				tags_vec.push_back( std::make_pair( split_strings[ 0 ], combined_string ) );
				//addMapping( hash_id, split_strings[ 0 ], combined_string );
			}
			else if ( split_strings.size() == 1 )
			{
				tags_vec.push_back( std::make_pair( "", split_strings[ 0 ] ) );
				//addMapping( hash_id, "", split_strings[ 0 ] );
			}
			else
			{
				spdlog::error( "Invalid tag format {}", name );
			}

		}
	}


	return tags_vec;
}


void generateThumbnail( const std::vector< std::byte >& data, const QMimeType mime_type, const Hash32& sha256 )
{
	ZoneScoped;

	QSettings s( QSettings::IniFormat, QSettings::UserScope, "Future Gadget Labs", "IDHAN" );

	const auto x_res = s.value( "thumbnails/x_res", 120 ).toInt();
	const auto y_res = s.value( "thumbnails/y_res", 120 ).toInt();


	if ( mime_type.name().contains( "image/" ) )
	{
		QImage raw;
		raw.loadFromData( reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()) );

		//Resize image
		const auto resized_qimage = raw.scaled( x_res, y_res, Qt::KeepAspectRatio, Qt::SmoothTransformation );

		auto thumbnail_path = files::getThumbnailpathFromHash( sha256 );

		//Ensure the parent directory is created
		std::filesystem::create_directories( thumbnail_path.parent_path() );

		resized_qimage.save( QString::fromStdString( thumbnail_path.string() ) );
	}
}


ImportResultOutput importToDB( const std::filesystem::path& path )
{
	ZoneScoped;

	const std::vector< std::byte > file_data { readFile( path ) };

	const Hash32 sha256 { getSHA256Hash( file_data ) };

	const auto mime_type { determineMime( file_data ) };

	const std::filesystem::path out_filepath { [ &sha256, &mime_type ]()
	{
		std::filesystem::path filepath { files::getFilepathFromHash( sha256 ).string() };
		const auto ext = mime_type.preferredSuffix().toStdString();
		if ( ext.size() > 0 )
		{
			filepath += ".";
			filepath += ext;
		}
		return filepath;
	}() };

	int file_handle_id { 0 };

	if ( !std::filesystem::exists( out_filepath ) )
	{
		//start to write the file
		file_handle_id = startSecureWrite( out_filepath, file_data );
	}

	const auto tags { parseSidecarTags( path ) };

	uint64_t hash_id { 0 };

	bool exists { false };

	if ( files::async::getFileID( sha256 ).result() != 0 )
	{
		finalizeSecureWrite( file_handle_id );
		return { ImportResult::ALREADY_IMPORTED, hash_id };
	}

	auto hash_id_future { files::async::addFile( sha256 ) };

	hash_id = hash_id_future.result();

	auto mime_future = metadata::async::populateMime( hash_id_future.result(), mime_type.name().toStdString() );

	std::vector< QFuture< void>> tag_futures;

	for ( const auto& tag: tags )
	{
		auto tag_future = tags::async::getTagID( tag.first, tag.second );

		if ( tag_future.result() == 0 )
		{
			auto create_tag_future = tags::async::createTag( tag.first, tag.second );
			tag_futures.push_back( mappings::async::addMapping( hash_id, create_tag_future.result() ) );
		}
		else
		{
			tag_futures.push_back( mappings::async::addMapping( hash_id, tag_future.result() ) );
		}
	}

	for ( size_t i = 0; i < tag_futures.size(); ++i )
	{
		tag_futures[ i ].waitForFinished();
	}

	mime_future.waitForFinished();


	//Create the thumbnail if it doesn't exist
	generateThumbnail( file_data, mime_type, sha256 );


	finalizeSecureWrite( file_handle_id );
	return { ImportResult::SUCCESS, hash_id };
}
