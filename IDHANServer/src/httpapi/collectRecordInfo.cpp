#include "collectRecordInfo.hpp"

#include <bitset>
#include <format>
#include <span>
#include <unordered_map>

#include "idhan/SHA256.hpp"
#include "logging/log.hpp"

namespace idhan::httpapi::metadata
{

using EntryMap = std::unordered_map< RecordID, RecordInfoEntry >;

struct TypeGroups
{
	std::vector< RecordID > image {};
	std::vector< RecordID > video {};
	std::vector< RecordID > image_project {};
	std::vector< RecordID > audio {};
	std::vector< RecordID > animation {};
	std::vector< RecordID > archive {};
};

[[nodiscard]] static std::string sha256Hex( const std::span< const std::byte > bytes )
{
	return SHA256::fromBuffer( bytes ).hex();
}

[[nodiscard]] static std::string_view subsamplingName( const std::int16_t subsampling )
{
	switch ( subsampling )
	{
		case 1:
			return "greyscale";
		case 2:
			return "4:4:4";
		case 3:
			return "4:2:2";
		case 4:
			return "4:4:0";
		case 5:
			return "4:2:0";
		default:
			return "";
	}
}

[[nodiscard]] static std::string_view simpleTypeName( const SimpleMimeType type )
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

static void addToGroup( TypeGroups& groups, const SimpleMimeType type, const RecordID record_id )
{
	switch ( type )
	{
		case SimpleMimeType::IMAGE_TYPE:
			groups.image.push_back( record_id );
			break;
		case SimpleMimeType::VIDEO:
			groups.video.push_back( record_id );
			break;
		case SimpleMimeType::IMAGE_PROJECT:
			groups.image_project.push_back( record_id );
			break;
		case SimpleMimeType::AUDIO:
			groups.audio.push_back( record_id );
			break;
		case SimpleMimeType::ANIMATION:
			groups.animation.push_back( record_id );
			break;
		case SimpleMimeType::ARCHIVE:
			groups.archive.push_back( record_id );
			break;
		case SimpleMimeType::NONE:
			break;
		default:
			log::warn( "Record {} has an unrecognised simple mime type {}", record_id, std::to_underlying( type ) );
			break;
	}
}

//! Builds the initial EntryMap and sorts parsed records into TypeGroups.
static runtime::Task<> fetchBaseMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries,
	TypeGroups& groups )
{
	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT r.record_id, r.sha256, fi.size, fi.mime_id, m.name, m.best_extension, "
				"(EXTRACT(EPOCH FROM fi.file_mtime) * 1000000)::BIGINT AS file_mtime, "
				"(EXTRACT(EPOCH FROM fi.file_ctime) * 1000000)::BIGINT AS file_ctime, "
				"md.simple_mime_type, md.json::text AS json "
				"FROM records r "
				"LEFT JOIN file_info fi ON fi.record_id = r.record_id "
				"LEFT JOIN mime m ON m.mime_id = fi.mime_id "
				"LEFT JOIN metadata md ON md.record_id = r.record_id "
				"WHERE r.record_id = ANY($1)",
				std::move( bindings ) )
			.rows<
				RecordID,
				std::span< const std::byte >,
				std::optional< std::int64_t >,
				std::optional< MimeID >,
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< std::int64_t >,
				std::optional< std::int64_t >,
				std::optional< std::uint16_t >,
				std::optional< std::string > >()
	};

	for (
		const auto& [ record_id, sha256_bytes, size, mime_id, mime_name, best_extension, file_mtime, file_ctime, simple_mime_type, json_text ] :
		rows )
	{
		RecordInfoEntry entry {};
		entry.record_id = record_id;
		entry.sha256_hex = sha256Hex( sha256_bytes );

		if ( mime_id )
		{
			entry.size = size;
			entry.mime = mime_name;
			entry.extension = best_extension;
			entry.file_mtime = file_mtime;
			entry.file_ctime = file_ctime;

			if ( simple_mime_type )
			{
				entry.parsed = true;

				const auto type { static_cast< SimpleMimeType >( *simple_mime_type ) };
				entry.simple_type = std::string { simpleTypeName( type ) };
				entry.extra_json = json_text;

				addToGroup( groups, type, record_id );
			}
			else
				entry.parsed = false;
		}

		entries.emplace( record_id, std::move( entry ) );
	}
}

