#include "PTRDownloader.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkRequest>
#include <QUrlQuery>

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <stdexcept>

#include "PTRConstants.hpp"
#include "PTRFileParser.hpp"

namespace idhan::hydrus::ptr
{

PTRDownloader::PTRDownloader(
	const std::filesystem::path& output_dir,
	QString host,
	quint16 port,
	QString access_key,
	QObject* parent ) :
  QObject( parent ),
  m_output_dir( output_dir ),
  m_host( std::move( host ) ),
  m_port( port ),
  m_access_key( std::move( access_key ) )
{}

PTRDownloader::~PTRDownloader() = default;

QUrl PTRDownloader::makeUrl( const QString& subpath ) const
{
	QUrl url;
	url.setScheme( "https" );
	url.setHost( m_host );
	url.setPort( m_port );
	url.setPath( QString( "/%1" ).arg( subpath ) );
	return url;
}

void PTRDownloader::loadMetadata()
{
	const auto meta_path = m_output_dir / "ptr_metadata.json";
	std::ifstream file( meta_path );
	if ( !file )
	{
		m_last_update_index = -1;
		m_downloaded_hashes.clear();
		return;
	}

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	if ( !Json::parseFromStream( builder, file, &root, &errors ) )
	{
		spdlog::warn( "Failed to parse existing ptr_metadata.json: {}", errors );
		m_last_update_index = -1;
		m_downloaded_hashes.clear();
		return;
	}

	m_last_update_index = root.get( "last_update_index", -1 ).asInt();
	m_metadata.next_update_due = root.get( "next_update_due", 0 ).asInt64();

	const auto& updates_arr = root[ "updates" ];
	if ( updates_arr.isArray() )
	{
		for ( const auto& u : updates_arr )
		{
			MetadataUpdateEntry entry;
			entry.index = u[ "index" ].asInt();
			const auto& hashes = u[ "hashes" ];
			if ( hashes.isArray() )
			{
				for ( const auto& h : hashes ) entry.hashes.push_back( h.asString() );
			}
			entry.begin = u[ "begin" ].asInt64();
			entry.end = u[ "end" ].asInt64();
			m_metadata.updates.push_back( std::move( entry ) );
		}
	}

	const auto& dl_hashes = root[ "downloaded_hashes" ];
	if ( dl_hashes.isArray() )
	{
		for ( const auto& h : dl_hashes ) m_downloaded_hashes.insert( h.asString() );
	}

	spdlog::info(
		"Loaded metadata: last_update_index={}, updates={}, downloaded_hashes={}",
		m_last_update_index,
		m_metadata.updates.size(),
		m_downloaded_hashes.size() );
}

void PTRDownloader::saveMetadata()
{
	Json::Value root;
	root[ "schema_version" ] = 1;
	root[ "last_sync_time" ] = static_cast< Json::Int64 >(
		std::chrono::duration_cast< std::chrono::seconds >( std::chrono::system_clock::now().time_since_epoch() )
			.count() );
	root[ "last_update_index" ] = m_last_update_index;
	root[ "next_update_due" ] = static_cast< Json::Int64 >( m_metadata.next_update_due );

	Json::Value updates_arr( Json::arrayValue );
	for ( const auto& u : m_metadata.updates )
	{
		Json::Value entry;
		entry[ "index" ] = u.index;

		Json::Value hashes_arr( Json::arrayValue );
		for ( const auto& h : u.hashes ) hashes_arr.append( h );
		entry[ "hashes" ] = hashes_arr;

		entry[ "begin" ] = static_cast< Json::Int64 >( u.begin );
		entry[ "end" ] = static_cast< Json::Int64 >( u.end );
		updates_arr.append( entry );
	}
	root[ "updates" ] = updates_arr;

	Json::Value dl_arr( Json::arrayValue );
	for ( const auto& h : m_downloaded_hashes ) dl_arr.append( h );
	root[ "downloaded_hashes" ] = dl_arr;

	Json::StreamWriterBuilder writer_builder;
	writer_builder[ "indentation" ] = "  ";
	const auto json_str = Json::writeString( writer_builder, root );

	const auto meta_path = m_output_dir / "ptr_metadata.json";
	std::ofstream file( meta_path );
	if ( !file ) throw std::runtime_error( "Failed to write ptr_metadata.json" );
	file << json_str;
}

void PTRDownloader::startSync()
{
	if ( m_state != State::Idle )
	{
		spdlog::warn( "startSync called but state is not Idle (state={})", static_cast< int >( m_state ) );
		return;
	}
	m_cancelled = false;

	spdlog::info( "Starting PTR sync to directory: {}", m_output_dir.string() );

	if ( !std::filesystem::exists( m_output_dir ) )
	{
		spdlog::debug( "Creating output directory: {}", m_output_dir.string() );
		std::filesystem::create_directories( m_output_dir );
	}

	loadMetadata();

	// Try loading cached metadata from disk first
	if ( loadCachedMetadata() )
	{
		spdlog::info( "Using cached metadata (less than {}h old)", METADATA_MAX_AGE.count() );
		buildDownloadQueue();
		return;
	}

	spdlog::info( "Cached metadata stale or missing, fetching fresh" );
	downloadMetadata();
}

void PTRDownloader::cancel()
{
	m_cancelled = true;
	emit progress( "Cancelling..." );
}

bool PTRDownloader::loadCachedMetadata()
{
	const auto meta_path = m_output_dir / METADATA_FILENAME;
	std::error_code ec;
	const auto mtime = std::filesystem::last_write_time( meta_path, ec );
	if ( ec )
	{
		spdlog::debug( "No cached metadata file found at {}", meta_path.string() );
		return false;
	}

	const auto age = std::filesystem::file_time_type::clock::now() - mtime;
	if ( age >= METADATA_MAX_AGE )
	{
		spdlog::debug(
			"Cached metadata is too old (age={}h)", std::chrono::duration_cast< std::chrono::hours >( age ).count() );
		return false;
	}

	spdlog::debug(
		"Cached metadata age={}h, loading", std::chrono::duration_cast< std::chrono::hours >( age ).count() );

	try
	{
		const auto data = readFile( meta_path );
		const auto meta = parseMetadataBytes( QByteArray( data.data(), static_cast< int >( data.size() ) ) );

		this->m_metadata = meta;

		for ( const auto& u : this->m_metadata.updates )
		{
			if ( u.index > m_last_update_index ) m_last_update_index = u.index;
		}

		return true;
	}
	catch ( const std::exception& e )
	{
		spdlog::warn( "Failed to parse cached metadata: {}", e.what() );
		return false;
	}
}

void PTRDownloader::buildDownloadQueue()
{
	m_pending_downloads.clear();

	if ( !m_metadata.updates.empty() )
	{
		std::set< int > indices;
		for ( const auto& u : m_metadata.updates ) indices.insert( u.index );

		if ( !indices.empty() )
		{
			const int max_index = *indices.rbegin();
			std::vector< int > missing;
			for ( int i = 0; i <= max_index; ++i )
			{
				if ( indices.find( i ) == indices.end() ) missing.push_back( i );
			}
			if ( !missing.empty() )
			{
				spdlog::warn(
					"Missing update indices in metadata: {} entries missing from 0 to {}", missing.size(), max_index );
				for ( const auto& idx : missing ) spdlog::warn( "  Missing update index: {}", idx );
			}
			else
			{
				spdlog::debug( "Update sequence complete: indices 0 to {} ({} updates)", max_index, indices.size() );
			}
		}
	}

	for ( const auto& u : m_metadata.updates )
	{
		int files_present = 0;
		int files_missing = 0;

		for ( const auto& h : u.hashes )
		{
			const auto file_path = m_output_dir / ( h + ".ptrupdate" );

			if ( std::filesystem::exists( file_path ) )
			{
				m_downloaded_hashes.insert( h );
				++files_present;
			}
			else
			{
				if ( m_downloaded_hashes.count( h ) )
				{
					spdlog::warn( "  {} marked as downloaded but file missing, will re-download", h );
					m_downloaded_hashes.erase( h );
				}

				++files_missing;
				m_pending_downloads.push_back( h );
			}

			QCoreApplication::processEvents();
			if ( m_cancelled )
			{
				m_state = State::Idle;
				emit finished( false, "Cancelled" );
				return;
			}
		}

		if ( files_missing > 0 )
		{
			spdlog::info(
				"Update {}: {}/{} files present, {} to download",
				u.index,
				files_present,
				u.hashes.size(),
				files_missing );
		}
		else
		{
			spdlog::debug( "Update {}: all {} files present", u.index, files_present );
		}
	}

	m_total_downloads = static_cast< int >( m_pending_downloads.size() );
	m_completed_downloads = 0;
	m_current_download_index = 0;

	spdlog::info(
		"Build download queue: {} pending files, {} total updates in metadata",
		m_total_downloads,
		m_metadata.updates.size() );
	for ( const auto& u : m_metadata.updates )
		spdlog::trace( "  update index={}: {} hashes", u.index, u.hashes.size() );

	if ( m_total_downloads == 0 )
	{
		spdlog::info( "No pending downloads, metadata is up to date" );
		m_state = State::Done;
		saveMetadata();
		emit finished( true, "Already up to date. No new files." );
		return;
	}

	emit metadataReceived( static_cast< int >( m_metadata.updates.size() ), m_total_downloads );
	downloadNextUpdate();
}

void PTRDownloader::downloadMetadata()
{
	m_state = State::DownloadingMetadata;

	const int since = 0;

	auto url = makeUrl( "metadata" );
	QUrlQuery query;
	query.addQueryItem( "since", QString::number( since ) );
	url.setQuery( query );

	spdlog::info( "Downloading metadata from {}:{}", m_host.toStdString(), m_port );
	spdlog::debug( "Metadata URL: {}", url.toString().toStdString() );
	spdlog::debug( "Requesting metadata from index 0 to ensure completeness" );

	emit progress( "Downloading metadata (from index 0)..." );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", "IDHAN PTR Importer/0.1" );
	request.setRawHeader( "Hydrus-Key", m_access_key.toUtf8() );
	request.setTransferTimeout( 30000 );

	auto* reply = m_network.get( request );

	connect(
		reply, &QNetworkReply::sslErrors, reply, [ reply ]( const QList< QSslError >& ) { reply->ignoreSslErrors(); } );

	connect(
		reply,
		&QNetworkReply::errorOccurred,
		this,
		[ this, reply ]( QNetworkReply::NetworkError code )
		{
			spdlog::error(
				"Metadata network error: code={}, msg={}, http_status={}",
				static_cast< int >( code ),
				reply->errorString().toStdString(),
				reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() );
		} );

	connect( reply, &QNetworkReply::finished, this, [ this, reply ]() { onMetadataReply( reply ); } );
}

bool PTRDownloader::updateExists( const std::string& value )
{
	const auto output_dir { m_output_dir };
	const auto expected_path { output_dir / ( value + ".ptrupdate" ) };
	const auto absolute { std::filesystem::absolute( expected_path ) };
	return std::filesystem::exists( absolute );
}

void PTRDownloader::onMetadataReply( QNetworkReply* reply )
{
	const auto http_status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
	const auto error_str = reply->errorString().toStdString();
	const auto url_str = reply->url().toString().toStdString();

	spdlog::debug( "Metadata reply received: http_status={}, error={}, url={}", http_status, error_str, url_str );

	reply->deleteLater();

	if ( m_cancelled )
	{
		spdlog::warn( "Metadata download cancelled" );
		m_state = State::Idle;
		emit finished( false, "Cancelled" );
		return;
	}

	if ( reply->error() != QNetworkReply::NoError )
	{
		spdlog::error(
			"Metadata download failed: code={}, http_status={}, msg={}, url={}",
			static_cast< int >( reply->error() ),
			http_status,
			error_str,
			reply->url().toString().toStdString() );

		spdlog::warn(
			"Check that the PTR server is reachable at {}:{}{}",
			m_host.toStdString(),
			m_port,
			( m_port == 45871 ) ? "" : " (default PTR port is 45871)" );

		// Log raw response if available
		const auto response_data = reply->readAll();
		if ( !response_data.isEmpty() )
		{
			spdlog::debug(
				"Metadata error response body ({} bytes): {}",
				response_data.size(),
				response_data.left( 500 ).toStdString() );
		}
		else
		{
			spdlog::debug( "Metadata error: no response body (connection likely closed)" );
		}

		m_state = State::Error;
		emit finished(
			false, QString( "Metadata download failed (HTTP %1): %2" ).arg( http_status ).arg( error_str.c_str() ) );
		return;
	}

	const auto data = reply->readAll();
	spdlog::debug( "Metadata response body size: {} bytes", data.size() );

	try
	{
		// Save raw metadata to disk before processing
		const auto meta_path = m_output_dir / METADATA_FILENAME;
		{
			std::ofstream file( meta_path, std::ios::binary );
			if ( file )
			{
				file.write( data.data(), static_cast< std::streamsize >( data.size() ) );
				spdlog::debug( "Saved raw metadata to {}", meta_path.string() );
			}
			else
			{
				spdlog::warn( "Failed to save raw metadata to {}", meta_path.string() );
			}
		}

		processMetadataResponse( data );
	}
	catch ( const std::exception& e )
	{
		spdlog::error( "Metadata parse failed: {}", e.what() );
		spdlog::debug( "Raw response body (first 1000 bytes): {}", data.left( 1000 ).toStdString() );
		m_state = State::Error;
		emit finished( false, QString( "Metadata parse failed: %1" ).arg( e.what() ) );
		return;
	}

	buildDownloadQueue();
}

void PTRDownloader::downloadNextUpdate()
{
	spdlog::debug(
		"downloadNextUpdate: index={}/{} completed={}",
		m_current_download_index,
		m_pending_downloads.size(),
		m_completed_downloads );

	if ( m_cancelled )
	{
		spdlog::warn( "Download cancelled during update fetch" );
		m_state = State::Idle;
		emit finished( false, "Cancelled" );
		return;
	}

	if ( m_current_download_index >= static_cast< int >( m_pending_downloads.size() ) )
	{
		spdlog::info( "All {} updates downloaded successfully", m_completed_downloads );
		m_state = State::Done;
		saveMetadata();
		emit finished( true, QString( "Downloaded %1 files." ).arg( QLocale::system().toString( m_completed_downloads ) ) );
		return;
	}

	const auto& hash_hex = m_pending_downloads[ m_current_download_index ];

	if ( updateExists( hash_hex ) )
	{
		spdlog::info( "Skipping update {} because it already exists", hash_hex );
		return;
	}

	spdlog::info( "Downloading update {}/{}: {}", m_current_download_index + 1, m_total_downloads, hash_hex );

	emit fileDownloading( QString::fromStdString( hash_hex ), m_current_download_index + 1, m_total_downloads );
	emit progress( QString( "Downloading %1/%2: %3" )
	                   .arg( QLocale::system().toString( m_current_download_index + 1 ) )
	                   .arg( QLocale::system().toString( m_total_downloads ) )
	                   .arg( QString::fromStdString( hash_hex ) ) );

	auto url = makeUrl( "update" );
	QUrlQuery query;
	query.addQueryItem( "update_hash", QString::fromStdString( hash_hex ) );
	url.setQuery( query );

	QNetworkRequest request( url );
	request.setRawHeader( "User-Agent", "IDHAN PTR Importer/0.1" );
	request.setRawHeader( "Hydrus-Key", m_access_key.toUtf8() );
	request.setTransferTimeout( 60000 );

	auto* reply = m_network.get( request );

	connect(
		reply, &QNetworkReply::sslErrors, reply, [ reply ]( const QList< QSslError >& ) { reply->ignoreSslErrors(); } );

	connect(
		reply,
		&QNetworkReply::errorOccurred,
		this,
		[ this, reply, hash_hex ]( QNetworkReply::NetworkError code )
		{
			spdlog::error(
				"Update download error for {}: code={}, msg={}, http_status={}",
				hash_hex,
				static_cast< int >( code ),
				reply->errorString().toStdString(),
				reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() );
		} );

	connect(
		reply,
		&QNetworkReply::finished,
		this,
		[ this, reply, hash_hex ]() { onUpdateReply( reply, QString::fromStdString( hash_hex ) ); } );
}

void PTRDownloader::onUpdateReply( QNetworkReply* reply, QString hash_hex )
{
	const auto http_status = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
	const auto error_str = reply->errorString().toStdString();
	const auto error_code = reply->error();

	spdlog::trace(
		"Update reply for {}: http_status={}, error_code={}, msg={}",
		hash_hex.toStdString(),
		http_status,
		static_cast< int >( error_code ),
		error_str );

	reply->deleteLater();

	if ( m_cancelled )
	{
		spdlog::warn( "Update download cancelled for {}", hash_hex.toStdString() );
		m_state = State::Idle;
		emit finished( false, "Cancelled" );
		return;
	}

	if ( error_code != QNetworkReply::NoError )
	{
		spdlog::warn(
			"Failed to download update {}: error_code={}, http_status={}, msg={}",
			hash_hex.toStdString(),
			static_cast< int >( error_code ),
			http_status,
			error_str );

		// Log response body for debugging
		const auto response_data = reply->readAll();
		if ( !response_data.isEmpty() )
		{
			spdlog::debug(
				"Error response body for {} ({} bytes): {}",
				hash_hex.toStdString(),
				response_data.size(),
				response_data.left( 500 ).toStdString() );
		}
		else
		{
			spdlog::debug( "No response body for {} (connection likely closed)", hash_hex.toStdString() );
		}
	}
	else
	{
		const auto data = reply->readAll();
		spdlog::debug( "Received {} bytes for update {}", data.size(), hash_hex.toStdString() );

		if ( data.isEmpty() )
		{
			spdlog::warn( "Empty response body for update {} (HTTP status {})", hash_hex.toStdString(), http_status );
		}
		else
		{
			// Verify SHA-256 matches filename
			const auto actual_hash = QCryptographicHash::hash( data, QCryptographicHash::Sha256 ).toHex();
			const auto expected_hash = hash_hex.toLower();
			if ( actual_hash != expected_hash )
			{
				spdlog::error(
					"SHA-256 mismatch for {}: expected={}, actual={}",
					hash_hex.toStdString(),
					expected_hash.toStdString(),
					actual_hash.toStdString() );
			}
			else
			{
				spdlog::trace( "SHA-256 verified for {}", hash_hex.toStdString() );
				try
				{
					const auto file_path = m_output_dir / ( hash_hex.toStdString() + ".ptrupdate" );
					std::ofstream file( file_path, std::ios::binary );
					if ( !file )
					{
						spdlog::error( "Failed to open output file: {}", file_path.string() );
					}
					else
					{
						file.write( data.data(), static_cast< std::streamsize >( data.size() ) );
						if ( !file )
						{
							spdlog::error( "Failed to write {} bytes to {}", data.size(), file_path.string() );
						}
						else
						{
							m_downloaded_hashes.insert( hash_hex.toStdString() );
							++m_completed_downloads;

							spdlog::debug(
								"Saved update {} to {} ({} bytes)",
								hash_hex.toStdString(),
								file_path.string(),
								data.size() );

							emit fileDownloaded( hash_hex, m_completed_downloads, m_total_downloads );
						}
					}
				}
				catch ( const std::exception& e )
				{
					spdlog::error( "Failed to save update {}: {}", hash_hex.toStdString(), e.what() );
				}
			}
		}
	}

	++m_current_download_index;
	spdlog::info( "Completed {}/{}", m_completed_downloads, m_total_downloads );
	downloadNextUpdate();
}

MetadataUpdate PTRDownloader::parseMetadataBytes( const QByteArray& data )
{
	const std::vector< char > compressed( data.begin(), data.end() );
	auto root = decompressToJson( compressed );

	{
		Json::StreamWriterBuilder w;
		w[ "indentation" ] = "";
		spdlog::trace( "Decompressed JSON: {}", Json::writeString( w, root ) );
	}

	// Response is a HydrusDictionary (type 21) wrapping a MetadataUpdate under "metadata_slice"
	// Wire format: [21, 1, {"list_sim_sim": [["metadata_slice", [37, 1, metadata_data]], ...], ...}]
	if ( root.isArray() && root.size() >= 3 && root[ 0 ].asInt() == SERIALISABLE_TYPE_METADATA )
	{
		// Already a bare MetadataUpdate tuple, nothing to unwrap
	}
	else if ( root.isArray() && root.size() >= 3 && root[ 0 ].asInt() == HYDRUS_TYPE_DICTIONARY )
	{
		const auto& info = root[ 2 ];
		// Wire format: [21, version, [key1, val1, key2, val2, ...]] — flat alternating key-value array
		if ( info.isArray() )
		{
			// Format: [[type_k, key], [type_v, val], ...] — pairs of typed entries
			bool found = false;
			for ( const auto& entry : info )
			{
				if ( entry.isArray() && entry.size() >= 2 )
				{
					const auto& key_part = entry[ 0 ]; // [type_k, key]
					const auto& val_part = entry[ 1 ]; // [type_v, val]
					if ( key_part.isArray() && key_part.size() >= 2 && key_part[ 1 ].asString() == "metadata_slice" )
					{
						root = val_part[ 1 ];
						found = true;
						break;
					}
				}
			}
			if ( !found )
			{
				spdlog::error( "metadata_slice not found in HydrusDictionary" );
				throw std::runtime_error( "metadata_slice not found in HydrusDictionary" );
			}
		}
		else
		{
			spdlog::error( "HydrusDictionary info is not an array, type={}", static_cast< int >( info.type() ) );
			throw std::runtime_error( "HydrusDictionary info must be an array" );
		}
	}
	else
	{
		const auto type_str = root.isArray() && root.size() >= 1 ? std::to_string( root[ 0 ].asInt() ) : "non-array";
		spdlog::error( "Unexpected metadata response type: {}", type_str );
		throw std::runtime_error( "Expected HydrusDictionary (21) or MetadataUpdate (37), got " + type_str );
	}

	const auto parsed = parseUpdateJson( root );
	const auto* new_meta = std::get_if< MetadataUpdate >( &parsed );
	if ( !new_meta )
	{
		spdlog::error( "Metadata response is not a MetadataUpdate type" );
		throw std::runtime_error( "Expected Metadata update type" );
	}

	return *new_meta;
}

void PTRDownloader::processMetadataResponse( const QByteArray& data )
{
	spdlog::debug( "Processing metadata response: {} bytes", data.size() );

	const auto new_meta = parseMetadataBytes( data );

	spdlog::info(
		"Received metadata: {} update entries, next_update_due={}", new_meta.updates.size(), new_meta.next_update_due );

	for ( const auto& u : new_meta.updates )
	{
		spdlog::trace( "  update index={}: {} hashes, begin={}, end={}", u.index, u.hashes.size(), u.begin, u.end );
	}

	this->m_metadata = std::move( new_meta );

	for ( const auto& u : this->m_metadata.updates )
	{
		if ( u.index > m_last_update_index ) m_last_update_index = u.index;
	}

	spdlog::info(
		"Metadata processed: {} total updates, last_update_index={}",
		this->m_metadata.updates.size(),
		m_last_update_index );
}

} // namespace idhan::hydrus::ptr
