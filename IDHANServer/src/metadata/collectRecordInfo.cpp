#include <unordered_map>
#include <utility>

#include "PerceptualHash.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "logging/log.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

//! Every key a record's info object is built from. A Json::StaticString stores the pointer instead
//! of copying the key, which matters once a batch is a thousand records wide.
//!@{
static const Json::StaticString KEY_RECORD_ID { "record_id" };
static const Json::StaticString KEY_HASHES { "hashes" };
static const Json::StaticString KEY_SHA256 { "sha256" };
static const Json::StaticString KEY_SIZE { "size" };
static const Json::StaticString KEY_MIME { "mime" };
static const Json::StaticString KEY_EXTENSION { "extension" };
static const Json::StaticString KEY_MODIFIED_TIME { "modified_time" };
static const Json::StaticString KEY_PARSED { "parsed" };
static const Json::StaticString KEY_SIMPLE_TYPE { "simple_type" };
static const Json::StaticString KEY_EXTRA { "extra" };
static const Json::StaticString KEY_WIDTH { "width" };
static const Json::StaticString KEY_HEIGHT { "height" };
static const Json::StaticString KEY_CHANNELS { "channels" };
static const Json::StaticString KEY_PHASH { "phash" };
static const Json::StaticString KEY_HAS_EXIF { "has_exif" };
static const Json::StaticString KEY_HAS_GPS { "has_gps" };
static const Json::StaticString KEY_HAS_XMP { "has_xmp" };
static const Json::StaticString KEY_HAS_IPTC { "has_iptc" };
static const Json::StaticString KEY_HAS_ICC_PROFILE { "has_icc_profile" };
static const Json::StaticString KEY_LAYERS { "layers" };
static const Json::StaticString KEY_DURATION { "duration" };
static const Json::StaticString KEY_BITRATE { "bitrate" };
static const Json::StaticString KEY_FRAMERATE { "framerate" };
static const Json::StaticString KEY_HAS_AUDIO { "has_audio" };
static const Json::StaticString KEY_SAMPLE_RATE { "sample_rate" };
static const Json::StaticString KEY_FRAME_COUNT { "frame_count" };
static const Json::StaticString KEY_LOOPS { "loops" };
static const Json::StaticString KEY_ARCHIVE_ID { "archive_id" };
static const Json::StaticString KEY_ARCHIVE_IDS { "archive_ids" };
static const Json::StaticString KEY_ENCRYPTED { "encrypted" };
static const Json::StaticString KEY_FILE_COUNT { "file_count" };

//!@}

constexpr std::size_t PHASH_BITS { std::tuple_size_v< PerceptualHash > * 8 };

static std::string_view simpleTypeName( const SimpleMimeType type )
{
	switch ( type )
	{
		case SimpleMimeType::IMAGE_TYPE:
			return "image";
		case SimpleMimeType::VIDEO:
			return "video";
		case SimpleMimeType::ANIMATION:
			return "animation";
		case SimpleMimeType::AUDIO:
			return "audio";
		case SimpleMimeType::ARCHIVE:
			return "archive";
		case SimpleMimeType::IMAGE_PROJECT:
			return "image_project";
		case SimpleMimeType::NONE:
			[[fallthrough]];
		default:
			return "none";
	}
}

//! Records grouped by the table their file-specific columns live in, so each table is queried once
//! for the whole batch instead of once per record.
struct TypeGroups
{
	std::vector< RecordID > image {};
	std::vector< RecordID > video {};
	std::vector< RecordID > image_project {};
	std::vector< RecordID > audio {};
	std::vector< RecordID > animation {};
	std::vector< RecordID > archive {};
};

//! Where each record's half-built entry sits in the working vector.
using EntryIndex = std::unordered_map< RecordID, std::size_t >;