static runtime::Task<> fillImageMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, width, height, channels, phash, has_exif, has_gps, has_xmp, has_iptc, "
				"has_icc_profile, lossless, quality, subsampling, progressive, comment "
				"FROM image_metadata WHERE record_id = ANY($1)",
				std::move( bindings ) )
			.rows<
				RecordID,
				std::uint32_t,
				std::uint32_t,
				std::uint16_t,
				std::optional< std::bitset< 64 > >,
				std::optional< bool >,
				std::optional< bool >,
				std::optional< bool >,
				std::optional< bool >,
				std::optional< bool >,
				std::optional< bool >,
				std::optional< std::uint16_t >,
				std::optional< std::uint16_t >,
				std::optional< bool >,
				std::optional< std::string > >()
	};

	for (
		const auto& [ record_id, width, height, channels, phash, has_exif, has_gps, has_xmp, has_iptc, has_icc_profile, lossless, quality, subsampling, progressive, comment ] :
		rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.width = width;
		entry.height = height;
		entry.channels = channels;

		entry.has_exif = has_exif;
		entry.has_gps = has_gps;
		entry.has_xmp = has_xmp;
		entry.has_iptc = has_iptc;
		entry.has_icc_profile = has_icc_profile;
		entry.lossless = lossless;
		if ( quality ) entry.quality = *quality;
		entry.progressive = progressive;
		entry.comment = comment;

		if ( subsampling )
		{
			const auto name { subsamplingName( *subsampling ) };
			if ( !name.empty() ) entry.chroma_subsampling = std::string { name };
		}

		if ( phash ) entry.phash_hex = std::format( "{:016x}", phash->to_ullong() );
	}
}

static runtime::Task<> fillVideoMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, width, height, duration, framerate, bitrate, has_audio FROM video_metadata "
				"WHERE record_id = ANY($1)",
				std::move( bindings ) )
			.rows< RecordID, std::uint32_t, std::uint32_t, double, double, std::uint32_t, bool >()
	};

	for ( const auto& [ record_id, width, height, duration, framerate, bitrate, has_audio ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.width = width;
		entry.height = height;
		entry.duration = duration;
		entry.framerate = framerate;
		entry.bitrate = bitrate;
		entry.has_audio = has_audio;
	}
}

static runtime::Task<> fillImageProjectMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, width, height, channels, layers FROM image_project_metadata "
				"WHERE record_id = ANY($1)",
				std::move( bindings ) )
			.rows< RecordID, std::uint32_t, std::uint32_t, std::uint16_t, std::uint16_t >()
	};

	for ( const auto& [ record_id, width, height, channels, layers ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.width = width;
		entry.height = height;
		entry.channels = channels;
		entry.layers = layers;
	}
}

static runtime::Task<> fillAudioMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, duration, bitrate, channels, sample_rate FROM audio_metadata "
				"WHERE record_id = ANY($1)",
				std::move( bindings ) )
			.rows< RecordID, double, std::uint32_t, std::uint16_t, std::uint32_t >()
	};

	for ( const auto& [ record_id, duration, bitrate, channels, sample_rate ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.duration = duration;
		entry.bitrate = bitrate;
		entry.channels = channels;
		entry.sample_rate = sample_rate;
	}
}

static runtime::Task<> fillAnimationMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, width, height, frame_count, duration, loops FROM animation_metadata "
				"WHERE record_id = ANY($1)",
				std::move( bindings ) )
			.rows< RecordID, std::uint32_t, std::uint32_t, std::uint32_t, double, bool >()
	};

	for ( const auto& [ record_id, width, height, frame_count, duration, loops ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.width = width;
		entry.height = height;
		entry.frame_count = frame_count;
		entry.duration = duration;
		entry.loops = loops;
	}
}

