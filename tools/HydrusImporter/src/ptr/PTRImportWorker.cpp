#include "PTRImportWorker.hpp"

#include <QCryptographicHash>
#include <QThread>

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <map>
#include <ranges>
#include <set>

#include "PTRConstants.hpp"
#include "PTRFileParser.hpp"
#include "idhan/IDHANClient.hpp"
#include "splitTag.hpp"

namespace idhan::hydrus::ptr
{

PTRImportWorker::PTRImportWorker( const std::filesystem::path& ptr_directory, QObject* parent ) :
  QObject( parent ),
  QRunnable(),
  m_ptr_directory( ptr_directory )
{
	setAutoDelete( false );
}

PTRImportWorker::~PTRImportWorker() = default;

void PTRImportWorker::run()
{
	spdlog::info( "PTR import worker started for directory: {}", m_ptr_directory.string() );

	try
	{
		emit progress( "Scanning directory..." );
		scanDirectory();

		emit progress( "Loading metadata for ordering..." );
		loadMetadataForOrdering();

		emit progress( "Building translation tables from definitions..." );
		buildTranslationTables();

		emit progress( "Importing content files..." );
		processContentFiles();

		spdlog::info( "PTR import completed successfully" );
		emit finished( true, "PTR import completed successfully." );
	}
	catch ( const std::exception& e )
	{
		spdlog::error( "PTR import failed: {}", e.what() );
		emit finished( false, QString( "Import failed: %1" ).arg( e.what() ) );
	}
}

void PTRImportWorker::scanDirectory()
{
	m_file_entries.clear();

	if ( !std::filesystem::exists( m_ptr_directory ) ) throw std::runtime_error( "PTR directory does not exist" );

	for ( const auto& entry : std::filesystem::directory_iterator( m_ptr_directory ) )
	{
		if ( m_cancelled ) return;

		if ( !entry.is_regular_file() ) continue;

		const auto ext = entry.path().extension().string();
		if ( ext != ".ptrupdate" ) continue;

		FileEntry fe;
		fe.path = entry.path();
		fe.hash_hex = entry.path().stem().string();

		// Determine type by reading the file
		try
		{
			const auto data = readFile( fe.path );
			const auto root = decompressToJson( data );
			fe.type = detectUpdateType( root );
		}
		catch ( ... )
		{
			spdlog::warn( "Skipping unparseable file: {}", fe.path.string() );
			continue;
		}

		m_file_entries.push_back( std::move( fe ) );
	}

	int def_count = 0, content_count = 0, unknown_count = 0;
	for ( const auto& fe : m_file_entries )
	{
		if ( fe.is_definitions() )
			++def_count;
		else if ( fe.is_content() )
			++content_count;
		else
			++unknown_count;
	}

	spdlog::info(
		"Found {} PTR files in directory ({} definitions, {} content, {} unknown)",
		m_file_entries.size(),
		def_count,
		content_count,
		unknown_count );
}

void PTRImportWorker::loadMetadataForOrdering()
{
	const auto meta_path = m_ptr_directory / "ptr_metadata.json";
	std::ifstream file( meta_path );
	if ( !file )
	{
		spdlog::warn( "No ptr_metadata.json found; using file modification time for ordering" );
		return;
	}

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	if ( !Json::parseFromStream( builder, file, &root, &errors ) )
	{
		spdlog::warn( "Failed to parse ptr_metadata.json; using file mtime for ordering" );
		return;
	}

	// Parse metadata
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
	m_metadata.next_update_due = root.get( "next_update_due", 0 ).asInt64();

	// Parse imported files tracking
	const auto& imported = root[ "imported_files" ];
	if ( imported.isObject() )
	{
		for ( const auto& hash : imported.getMemberNames() )
		{
			if ( imported[ hash ].asBool() ) m_imported_hashes.insert( hash );
		}
	}

	spdlog::info(
		"Loaded metadata: {} update indices, {} previously imported files",
		m_metadata.updates.size(),
		m_imported_hashes.size() );

	// Map update index to file entries
	std::map< int, std::vector< FileEntry* > > index_map;
	for ( auto& fe : m_file_entries ) fe.update_index = -1;

	// Build lookup from hash_hex to file entry
	std::unordered_map< std::string, FileEntry* > hash_to_entry;
	for ( auto& fe : m_file_entries ) hash_to_entry[ fe.hash_hex ] = &fe;

	// Assign indices
	for ( const auto& u : m_metadata.updates )
	{
		for ( const auto& h : u.hashes )
		{
			auto it = hash_to_entry.find( h );
			if ( it != hash_to_entry.end() ) it->second->update_index = u.index;
		}
	}
}

void PTRImportWorker::buildTranslationTables()
{
	// Sort files: definitions first, then by update_index, then by mtime
	std::vector< FileEntry* > ordered;
	for ( auto& fe : m_file_entries )
	{
		if ( fe.is_definitions() ) ordered.push_back( &fe );
	}

	std::ranges::sort(
		ordered,
		[]( const FileEntry* a, const FileEntry* b )
		{
			if ( a->update_index != b->update_index ) return a->update_index < b->update_index;
			return a->path < b->path;
		} );

	spdlog::info( "Processing {} definition files for translation tables...", ordered.size() );

	int processed = 0;
	for ( auto* fe : ordered )
	{
		if ( m_cancelled ) return;

		spdlog::trace( "Processing definition file: {}", fe->path.string() );

		try
		{
			auto parsed = parseUpdateFile( fe->path );
			auto* defs = std::get_if< DefinitionsUpdate >( &parsed );
			if ( !defs )
			{
				spdlog::warn( "File {} is not a definitions update, skipping", fe->path.string() );
				continue;
			}

			spdlog::trace(
				"Definitions in {}: {} hashes, {} tags",
				fe->path.filename().string(),
				defs->hash_ids_to_hashes.size(),
				defs->tag_ids_to_tags.size() );

			// Merge into translation tables
			for ( auto& [ hash_id, hash_hex ] : defs->hash_ids_to_hashes )
				m_tables.hash_id_to_sha256[ hash_id ] = std::move( hash_hex );

			for ( auto& [ tag_id, tag_str ] : defs->tag_ids_to_tags )
				m_tables.tag_id_to_tag[ tag_id ] = std::move( tag_str );

			++processed;

			if ( processed % 100 == 0 )
			{
				emit progress( QString( "Building translations: %1 files, %2 hashes, %3 tags" )
				                   .arg( processed )
				                   .arg( m_tables.hash_id_to_sha256.size() )
				                   .arg( m_tables.tag_id_to_tag.size() ) );
			}
		}
		catch ( const std::exception& e )
		{
			spdlog::warn( "Failed to parse definition file {}: {}", fe->path.string(), e.what() );
		}
	}

	spdlog::info(
		"Translation tables built: {} hashes, {} tags from {} files",
		m_tables.hash_id_to_sha256.size(),
		m_tables.tag_id_to_tag.size(),
		processed );
}

void PTRImportWorker::processContentFiles()
{
	std::vector< FileEntry* > ordered;
	for ( auto& fe : m_file_entries )
	{
		if ( fe.is_content() ) ordered.push_back( &fe );
	}

	std::ranges::sort(
		ordered,
		[]( const FileEntry* a, const FileEntry* b )
		{
			if ( a->update_index != b->update_index ) return a->update_index < b->update_index;
			return a->path < b->path;
		} );

	// Remove already-imported files
	std::vector< FileEntry* > to_process;
	for ( auto* fe : ordered )
	{
		if ( m_imported_hashes.find( fe->hash_hex ) == m_imported_hashes.end() ) to_process.push_back( fe );
	}

	spdlog::info(
		"Content files to process: {} ({} already imported)", to_process.size(), ordered.size() - to_process.size() );

	if ( to_process.empty() )
	{
		emit progress( "All content files already imported." );
		return;
	}

	auto& client = IDHANClient::instance();

	// Create or find tag domain
	const std::string domain_name = "public tag repository";
	TagDomainID domain_id = 0;

	auto domain_search_f = client.getTagDomain( domain_name );
	domain_search_f.waitForFinished();
	auto domain_result = domain_search_f.result();
	if ( domain_result.has_value() )
	{
		domain_id = domain_result.value();
		spdlog::info( "Using existing tag domain: {} (id={})", domain_name, domain_id );
	}
	else
	{
		auto domain_f = client.createTagDomain( domain_name );
		domain_f.waitForFinished();
		domain_id = domain_f.result();
		spdlog::info( "Created new tag domain: {} (id={})", domain_name, domain_id );
	}

	int processed = 0;
	const int total = static_cast< int >( to_process.size() );

	for ( auto* fe : to_process )
	{
		if ( m_cancelled )
		{
			spdlog::warn( "Import cancelled by user" );
			emit finished( false, "Cancelled" );
			return;
		}

		spdlog::debug( "Processing content file {}/{}: {}", processed + 1, total, fe->path.filename().string() );

		try
		{
			auto parsed = parseUpdateFile( fe->path );
			auto* content = std::get_if< ContentUpdate >( &parsed );
			if ( !content )
			{
				spdlog::warn( "File {} is not a content update, skipping", fe->path.string() );
				continue;
			}

			spdlog::trace(
				"Content in {}: {} mappings, {} parents, {} siblings",
				fe->hash_hex,
				content->mappings_add.size(),
				content->tag_parents_add.size(),
				content->tag_siblings_add.size() );

			processSingleContentFile( *fe, *content, domain_id );

			++processed;

			emit progress(
				QString( "Imported %1/%2: %3 (mappings: %4, parents: %5, siblings: %6)" )
					.arg( processed )
					.arg( total )
					.arg( QString::fromStdString( fe->hash_hex ) )
					.arg( content->mappings_add.size() )
					.arg( content->tag_parents_add.size() )
					.arg( content->tag_siblings_add.size() ) );

			emit fileProcessed( QString::fromStdString( fe->hash_hex ), processed, total );
		}
		catch ( const std::exception& e )
		{
			spdlog::error( "Failed to process {}: {}", fe->path.string(), e.what() );
			// Continue with next file
		}
	}

	spdlog::info( "PTR import completed: {} content files processed", processed );
}

void PTRImportWorker::processSingleContentFile(
	const FileEntry& entry,
	const ContentUpdate& content,
	const TagDomainID domain_id )
{
	auto& client = IDHANClient::instance();

	spdlog::trace( "Processing content file: {}", entry.hash_hex );

	// ── Phase 1: Collect all unique hashes and tags ──
	std::unordered_map< int /*hash_id*/, std::set< int /*tag_id*/ > > hash_to_tags;
	std::unordered_set< int > all_hash_ids;
	std::unordered_set< int > all_tag_ids;

	for ( const auto& mapping : content.mappings_add )
	{
		all_tag_ids.insert( mapping.tag_id );
		spdlog::trace( "  mapping: tag_id={} -> {} hash_ids", mapping.tag_id, mapping.hash_ids.size() );
		for ( const auto& hid : mapping.hash_ids )
		{
			all_hash_ids.insert( hid );
			hash_to_tags[ hid ].insert( mapping.tag_id );
		}
	}

	for ( const auto& [ child_id, parent_id ] : content.tag_parents_add )
	{
		all_tag_ids.insert( child_id );
		all_tag_ids.insert( parent_id );
		spdlog::trace( "  parent: child_id={}, parent_id={}", child_id, parent_id );
	}

	for ( const auto& [ bad_id, good_id ] : content.tag_siblings_add )
	{
		all_tag_ids.insert( bad_id );
		all_tag_ids.insert( good_id );
		spdlog::trace( "  sibling: bad_id={}, good_id={}", bad_id, good_id );
	}

	spdlog::trace( "Phase 1: {} unique hash_ids, {} unique tag_ids", all_hash_ids.size(), all_tag_ids.size() );

	// ── Phase 2: Translate hash_ids to SHA-256 hex strings ──
	std::vector< std::string > hash_hexes;
	hash_hexes.reserve( all_hash_ids.size() );

	std::unordered_map< int, std::string > hash_id_to_hex;
	for ( const auto& hid : all_hash_ids )
	{
		auto it = m_tables.hash_id_to_sha256.find( hid );
		if ( it == m_tables.hash_id_to_sha256.end() )
		{
			spdlog::warn( "Missing hash definition for hash_id={}, skipping", hid );
			continue;
		}
		hash_id_to_hex[ hid ] = it->second;
		hash_hexes.push_back( it->second );
	}

	spdlog::trace( "Phase 2: {} hash_ids translated to SHA-256", hash_hexes.size() );

	// ── Phase 3: Translate tag_ids to (namespace, subtag) pairs ──
	std::vector< std::pair< std::string, std::string > > tag_pairs;
	tag_pairs.reserve( all_tag_ids.size() );

	std::unordered_map< int, std::pair< std::string, std::string > > tag_id_to_pair;
	for ( const auto& tid : all_tag_ids )
	{
		auto it = m_tables.tag_id_to_tag.find( tid );
		if ( it == m_tables.tag_id_to_tag.end() )
		{
			spdlog::warn( "Missing tag definition for tag_id={}, skipping", tid );
			continue;
		}
		tag_id_to_pair[ tid ] = splitTag( it->second );
		tag_pairs.push_back( tag_id_to_pair[ tid ] );
	}

	spdlog::trace( "Phase 3: {} tag_ids translated to strings", tag_pairs.size() );

	// ── Phase 4: Create records in IDHAN ──
	std::unordered_map< std::string, RecordID > hex_to_record_id;
	if ( !hash_hexes.empty() )
	{
		spdlog::trace( "Phase 4: creating {} records via IDHAN API", hash_hexes.size() );
		auto records_future = client.createRecords( hash_hexes );
		records_future.waitForFinished();
		const auto record_ids = records_future.result();

		spdlog::trace( "Phase 4: got {} record IDs back", record_ids.size() );
		for ( size_t i = 0; i < hash_hexes.size(); ++i ) hex_to_record_id[ hash_hexes[ i ] ] = record_ids[ i ];
	}

	// ── Phase 5: Create tags in IDHAN ──
	std::unordered_map< int, TagID > tag_id_to_idhan_id;
	if ( !tag_pairs.empty() )
	{
		spdlog::trace( "Phase 5: creating {} tags via IDHAN API", tag_pairs.size() );
		auto tags_future = client.createTags( tag_pairs );
		tags_future.waitForFinished();
		const auto idhan_tag_ids = tags_future.result();

		size_t idx = 0;
		for ( const auto& tid : all_tag_ids )
		{
			if ( tag_id_to_pair.contains( tid ) )
			{
				tag_id_to_idhan_id[ tid ] = idhan_tag_ids[ idx ];
				++idx;
			}
		}
		spdlog::trace( "Phase 5: {} tags registered in IDHAN", tag_id_to_idhan_id.size() );
	}

	// ── Phase 6: Apply mappings ──
	if ( !hash_to_tags.empty() )
	{
		spdlog::trace( "Phase 6: applying {} hash->tag mappings", hash_to_tags.size() );
		std::vector< RecordID > record_ids;
		std::vector< std::vector< std::pair< std::string, std::string > > > tag_sets;

		for ( const auto& [ hid, tag_id_set ] : hash_to_tags )
		{
			auto hex_it = hash_id_to_hex.find( hid );
			if ( hex_it == hash_id_to_hex.end() ) continue;

			auto rec_it = hex_to_record_id.find( hex_it->second );
			if ( rec_it == hex_to_record_id.end() ) continue;

			std::vector< std::pair< std::string, std::string > > file_tags;
			for ( const auto& tid : tag_id_set )
			{
				auto tag_it = tag_id_to_pair.find( tid );
				if ( tag_it != tag_id_to_pair.end() ) file_tags.push_back( tag_it->second );
			}

			if ( !file_tags.empty() )
			{
				record_ids.push_back( rec_it->second );
				tag_sets.push_back( std::move( file_tags ) );
			}
		}

		if ( !record_ids.empty() )
		{
			spdlog::trace( "Phase 6: calling addTags with {} records", record_ids.size() );
			auto add_future = client.addTags( std::move( record_ids ), domain_id, std::move( tag_sets ) );
			add_future.waitForFinished();
			spdlog::trace( "Phase 6: addTags completed" );
		}
	}

	// ── Phase 7: Apply parent relationships ──
	if ( !content.tag_parents_add.empty() )
	{
		spdlog::trace( "Phase 7: applying {} parent relationships", content.tag_parents_add.size() );
		std::vector< std::pair< TagID, TagID > > parent_pairs;
		for ( const auto& [ child_id, parent_id ] : content.tag_parents_add )
		{
			auto child_it = tag_id_to_idhan_id.find( child_id );
			auto parent_it = tag_id_to_idhan_id.find( parent_id );
			if ( child_it != tag_id_to_idhan_id.end() && parent_it != tag_id_to_idhan_id.end() )
				parent_pairs.emplace_back( parent_it->second, child_it->second );
		}

		if ( !parent_pairs.empty() )
		{
			spdlog::trace( "Phase 7: creating {} parent relationships", parent_pairs.size() );
			auto parent_future = client.createParentRelationship( domain_id, parent_pairs );
			parent_future.waitForFinished();
		}
	}

	// ── Phase 8: Apply sibling/alias relationships ──
	if ( !content.tag_siblings_add.empty() )
	{
		spdlog::trace( "Phase 8: applying {} sibling relationships", content.tag_siblings_add.size() );
		std::vector< std::pair< TagID, TagID > > sibling_pairs;
		for ( const auto& [ bad_id, good_id ] : content.tag_siblings_add )
		{
			auto bad_it = tag_id_to_idhan_id.find( bad_id );
			auto good_it = tag_id_to_idhan_id.find( good_id );
			if ( bad_it != tag_id_to_idhan_id.end() && good_it != tag_id_to_idhan_id.end() )
				sibling_pairs.emplace_back( bad_it->second, good_it->second );
		}

		if ( !sibling_pairs.empty() )
		{
			spdlog::trace( "Phase 8: creating {} alias relationships", sibling_pairs.size() );
			auto sibling_future = client.createAliasRelationship( domain_id, sibling_pairs );
			sibling_future.waitForFinished();
		}
	}

	spdlog::debug( "Finished processing content file: {}", entry.hash_hex );
}

} // namespace idhan::hydrus::ptr
