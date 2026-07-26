#include "PTRImportWorker.hpp"

#include <QEventLoop>
#include <QFuture>
#include <QLocale>

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <thread>
#include <type_traits>

#include "PTRConstants.hpp"
#include "PTRFileParser.hpp"
#include "idhan/IDHANClient.hpp"
#include "splitTag.hpp"

namespace
{

//! Split [0,n) into [start,end) chunks of at most \p batch_size and invoke \p launch(start,end) for
//! each, returning all the futures it produced. Nothing is awaited here — every batch is submitted
//! before any is waited on, so they run concurrently (bounded only by QNetworkAccessManager).
template < typename Launch >
auto launchBatches( const std::size_t n, const std::size_t batch_size, Launch&& launch )
{
	using Future = std::invoke_result_t< Launch&, std::size_t, std::size_t >;
	std::vector< Future > futures {};
	futures.reserve( n / batch_size + 1 );
	for ( std::size_t start = 0; start < n; start += batch_size )
		futures.push_back( launch( start, std::min( start + batch_size, n ) ) );
	return futures;
}

//! Wait for every value-returning future, invoking assign(input_index, value) for each result while
//! preserving batch alignment: a batch that throws leaves its indices unassigned rather than
//! shifting the ones after it. \p on_progress is called with (completed_batches, total_batches).
template < typename T, typename Assign, typename OnProgress >
void awaitBatches(
	std::vector< QFuture< std::vector< T > > >& futures,
	const std::size_t batch_size,
	const char* const what,
	Assign&& assign,
	OnProgress&& on_progress )
{
	for ( std::size_t b = 0; b < futures.size(); ++b )
	{
		try
		{
			futures[ b ].waitForFinished();
			const auto& results = futures[ b ].result();
			const std::size_t start = b * batch_size;
			for ( std::size_t j = 0; j < results.size(); ++j ) assign( start + j, results[ j ] );
		}
		catch ( const std::exception& e )
		{
			spdlog::error( "{} batch {} failed: {}", what, b, e.what() );
		}
		on_progress( b + 1, futures.size() );
	}
}

//! Wait for every void future, logging any that threw. \p on_progress is called with
//! (completed, total) after each future resolves.
template < typename OnProgress >
void awaitBatches( std::vector< QFuture< void > >& futures, const char* const what, OnProgress&& on_progress )
{
	for ( std::size_t i = 0; i < futures.size(); ++i )
	{
		try
		{
			futures[ i ].waitForFinished();
		}
		catch ( const std::exception& e )
		{
			spdlog::error( "{} batch {} failed: {}", what, i, e.what() );
		}
		on_progress( i + 1, futures.size() );
	}
}

} // namespace