static runtime::Task<> fillArchiveMetadata(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT am.record_id, am.archive_id, am.encrypted, count(map.record_id) AS file_count "
				"FROM archive_metadata am "
				"LEFT JOIN archive_map map ON map.archive_id = am.archive_id "
				"WHERE am.record_id = ANY($1) "
				"GROUP BY am.record_id, am.archive_id, am.encrypted",
				std::move( bindings ) )
			.rows< RecordID, ArchiveID, bool, std::int64_t >()
	};

	for ( const auto& [ record_id, archive_id, encrypted, file_count ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;
		auto& entry { it->second };

		entry.archive_id = archive_id;
		entry.encrypted = encrypted;
		entry.file_count = file_count;
	}
}

static runtime::Task<> fillArchiveMembership(
	db::DBContext& db,
	const std::vector< RecordID >& record_ids,
	EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT DISTINCT record_id, archive_id FROM archive_map "
				"WHERE record_id = ANY($1) ORDER BY record_id, archive_id",
				std::move( bindings ) )
			.rows< RecordID, ArchiveID >()
	};

	for ( const auto& [ record_id, archive_id ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;

		it->second.archive_ids.push_back( archive_id );
	}
}

static runtime::Task<> fillTags( db::DBContext& db, const std::vector< RecordID >& record_ids, EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT DISTINCT m.record_id, d.domain_name, t.tag_text "
				"FROM active_tag_mappings_final m "
				"JOIN tags t ON t.tag_id = m.tag_id "
				"JOIN tag_domains d ON d.tag_domain_id = m.tag_domain_id "
				"WHERE m.record_id = ANY($1) "
				"ORDER BY m.record_id, d.domain_name, t.tag_text",
				std::move( bindings ) )
			.rows< RecordID, std::string, std::string >()
	};

	for ( const auto& [ record_id, domain, text ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;

		it->second.tags.push_back( RecordTag { domain, text } );
	}
}

static runtime::Task<> fillUrls( db::DBContext& db, const std::vector< RecordID >& record_ids, EntryMap& entries )
{
	if ( record_ids.empty() ) co_return;

	db::DBBindings bindings {};
	bindings.bind( record_ids );

	const auto rows {
		co_await db
			.query(
				"SELECT record_id, url FROM url_mappings JOIN urls USING (url_id) "
				"WHERE record_id = ANY($1) ORDER BY record_id, url",
				std::move( bindings ) )
			.rows< RecordID, std::string >()
	};

	for ( const auto& [ record_id, url ] : rows )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() ) continue;

		it->second.urls.push_back( url );
	}
}

runtime::Task< RecordInfoBatch > collectRecordInfo( std::vector< RecordID > record_ids, db::DBContext& db )
{
	RecordInfoBatch batch {};

	if ( record_ids.empty() ) co_return batch;

	EntryMap entries {};
	entries.reserve( record_ids.size() );

	TypeGroups groups {};

	co_await fetchBaseMetadata( db, record_ids, entries, groups );

	co_await fillImageMetadata( db, groups.image, entries );
	co_await fillVideoMetadata( db, groups.video, entries );
	co_await fillImageProjectMetadata( db, groups.image_project, entries );
	co_await fillAudioMetadata( db, groups.audio, entries );
	co_await fillAnimationMetadata( db, groups.animation, entries );
	co_await fillArchiveMetadata( db, groups.archive, entries );
	co_await fillArchiveMembership( db, record_ids, entries );
	co_await fillTags( db, record_ids, entries );
	co_await fillUrls( db, record_ids, entries );

	batch.records.reserve( record_ids.size() );
	for ( const auto record_id : record_ids )
	{
		const auto it { entries.find( record_id ) };
		if ( it == entries.end() )
		{
			batch.missing.push_back( record_id );
			continue;
		}

		batch.records.push_back( it->second );
	}

	co_return batch;
}

} // namespace idhan::httpapi::metadata