static void addBaseFields( Json::Value& entry, const drogon::orm::Row& row, const RecordID record_id )
{
	entry[ KEY_RECORD_ID ] = record_id;
	entry[ KEY_HASHES ][ KEY_SHA256 ] = SHA256::fromPgCol( row[ "sha256" ] ).hex();
	// A default-constructed Json::Value stays null until something is appended, so a record that is
	// in no archive would serialize as null rather than an empty array.
	entry[ KEY_ARCHIVE_IDS ] = Json::Value { Json::arrayValue };

	if ( !row[ "mime_id" ].isNull() )
	{
		entry[ KEY_SIZE ] = row[ "size" ].as< std::size_t >();
		entry[ KEY_MIME ] = row[ "name" ].as< std::string >();
		entry[ KEY_EXTENSION ] = row[ "best_extension" ].as< std::string >();
		if ( !row[ "modified_time" ].isNull() )
			entry[ KEY_MODIFIED_TIME ] = row[ "modified_time" ].as< std::int64_t >();
	}
}

static void addMetadataFields(
	Json::Value& entry,
	const drogon::orm::Row& row,
	const RecordID record_id,
	TypeGroups& groups )
{
	if ( row[ "simple_mime_type" ].isNull() )
	{
		entry[ KEY_PARSED ] = false;
		return;
	}

	entry[ KEY_PARSED ] = true;

	const SimpleMimeType simple_type { row[ "simple_mime_type" ].as< std::uint16_t >() };

	entry[ KEY_SIMPLE_TYPE ] = std::string( simpleTypeName( simple_type ) );
	entry[ KEY_EXTRA ] = row[ "json" ].as< Json::Value >();

	switch ( simple_type )
	{
		case SimpleMimeType::IMAGE_TYPE:
			groups.image.emplace_back( record_id );
			break;
		case SimpleMimeType::VIDEO:
			groups.video.emplace_back( record_id );
			break;
		case SimpleMimeType::IMAGE_PROJECT:
			groups.image_project.emplace_back( record_id );
			break;
		case SimpleMimeType::AUDIO:
			groups.audio.emplace_back( record_id );
			break;
		case SimpleMimeType::ANIMATION:
			groups.animation.emplace_back( record_id );
			break;
		case SimpleMimeType::ARCHIVE:
			groups.archive.emplace_back( record_id );
			break;
		case SimpleMimeType::NONE:
			break;
		default:
			log::warn(
				"Record {} has an unrecognised simple mime type {}", record_id, std::to_underlying( simple_type ) );
			break;
	}
}

static void addEmbeddedFlag(
	Json::Value& entry,
	const drogon::orm::Row& row,
	const char* const column,
	const Json::StaticString& key )
{
	if ( row[ column ].isNull() ) return;
	entry[ key ] = row[ column ].as< bool >();
}

//! Fills one field of a record's entry from a row of its type table.
using ApplyRow = void ( * )( Json::Value& entry, const drogon::orm::Row& row );

//! Runs \p query over \p record_ids and hands each returned row to \p apply along with the entry it
//! belongs to. A record whose type table has no row keeps its basic fields instead of failing.
static drogon::Task< void > applyTypeRows(
	DbClientPtr db,
	const char* const query,
	std::vector< RecordID > record_ids,
	const EntryIndex& index,
	std::vector< Json::Value >& entries,
	const ApplyRow apply )
{
	if ( record_ids.empty() ) co_return;

	const auto expected { record_ids.size() };
	const auto rows { co_await db->execSqlCoro( query, std::move( record_ids ) ) };

	if ( rows.size() != expected )
		log::warn( "Expected {} rows of file specific metadata but got {}", expected, rows.size() );

	for ( const auto& row : rows )
	{
		const auto itter { index.find( row[ "record_id" ].as< RecordID >() ) };
		if ( itter == index.end() ) continue;

		apply( entries[ itter->second ], row );
	}
}