namespace idhan::hydrus::ptr
{

PTRImportWorker::PTRImportWorker( const std::filesystem::path& ptr_directory, QObject* parent ) :
  QObject( parent ),
  QRunnable(),
  m_ptr_directory( ptr_directory )
{
	setAutoDelete( false );
	qRegisterMetaType< PTRHistoryEntry >( "PTRHistoryEntry" );
}

PTRImportWorker::~PTRImportWorker() = default;

void PTRImportWorker::run()
{
	spdlog::info( "PTR import worker started for directory: {}", m_ptr_directory.string() );

	try
	{
		emit progress( "Loading metadata..." );
		loadMetadata();

		emit progress( "Processing files in metadata order..." );
		if ( processInOrder() )
		{
			emit finished( false, "Cancelled" );
			return;
		}

		spdlog::info( "PTR import completed successfully" );
		emit finished( true, "PTR import completed successfully." );
	}
	catch ( const std::exception& e )
	{
		spdlog::error( "PTR import failed: {}", e.what() );
		emit finished( false, QString( "Import failed: %1" ).arg( e.what() ) );
	}
}

void PTRImportWorker::loadMetadata()
{
	// Try ptr_metadata.json first — written by the downloader, already parsed JSON
	const auto meta_json_path = m_ptr_directory / "ptr_metadata.json";
	{
		std::ifstream file( meta_json_path );
		if ( file )
		{
			Json::Value root;
			Json::CharReaderBuilder builder;
			std::string errors;
			if ( Json::parseFromStream( builder, file, &root, &errors ) )
			{
				m_metadata = parseMetadataCacheJson( root );

				const auto& imported = root[ "imported_files" ];
				if ( imported.isObject() )
				{
					for ( const auto& hash : imported.getMemberNames() )
					{
						if ( imported[ hash ].asBool() ) m_imported_hashes.insert( hash );
					}
				}

				spdlog::info(
					"Loaded metadata from ptr_metadata.json: {} update indices, {} previously imported",
					m_metadata.updates.size(),
					m_imported_hashes.size() );
				return;
			}
			spdlog::warn( "Failed to parse ptr_metadata.json: {}", errors );
		}
	}

	// Fall back to parsing metadata.ptrupdate (raw PTR server metadata)
	const auto meta_ptr_path = m_ptr_directory / "metadata.ptrupdate";
	if ( std::filesystem::exists( meta_ptr_path ) )
	{
		try
		{
			auto parsed = parseUpdateFile( meta_ptr_path );
			if ( auto* meta = std::get_if< MetadataUpdate >( &parsed ) )
			{
				m_metadata = std::move( *meta );
				spdlog::info( "Loaded metadata from metadata.ptrupdate: {} update indices", m_metadata.updates.size() );
				return;
			}
			spdlog::warn( "metadata.ptrupdate did not parse as MetadataUpdate" );
		}
		catch ( const std::exception& e )
		{
			spdlog::warn( "Failed to parse metadata.ptrupdate: {}", e.what() );
		}
	}

	throw std::runtime_error( "No metadata found in directory. Run the download step first." );
}

PTRImportWorker::DownloadStatus PTRImportWorker::readDownloadStatus() const
{
	const auto meta_json_path = m_ptr_directory / "ptr_metadata.json";
	{
		std::ifstream file( meta_json_path );
		if ( file )
		{
			Json::Value root;
			Json::CharReaderBuilder builder;
			std::string errors;
			if ( Json::parseFromStream( builder, file, &root, &errors ) )
			{
				// Missing "state" means this file was written before/without this feature, or by a
				// run that only ever wrote it once at the very end — either way, treat as done.
				const auto state = root.get( "state", "done" ).asString();
				const auto last_sync_time = root.get( "last_sync_time", Json::Int64( 0 ) ).asInt64();
				const auto now = std::chrono::duration_cast< std::chrono::seconds >(
									 std::chrono::system_clock::now().time_since_epoch() )
				                     .count();
				const auto age = std::max< std::int64_t >( 0, now - last_sync_time );
				return { state, std::chrono::seconds( age ) };
			}
			spdlog::warn( "Failed to parse ptr_metadata.json while polling: {}", errors );
		}
	}

	// No usable ptr_metadata.json yet — if the raw metadata.ptrupdate exists, the downloader has at
	// least started and just hasn't finished its first update index; treat it as still running.
	const auto meta_ptr_path = m_ptr_directory / "metadata.ptrupdate";
	std::error_code ec;
	const auto mtime = std::filesystem::last_write_time( meta_ptr_path, ec );
	if ( !ec )
	{
		const auto age = std::filesystem::file_time_type::clock::now() - mtime;
		return { "running", std::chrono::duration_cast< std::chrono::seconds >( age ) };
	}

	return { "running", std::chrono::seconds( 0 ) };
}

bool PTRImportWorker::waitForFile( const std::filesystem::path& file_path )
{
	while ( !std::filesystem::exists( file_path ) )
	{
		if ( m_cancelled ) return false;

		const auto status = readDownloadStatus();
		const bool terminal = status.state == "done" || status.state == "error" || status.state == "cancelled";
		if ( terminal )
		{
			spdlog::debug(
				"Downloader reported state '{}', giving up waiting for {}", status.state, file_path.string() );
			return false;
		}

		if ( status.heartbeat_age >= FILE_WAIT_STALL_TIMEOUT )
		{
			spdlog::warn(
				"Downloader heartbeat stale ({}s), giving up waiting for {}",
				status.heartbeat_age.count(),
				file_path.string() );
			return false;
		}

		emit progress( QString( "Waiting for downloader to fetch %1..." )
		                   .arg( QString::fromStdString( file_path.filename().string() ) ) );
		std::this_thread::sleep_for( FILE_WAIT_POLL_INTERVAL );
	}

	return true;
}

bool PTRImportWorker::processInOrder()
{
	// Sort update entries by index so we always process definitions before content that depends on them
	std::ranges::sort(
		m_metadata.updates,
		[]( const MetadataUpdateEntry& a, const MetadataUpdateEntry& b ) { return a.index < b.index; } );

	int64_t total_files = 0;
	for ( const auto& u : m_metadata.updates ) total_files += static_cast< int64_t >( u.hashes.size() );

	spdlog::info( "Processing up to {} files across {} update indices", total_files, m_metadata.updates.size() );

	// Get or create the tag domain once before the loop
	auto& client = IDHANClient::instance();
	const std::string domain_name = "public tag repository";
	TagDomainID domain_id = 0;

	{
		auto f = client.getTagDomain( domain_name );
		f.waitForFinished();
		auto result = f.result();
		if ( result.has_value() )
		{
			domain_id = result.value();
			spdlog::info( "Using existing tag domain: {} (id={})", domain_name, domain_id );
		}
		else
		{
			auto cf = client.createTagDomain( domain_name );
			cf.waitForFinished();
			domain_id = cf.result();
			spdlog::info( "Created new tag domain: {} (id={})", domain_name, domain_id );
		}
	}

	if ( domain_id == TagDomainID( 0 ) )
		throw std::runtime_error( "Failed to get or create tag domain '" + domain_name + "'" );

	int64_t processed = 0;
	int64_t definitions_processed = 0;
	int64_t content_processed = 0;

	for ( std::size_t update_index = 0; update_index < m_metadata.updates.size(); ++update_index )
	{
		const auto& update_entry = m_metadata.updates[ update_index ];

		if ( m_cancelled ) return true;

		const auto update_file_count = static_cast< int64_t >( update_entry.hashes.size() );
		int64_t update_file_index = 0;
		ContentStats update_stats;
		std::unordered_set< int > unique_tag_ids;

		emit progress( QString( "Processing update %1/%2 (%3 files)" )
		                   .arg( QLocale::system().toString( static_cast< qlonglong >( update_index + 1 ) ) )
		                   .arg( QLocale::system().toString( static_cast< qlonglong >( m_metadata.updates.size() ) ) )
		                   .arg( QLocale::system().toString( update_file_count ) ) );

		for ( const auto& hash_hex : update_entry.hashes )
		{
			if ( m_cancelled ) return true;

			++processed;
			++update_file_index;

			if ( m_imported_hashes.count( hash_hex ) )
			{
				spdlog::debug( "Skipping already-imported file: {}", hash_hex );
				emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
				continue;
			}

			const auto file_path = m_ptr_directory / ( hash_hex + ".ptrupdate" );
			if ( !std::filesystem::exists( file_path ) && !waitForFile( file_path ) )
			{
				if ( m_cancelled ) return true;

				spdlog::warn( "File never arrived, skipping: {}", file_path.string() );
				emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
				continue;
			}

			try
			{
				auto parsed = parseUpdateFile( file_path );

				QString prefix = QString( "Update %1: file %2/%3" )
				                     .arg( QLocale::system().toString( update_entry.index ) )
				                     .arg( QLocale::system().toString( update_file_index ) )
				                     .arg( QLocale::system().toString( update_file_count ) );

				if ( auto* defs = std::get_if< DefinitionsUpdate >( &parsed ) )
				{
					spdlog::trace(
						"Definitions [{}]: {} hashes, {} tags",
						hash_hex,
						defs->hash_ids_to_hashes.size(),
						defs->tag_ids_to_tags.size() );

					for ( auto& [ hash_id, hex ] : defs->hash_ids_to_hashes )
						m_tables.hash_id_to_sha256[ hash_id ] = std::move( hex );

					for ( auto& [ tag_id, tag ] : defs->tag_ids_to_tags )
						m_tables.tag_id_to_tag[ tag_id ] = std::move( tag );

					++definitions_processed;

					emit progress(
						QString( "%1 - definitions (%2 hashes, %3 tags known)" )
							.arg( prefix )
							.arg(
								QLocale::system().toString(
									static_cast< qlonglong >( m_tables.hash_id_to_sha256.size() ) ) )
							.arg(
								QLocale::system().toString(
									static_cast< qlonglong >( m_tables.tag_id_to_tag.size() ) ) ) );
				}
				else if ( auto* content = std::get_if< ContentUpdate >( &parsed ) )
				{
					spdlog::debug(
						"Content [{}]: {} mappings ({} delete), {} parents ({} delete), {} siblings ({} delete)",
						hash_hex,
						content->mappings_add.size(),
						content->mappings_delete.size(),
						content->tag_parents_add.size(),
						content->tag_parents_delete.size(),
						content->tag_siblings_add.size(),
						content->tag_siblings_delete.size() );

					auto file_stats = processSingleContentFile( hash_hex, *content, domain_id, prefix, unique_tag_ids );
					update_stats.records_created += file_stats.records_created;
					update_stats.mappings_added += file_stats.mappings_added;
					update_stats.mappings_removed += file_stats.mappings_removed;
					update_stats.parents_added += file_stats.parents_added;
					update_stats.parents_removed += file_stats.parents_removed;
					update_stats.aliases_added += file_stats.aliases_added;
					update_stats.aliases_removed += file_stats.aliases_removed;
					++content_processed;
				}
			}
			catch ( const std::exception& e )
			{
				spdlog::error( "Failed to process {}: {}", hash_hex, e.what() );
				// Continue with next file rather than aborting the whole import
			}

			emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
		}

		update_stats.tags_created = static_cast< int >( unique_tag_ids.size() );

		PTRHistoryEntry history_entry;
		history_entry.update_index = update_entry.index;
		history_entry.file_count = update_file_count;
		history_entry.stats = update_stats;
		emit updateCompleted( history_entry );
	}

	spdlog::info(
		"PTR import pass complete: {} definitions files, {} content files processed",
		definitions_processed,
		content_processed );
	return false;
}

ContentStats PTRImportWorker::processSingleContentFile(
	const std::string& hash_hex,
	const ContentUpdate& content,
	const TagDomainID domain_id,
	const QString& progress_prefix,
	std::unordered_set< int >& unique_tag_ids )
{
	ContentStats stats;
	auto& client = IDHANClient::instance();

	emit subProgress( 0, 0, progress_prefix + " - Analyzing..." );
	spdlog::trace( "Processing content file: {}", hash_hex );

	// ---- Gather unique ids and per-record tag sets in a single pass over each list ----
	std::unordered_map< int /*hash_id*/, std::set< int /*tag_id*/ > > hash_to_tags;
	std::unordered_map< int /*hash_id*/, std::set< int /*tag_id*/ > > hash_to_tags_delete;
	std::unordered_set< int > all_hash_ids;
	std::unordered_set< int > all_tag_ids;

	for ( const auto& mapping : content.mappings_add )
	{
		all_tag_ids.insert( mapping.tag_id );
		stats.mappings_added += static_cast< int >( mapping.hash_ids.size() );
		for ( const auto& hid : mapping.hash_ids )
		{
			all_hash_ids.insert( hid );
			hash_to_tags[ hid ].insert( mapping.tag_id );
		}
	}

	for ( const auto& mapping : content.mappings_delete )
	{
		all_tag_ids.insert( mapping.tag_id );
		stats.mappings_removed += static_cast< int >( mapping.hash_ids.size() );
		for ( const auto& hid : mapping.hash_ids )
		{
			all_hash_ids.insert( hid );
			hash_to_tags_delete[ hid ].insert( mapping.tag_id );
		}
	}

	for ( const auto& [ child_id, parent_id ] : content.tag_parents_add )
	{
		all_tag_ids.insert( child_id );
		all_tag_ids.insert( parent_id );
	}
	for ( const auto& [ child_id, parent_id ] : content.tag_parents_delete )
	{
		all_tag_ids.insert( child_id );
		all_tag_ids.insert( parent_id );
	}
	for ( const auto& [ bad_id, good_id ] : content.tag_siblings_add )
	{
		all_tag_ids.insert( bad_id );
		all_tag_ids.insert( good_id );
	}
	for ( const auto& [ bad_id, good_id ] : content.tag_siblings_delete )
	{
		all_tag_ids.insert( bad_id );
		all_tag_ids.insert( good_id );
	}

	stats.parents_added = static_cast< int >( content.tag_parents_add.size() );
	stats.parents_removed = static_cast< int >( content.tag_parents_delete.size() );
	stats.aliases_added = static_cast< int >( content.tag_siblings_add.size() );
	stats.aliases_removed = static_cast< int >( content.tag_siblings_delete.size() );

	unique_tag_ids.insert( all_tag_ids.begin(), all_tag_ids.end() );

	spdlog::trace( "Phase 1: {} unique hash_ids, {} unique tag_ids", all_hash_ids.size(), all_tag_ids.size() );

	// ---- Translate hash_ids -> hex, tag_ids -> (namespace, subtag) ----
	std::vector< std::string > hash_hexes;
	std::unordered_map< int, std::string > hash_id_to_hex;
	hash_hexes.reserve( all_hash_ids.size() );
	hash_id_to_hex.reserve( all_hash_ids.size() );
	for ( const auto& hid : all_hash_ids )
	{
		const auto it = m_tables.hash_id_to_sha256.find( hid );
		if ( it == m_tables.hash_id_to_sha256.end() )
		{
			spdlog::warn( "Missing hash definition for hash_id={}, skipping", hid );
			continue;
		}
		// A SHA-256 hex string is exactly 64 characters. Anything else is a malformed definition —
		// skip it so it never reaches the createRecords batch (mappings referencing it are dropped
		// downstream, since it won't be in hash_id_to_hex).
		if ( it->second.size() != 64 )
		{
			spdlog::warn( "Invalid hash for hash_id={} ({} chars, expected 64), skipping", hid, it->second.size() );
			continue;
		}
		hash_id_to_hex.emplace( hid, it->second );
		hash_hexes.push_back( it->second );
	}

	std::vector< std::pair< std::string, std::string > > tag_pairs;
	std::vector< int > translated_tag_ids; // parallel to tag_pairs — same index → same position
	tag_pairs.reserve( all_tag_ids.size() );
	translated_tag_ids.reserve( all_tag_ids.size() );
	for ( const auto& tid : all_tag_ids )
	{
		const auto it = m_tables.tag_id_to_tag.find( tid );
		if ( it == m_tables.tag_id_to_tag.end() )
		{
			spdlog::warn( "Missing tag definition for tag_id={}, skipping", tid );
			continue;
		}
		tag_pairs.push_back( splitTag( it->second ) );
		translated_tag_ids.push_back( tid );
	}

	spdlog::trace( "Phase 2/3: {} hashes, {} tags translated", hash_hexes.size(), tag_pairs.size() );

	if ( m_cancelled ) return stats;

	// ---- Phase A: create records and tags. Both are independent, so every batch of both is
	// submitted before either is awaited — records and tags are created concurrently. ----
	std::unordered_map< std::string, RecordID > hex_to_record_id;
	std::unordered_map< int, TagID > tag_id_to_idhan_id;
	hex_to_record_id.reserve( hash_hexes.size() );
	tag_id_to_idhan_id.reserve( tag_pairs.size() );

	{
		auto record_futures = launchBatches(
			hash_hexes.size(),
			BATCH_SIZE,
			[ & ]( const std::size_t s, const std::size_t e )
			{
				return client.createRecords( std::vector< std::string >(
					hash_hexes.begin() + static_cast< std::ptrdiff_t >( s ),
					hash_hexes.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			} );

		auto tag_futures = launchBatches(
			tag_pairs.size(),
			BATCH_SIZE,
			[ & ]( const std::size_t s, const std::size_t e )
			{
				return client.createTags( std::vector< std::pair< std::string, std::string > >(
					tag_pairs.begin() + static_cast< std::ptrdiff_t >( s ),
					tag_pairs.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			} );

		spdlog::trace(
			"Phase A: creating {} records / {} tags across {} + {} batches",
			hash_hexes.size(),
			tag_pairs.size(),
			record_futures.size(),
			tag_futures.size() );

		awaitBatches(
			record_futures,
			BATCH_SIZE,
			"createRecords",
			[ & ]( const std::size_t i, const RecordID rid )
			{
				if ( i < hash_hexes.size() )
				{
					hex_to_record_id[ hash_hexes[ i ] ] = rid;
					++stats.records_created;
				}
			},
			[ & ]( const std::size_t done, const std::size_t total )
			{
				emit subProgress(
					static_cast< int >( done ),
					static_cast< int >( total ),
					progress_prefix + " - creating records" );
			} );

		awaitBatches(
			tag_futures,
			BATCH_SIZE,
			"createTags",
			[ & ]( const std::size_t i, const TagID tid )
			{
				if ( i < translated_tag_ids.size() ) tag_id_to_idhan_id[ translated_tag_ids[ i ] ] = tid;
			},
			[ & ]( const std::size_t done, const std::size_t total )
			{
				emit subProgress(
					static_cast< int >( done ),
					static_cast< int >( total ),
					progress_prefix + " - creating tags" );
			} );

		spdlog::trace( "Phase A: {} records, {} tags registered", stats.records_created, tag_id_to_idhan_id.size() );
	}

	if ( m_cancelled ) return stats;

	// ---- Phase B: apply every mapping/relationship change. Each depends only on Phase A, not on
	// one another (adds and deletes within one update touch disjoint rows), so all of them are
	// launched up front and awaited together as one concurrent pool of void futures. ----

	// Build parallel (record_ids, tag_id_sets) from a hash_id -> tag_id_set map, resolving through
	// the ids Phase A already produced. Using TagIDs here avoids the string-pair addTags overload,
	// which would re-create every tag server-side (they already exist from Phase A).
	const auto buildRecordTagSets = [ & ]( const auto& hash_to_tagids )
	{
		std::vector< RecordID > recs;
		std::vector< std::vector< TagID > > sets;
		recs.reserve( hash_to_tagids.size() );
		sets.reserve( hash_to_tagids.size() );
		for ( const auto& [ hid, tid_set ] : hash_to_tagids )
		{
			const auto hex_it = hash_id_to_hex.find( hid );
			if ( hex_it == hash_id_to_hex.end() ) continue;
			const auto rec_it = hex_to_record_id.find( hex_it->second );
			if ( rec_it == hex_to_record_id.end() ) continue;

			std::vector< TagID > ids;
			ids.reserve( tid_set.size() );
			for ( const auto& tid : tid_set )
			{
				const auto it = tag_id_to_idhan_id.find( tid );
				if ( it != tag_id_to_idhan_id.end() ) ids.push_back( it->second );
			}
			if ( !ids.empty() )
			{
				recs.push_back( rec_it->second );
				sets.push_back( std::move( ids ) );
			}
		}
		return std::pair { std::move( recs ), std::move( sets ) };
	};

	// Translate raw (tag_id, tag_id) pairs into IDHAN (TagID, TagID) pairs, applying \p order to the
	// resolved ids to get the argument order each relationship endpoint expects.
	const auto translatePairs = [ & ]( const auto& raw, auto order )
	{
		std::vector< std::pair< TagID, TagID > > out;
		out.reserve( raw.size() );
		for ( const auto& [ a, b ] : raw )
		{
			const auto ia = tag_id_to_idhan_id.find( a );
			const auto ib = tag_id_to_idhan_id.find( b );
			if ( ia != tag_id_to_idhan_id.end() && ib != tag_id_to_idhan_id.end() )
				out.push_back( order( ia->second, ib->second ) );
		}
		return out;
	};

	// parent endpoints want (parent, child); raw pairs are (child, parent).
	const auto parent_order = []( const TagID child, const TagID parent ) { return std::pair { parent, child }; };
	// alias endpoints want (bad, good); raw pairs are already (bad, good).
	const auto alias_order = []( const TagID bad, const TagID good ) { return std::pair { bad, good }; };

	auto add_mappings = buildRecordTagSets( hash_to_tags );
	auto& add_recs = add_mappings.first;
	auto& add_sets = add_mappings.second;
	auto del_mappings = buildRecordTagSets( hash_to_tags_delete );
	auto& del_recs = del_mappings.first;
	auto& del_sets = del_mappings.second;
	auto parent_add = translatePairs( content.tag_parents_add, parent_order );
	auto parent_del = translatePairs( content.tag_parents_delete, parent_order );
	auto alias_add = translatePairs( content.tag_siblings_add, alias_order );
	auto alias_del = translatePairs( content.tag_siblings_delete, alias_order );

	std::vector< QFuture< void > > ops;

	const auto append = [ &ops ]( auto&& futures )
	{
		ops.insert(
			ops.end(), std::make_move_iterator( futures.begin() ), std::make_move_iterator( futures.end() ) );
	};

	// Phase 6: apply mappings
	append( launchBatches(
		add_recs.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			std::vector< RecordID > br(
				add_recs.begin() + static_cast< std::ptrdiff_t >( s ),
				add_recs.begin() + static_cast< std::ptrdiff_t >( e ) );
			std::vector< std::vector< TagID > > bs(
				std::make_move_iterator( add_sets.begin() + static_cast< std::ptrdiff_t >( s ) ),
				std::make_move_iterator( add_sets.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			return client.addTags( std::move( br ), domain_id, std::move( bs ) );
		} ) );

	// Phase 7: add parent relationships
	append( launchBatches(
		parent_add.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			return client.createParentRelationship(
				domain_id,
				std::vector< std::pair< TagID, TagID > >(
					parent_add.begin() + static_cast< std::ptrdiff_t >( s ),
					parent_add.begin() + static_cast< std::ptrdiff_t >( e ) ) );
		} ) );

	// Phase 8: add alias relationships
	append( launchBatches(
		alias_add.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			return client.createAliasRelationship(
				domain_id,
				std::vector< std::pair< TagID, TagID > >(
					alias_add.begin() + static_cast< std::ptrdiff_t >( s ),
					alias_add.begin() + static_cast< std::ptrdiff_t >( e ) ) );
		} ) );

	// Phase 9: remove mappings
	append( launchBatches(
		del_recs.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			std::vector< RecordID > br(
				del_recs.begin() + static_cast< std::ptrdiff_t >( s ),
				del_recs.begin() + static_cast< std::ptrdiff_t >( e ) );
			std::vector< std::vector< TagID > > bs(
				std::make_move_iterator( del_sets.begin() + static_cast< std::ptrdiff_t >( s ) ),
				std::make_move_iterator( del_sets.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			return client.removeTags( std::move( br ), domain_id, std::move( bs ) );
		} ) );

	// Phase 10: remove parent relationships
	append( launchBatches(
		parent_del.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			return client.removeParentRelationship(
				domain_id,
				std::vector< std::pair< TagID, TagID > >(
					parent_del.begin() + static_cast< std::ptrdiff_t >( s ),
					parent_del.begin() + static_cast< std::ptrdiff_t >( e ) ) );
		} ) );

	// Phase 11: remove alias relationships
	append( launchBatches(
		alias_del.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			return client.removeAliasRelationship(
				domain_id,
				std::vector< std::pair< TagID, TagID > >(
					alias_del.begin() + static_cast< std::ptrdiff_t >( s ),
					alias_del.begin() + static_cast< std::ptrdiff_t >( e ) ) );
		} ) );

	if ( !ops.empty() )
	{
		spdlog::trace( "Phase B: applying {} update batches concurrently", ops.size() );
		emit subProgress( 0, static_cast< int >( ops.size() ), progress_prefix + " - applying updates..." );
		awaitBatches(
			ops,
			"content update",
			[ & ]( const std::size_t done, const std::size_t total )
			{
				emit subProgress(
					static_cast< int >( done ),
					static_cast< int >( total ),
					progress_prefix
						+ QString( " - applying updates (%1/%2)" )
							  .arg( QLocale::system().toString( static_cast< qlonglong >( done ) ) )
							  .arg( QLocale::system().toString( static_cast< qlonglong >( total ) ) ) );
			} );
	}

	emit subProgress( 1, 1, progress_prefix + " - Done" );
	spdlog::debug( "Finished processing content file: {}", hash_hex );
	return stats;
}

} // namespace idhan::hydrus::ptr
