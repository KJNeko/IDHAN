#include "PTRImportWorker.hpp"

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <fstream>
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
				spdlog::info(
					"Loaded metadata from metadata.ptrupdate: {} update indices", m_metadata.updates.size() );
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

bool PTRImportWorker::processInOrder()
{
	// Sort update entries by index so we always process definitions before content that depends on them
	std::ranges::sort(
		m_metadata.updates, []( const MetadataUpdateEntry& a, const MetadataUpdateEntry& b ) { return a.index < b.index; } );

	int64_t total_files = 0;
	for ( const auto& u : m_metadata.updates ) total_files += static_cast< int64_t >( u.hashes.size() );

	spdlog::info(
		"Processing up to {} files across {} update indices", total_files, m_metadata.updates.size() );

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

	for ( const auto& update_entry : m_metadata.updates )
	{
		if ( m_cancelled ) return true;

		for ( const auto& hash_hex : update_entry.hashes )
		{
			if ( m_cancelled ) return true;

			++processed;

			if ( m_imported_hashes.count( hash_hex ) )
			{
				spdlog::debug( "Skipping already-imported file: {}", hash_hex );
				emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
				continue;
			}

			const auto file_path = m_ptr_directory / ( hash_hex + ".ptrupdate" );
			if ( !std::filesystem::exists( file_path ) )
			{
				spdlog::warn( "File not found, skipping: {}", file_path.string() );
				emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
				continue;
			}

			try
			{
				auto parsed = parseUpdateFile( file_path );

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
						QString( "Update %1: definitions (%2 hashes, %3 tags known)" )
							.arg( update_entry.index )
							.arg( m_tables.hash_id_to_sha256.size() )
							.arg( m_tables.tag_id_to_tag.size() ) );
				}
				else if ( auto* content = std::get_if< ContentUpdate >( &parsed ) )
				{
					spdlog::debug(
						"Content [{}]: {} mappings, {} parents, {} siblings",
						hash_hex,
						content->mappings_add.size(),
						content->tag_parents_add.size(),
						content->tag_siblings_add.size() );

					processSingleContentFile( hash_hex, *content, domain_id );
					++content_processed;

					emit progress(
						QString( "Update %1: content %2/%3 (%4 mappings, %5 parents, %6 siblings)" )
							.arg( update_entry.index )
							.arg( processed )
							.arg( total_files )
							.arg( content->mappings_add.size() )
							.arg( content->tag_parents_add.size() )
							.arg( content->tag_siblings_add.size() ) );
				}
				// Metadata-type files in the directory are silently skipped
			}
			catch ( const std::exception& e )
			{
				spdlog::error( "Failed to process {}: {}", hash_hex, e.what() );
				// Continue with next file rather than aborting the whole import
			}

			emit fileProcessed( static_cast< int >( processed ), static_cast< int >( total_files ) );
		}
	}

	spdlog::info(
		"PTR import pass complete: {} definitions files, {} content files processed",
		definitions_processed,
		content_processed );
	return false;
}

void PTRImportWorker::processSingleContentFile(
	const std::string& hash_hex,
	const ContentUpdate& content,
	const TagDomainID domain_id )
{
	auto& client = IDHANClient::instance();

	spdlog::trace( "Processing content file: {}", hash_hex );

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

	std::vector< std::pair< std::string, std::string > > tag_pairs;
	std::vector< int > translated_tag_ids; // parallel to tag_pairs — same index → same position
	tag_pairs.reserve( all_tag_ids.size() );
	translated_tag_ids.reserve( all_tag_ids.size() );

	std::unordered_map< int, std::pair< std::string, std::string > > tag_id_to_pair;
	for ( const auto& tid : all_tag_ids )
	{
		auto it = m_tables.tag_id_to_tag.find( tid );
		if ( it == m_tables.tag_id_to_tag.end() )
		{
			spdlog::warn( "Missing tag definition for tag_id={}, skipping", tid );
			continue;
		}
		auto pair = splitTag( it->second );
		tag_id_to_pair[ tid ] = pair;
		tag_pairs.push_back( std::move( pair ) );
		translated_tag_ids.push_back( tid );
	}

	spdlog::trace( "Phase 3: {} tag_ids translated to strings", tag_pairs.size() );

	std::unordered_map< std::string, RecordID > hex_to_record_id;
	if ( !hash_hexes.empty() )
	{
		spdlog::trace( "Phase 4: creating {} records via IDHAN API", hash_hexes.size() );
		auto records_future = client.createRecords( hash_hexes );
		records_future.waitForFinished();
		const auto record_ids = records_future.result();

		spdlog::trace( "Phase 4: got {} record IDs back", record_ids.size() );
		if ( record_ids.size() != hash_hexes.size() )
		{
			spdlog::error(
				"createRecords returned {} IDs for {} hashes — skipping content file {}",
				record_ids.size(),
				hash_hexes.size(),
				hash_hex );
			return;
		}
		for ( size_t i = 0; i < hash_hexes.size(); ++i ) hex_to_record_id[ hash_hexes[ i ] ] = record_ids[ i ];
	}

	std::unordered_map< int, TagID > tag_id_to_idhan_id;
	if ( !tag_pairs.empty() )
	{
		spdlog::trace( "Phase 5: creating {} tags via IDHAN API", tag_pairs.size() );
		auto tags_future = client.createTags( tag_pairs );
		tags_future.waitForFinished();
		const auto idhan_tag_ids = tags_future.result();

		if ( idhan_tag_ids.size() != translated_tag_ids.size() )
		{
			spdlog::error(
				"createTags returned {} IDs for {} tags — skipping content file {}",
				idhan_tag_ids.size(),
				translated_tag_ids.size(),
				hash_hex );
			return;
		}

		for ( size_t i = 0; i < translated_tag_ids.size(); ++i )
			tag_id_to_idhan_id[ translated_tag_ids[ i ] ] = idhan_tag_ids[ i ];

		spdlog::trace( "Phase 5: {} tags registered in IDHAN", tag_id_to_idhan_id.size() );
	}

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

	spdlog::debug( "Finished processing content file: {}", hash_hex );
}

} // namespace idhan::hydrus::ptr