//! Appends the archives each record is a member of. This cannot go through applyTypeRows: that helper
//! assumes one row per record, whereas a record may sit in several archives or none, and membership
//! is not tied to a record's own simple mime type. A record sitting at several paths in one archive
//! still names that archive once.
static drogon::Task< void > applyArchiveMembership(
	DbClientPtr db,
	std::vector< RecordID > record_ids,
	const EntryIndex& index,
	std::vector< Json::Value >& entries )
{
	if ( record_ids.empty() ) co_return;

	const auto rows { co_await db->execSqlCoro(
		"SELECT DISTINCT record_id, archive_id FROM archive_map "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[]) "
		"ORDER BY record_id, archive_id",
		std::move( record_ids ) ) };

	for ( const auto& row : rows )
	{
		const auto itter { index.find( row[ "record_id" ].as< RecordID >() ) };
		if ( itter == index.end() ) continue;

		entries[ itter->second ][ KEY_ARCHIVE_IDS ].append( row[ "archive_id" ].as< ArchiveID >() );
	}
}

drogon::Task< RecordInfoBatch > collectRecordInfo( std::vector< RecordID > record_ids, DbClientPtr db )
{
	RecordInfoBatch batch {};

	if ( record_ids.empty() ) co_return batch;

	const auto base { co_await db->execSqlCoro(
		"SELECT r.record_id, r.sha256, fi.size, fi.mime_id, m.name, m.best_extension, "
		"(EXTRACT(EPOCH FROM fi.modified_time) * 1000000)::BIGINT AS modified_time, "
		"md.simple_mime_type, md.json "
		"FROM records r "
		"LEFT JOIN file_info fi ON fi.record_id = r.record_id "
		"LEFT JOIN mime m ON m.mime_id = fi.mime_id "
		"LEFT JOIN metadata md ON md.record_id = r.record_id "
		"WHERE r.record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::forward< const std::vector< RecordID > >( record_ids ) ) };

	TypeGroups groups {};
	EntryIndex index {};
	index.reserve( base.size() );

	std::vector< Json::Value > entries {};
	entries.reserve( base.size() );

	for ( const auto& row : base )
	{
		const RecordID record_id { row[ "record_id" ].as< RecordID >() };

		Json::Value entry {};
		addBaseFields( entry, row, record_id );

		// A record with no file_info row is a bare record: it has a hash and nothing to parse.
		if ( !row[ "mime_id" ].isNull() ) addMetadataFields( entry, row, record_id, groups );

		index.emplace( record_id, entries.size() );
		entries.emplace_back( std::move( entry ) );
	}

	for ( const auto record_id : record_ids )
		if ( !index.contains( record_id ) ) batch.missing.emplace_back( record_id );

	co_await applyTypeRows(
		db,
		"SELECT record_id, width, height, channels, phash, has_exif, has_gps, has_xmp, has_iptc, has_icc_profile "
		"FROM image_metadata "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( groups.image ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_WIDTH ] = row[ "width" ].as< std::uint32_t >();
			entry[ KEY_HEIGHT ] = row[ "height" ].as< std::uint32_t >();
			entry[ KEY_CHANNELS ] = row[ "channels" ].as< std::uint32_t >();

			// A NULL flag is an image parsed before embedded metadata was looked for, which is not the
			// same as one known to carry none, so it is left off the entry entirely.
			addEmbeddedFlag( entry, row, "has_exif", KEY_HAS_EXIF );
			addEmbeddedFlag( entry, row, "has_gps", KEY_HAS_GPS );
			addEmbeddedFlag( entry, row, "has_xmp", KEY_HAS_XMP );
			addEmbeddedFlag( entry, row, "has_iptc", KEY_HAS_IPTC );
			addEmbeddedFlag( entry, row, "has_icc_profile", KEY_HAS_ICC_PROFILE );

			if ( row[ "phash" ].isNull() ) return;

			const auto phash { row[ "phash" ].as< std::string >() };
			if ( phash.size() != PHASH_BITS )
			{
				log::warn( "Ignoring image metadata pHash with invalid bit length {}", phash.size() );
				return;
			}

			constexpr char HEX[] { "0123456789abcdef" };
			std::string hex( PHASH_BITS / 4, '0' );
			for ( std::size_t i = 0; i < hex.size(); ++i )
			{
				std::uint8_t nibble { 0 };
				for ( std::size_t bit = 0; bit < 4; ++bit )
					nibble = static_cast< std::uint8_t >( ( nibble << 1 ) | ( phash[ ( i * 4 ) + bit ] == '1' ) );
				hex[ i ] = HEX[ nibble ];
			}
			entry[ KEY_PHASH ] = std::move( hex );
		} );

	co_await applyTypeRows(
		db,
		"SELECT record_id, width, height, duration, framerate, bitrate, has_audio FROM video_metadata "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( groups.video ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_WIDTH ] = row[ "width" ].as< std::uint32_t >();
			entry[ KEY_HEIGHT ] = row[ "height" ].as< std::uint32_t >();
			entry[ KEY_DURATION ] = row[ "duration" ].as< double >();
			entry[ KEY_FRAMERATE ] = row[ "framerate" ].as< double >();
			entry[ KEY_BITRATE ] = row[ "bitrate" ].as< std::uint32_t >();
			entry[ KEY_HAS_AUDIO ] = row[ "has_audio" ].as< bool >();
		} );

	co_await applyTypeRows(
		db,
		"SELECT record_id, width, height, channels, layers FROM image_project_metadata "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( groups.image_project ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_WIDTH ] = row[ "width" ].as< std::uint32_t >();
			entry[ KEY_HEIGHT ] = row[ "height" ].as< std::uint32_t >();
			entry[ KEY_CHANNELS ] = row[ "channels" ].as< SmallInt >();
			entry[ KEY_LAYERS ] = row[ "layers" ].as< SmallInt >();
		} );

	co_await applyTypeRows(
		db,
		"SELECT record_id, duration, bitrate, channels, sample_rate FROM audio_metadata "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( groups.audio ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_DURATION ] = row[ "duration" ].as< double >();
			entry[ KEY_BITRATE ] = row[ "bitrate" ].as< std::uint32_t >();
			entry[ KEY_CHANNELS ] = row[ "channels" ].as< SmallInt >();
			entry[ KEY_SAMPLE_RATE ] = row[ "sample_rate" ].as< std::uint32_t >();
		} );

	co_await applyTypeRows(
		db,
		"SELECT record_id, width, height, frame_count, duration, loops FROM animation_metadata "
		"WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( groups.animation ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_WIDTH ] = row[ "width" ].as< std::uint32_t >();
			entry[ KEY_HEIGHT ] = row[ "height" ].as< std::uint32_t >();
			entry[ KEY_FRAME_COUNT ] = row[ "frame_count" ].as< std::uint32_t >();
			entry[ KEY_DURATION ] = row[ "duration" ].as< double >();
			entry[ KEY_LOOPS ] = row[ "loops" ].as< bool >();
		} );

	co_await applyTypeRows(
		db,
		"SELECT am.record_id, am.archive_id, am.encrypted, count(map.record_id) AS file_count "
		"FROM archive_metadata am "
		"LEFT JOIN archive_map map ON map.archive_id = am.archive_id "
		"WHERE am.record_id = ANY($1::" RECORD_PG_TYPE_NAME "[]) "
		"GROUP BY am.record_id, am.archive_id, am.encrypted",
		std::move( groups.archive ),
		index,
		entries,
		[]( Json::Value& entry, const drogon::orm::Row& row )
		{
			entry[ KEY_ARCHIVE_ID ] = row[ "archive_id" ].as< ArchiveID >();
			entry[ KEY_ENCRYPTED ] = row[ "encrypted" ].as< bool >();
			entry[ KEY_FILE_COUNT ] = row[ "file_count" ].as< std::size_t >();
		} );

	co_await applyArchiveMembership( db, std::move( record_ids ), index, entries );

	for ( auto& entry : entries ) batch.records.append( std::move( entry ) );

	co_return batch;
}

} // namespace idhan::metadata
