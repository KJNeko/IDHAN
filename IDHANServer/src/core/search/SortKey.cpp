#include "SortKey.hpp"

#include "logging/format_ns.hpp"

namespace idhan::search
{

constexpr std::string_view resolution_joins {
	" LEFT JOIN image_metadata im USING (record_id)"
	" LEFT JOIN video_metadata vm USING (record_id)"
	" LEFT JOIN image_project_metadata ipm USING (record_id)"
};

constexpr std::string_view width_expr { "COALESCE(im.width, vm.width, ipm.width)" };
constexpr std::string_view height_expr { "COALESCE(im.height, vm.height, ipm.height)" };

// bigint casts prevent int*int overflow on large images (50000x50000 exceeds INT32_MAX).
constexpr std::string_view num_pixels_expr {
	"(COALESCE(im.width, vm.width, ipm.width)::BIGINT"
	" * COALESCE(im.height, vm.height, ipm.height)::BIGINT)"
};

constexpr std::string_view ratio_expr {
	"(COALESCE(im.width, vm.width, ipm.width)::FLOAT"
	" / NULLIF(COALESCE(im.height, vm.height, ipm.height), 0))"
};

constexpr std::string_view store_time_expr { "(EXTRACT(EPOCH FROM fi.cluster_store_time) * 1000000)::BIGINT" };
constexpr std::string_view modified_time_expr { "(EXTRACT(EPOCH FROM fi.modified_time) * 1000000)::BIGINT" };
constexpr std::string_view creation_time_expr { "(EXTRACT(EPOCH FROM rc.creation_time) * 1000000)::BIGINT" };

constexpr std::string_view records_join { " JOIN records rc USING (record_id)" };
constexpr std::string_view video_join { " LEFT JOIN video_metadata vm USING (record_id)" };

//! Spells out what active_tag_mappings_final holds instead of reading the view. The view splits on
//! ideal_tag_id so that point lookups match the partial indexes; that split costs the record_id
//! ordering the primary key gives this full scan, turning it into a seq scan plus a 14M row sort.
constexpr std::string_view num_tags_join {
	" LEFT JOIN (SELECT record_id, COUNT(DISTINCT tag_id) AS tag_count FROM ("
	" SELECT record_id, COALESCE(ideal_tag_id, tag_id) AS tag_id FROM active_tag_mappings"
	" UNION ALL SELECT record_id, tag_id FROM active_tag_mappings_parents"
	" ) atm GROUP BY record_id) ntc USING (record_id)"
};

SortKeySpec sortKeySpec( const SortType type )
{
	switch ( type )
	{
		default:
			[[fallthrough]];
		case SortType::FILESIZE:
			return { {}, "fi.size", SortKeyType::Integer, false };
		case SortType::IMPORT_TIME:
			return { {}, store_time_expr, SortKeyType::Integer, false };
		case SortType::MODIFIED_TIME:
			return { {}, modified_time_expr, SortKeyType::Integer, true };
		case SortType::MIME:
			// the raw mime_id FK, not a semantic filetype-category ordering
			return { {}, "fi.mime_id", SortKeyType::Integer, false };
		case SortType::RECORD_TIME:
			return { records_join, creation_time_expr, SortKeyType::Integer, false };
		case SortType::HASH:
			return { records_join, "rc.sha256", SortKeyType::Hash, false };
		case SortType::DURATION:
			// a record with no video metadata has no duration, which orders as zero rather than dropping out
			return { video_join, "COALESCE(vm.duration, 0)", SortKeyType::Real, false };
		case SortType::FRAMERATE:
			return { video_join, "COALESCE(vm.framerate, 0)", SortKeyType::Real, false };
		case SortType::HAS_AUDIO:
			// silent records and records with no video metadata both rank below records with audio
			return { video_join, "COALESCE(vm.has_audio::INT, 0)", SortKeyType::Integer, false };
		case SortType::WIDTH:
			return { resolution_joins, width_expr, SortKeyType::Integer, true };
		case SortType::HEIGHT:
			return { resolution_joins, height_expr, SortKeyType::Integer, true };
		case SortType::NUM_PIXELS:
			return { resolution_joins, num_pixels_expr, SortKeyType::Integer, true };
		case SortType::RATIO:
			return { resolution_joins, ratio_expr, SortKeyType::Real, true };
		case SortType::NUM_TAGS:
			// zero tags is a real answer, not missing data, so this one is never excluded
			return { num_tags_join, "COALESCE(ntc.tag_count, 0)", SortKeyType::Integer, false };
		case SortType::RANDOM:
			return { {}, {}, SortKeyType::None, false };
	}
}

std::string_view sortTypeName( const SortType type )
{
	switch ( type )
	{
		default:
			[[fallthrough]];
		case SortType::FILESIZE:
			return "filesize";
		case SortType::IMPORT_TIME:
			return "import time";
		case SortType::MODIFIED_TIME:
			return "modified time";
		case SortType::MIME:
			return "mime";
		case SortType::RECORD_TIME:
			return "record time";
		case SortType::HASH:
			return "hash";
		case SortType::DURATION:
			return "duration";
		case SortType::FRAMERATE:
			return "framerate";
		case SortType::HAS_AUDIO:
			return "has audio";
		case SortType::WIDTH:
			return "width";
		case SortType::HEIGHT:
			return "height";
		case SortType::NUM_PIXELS:
			return "num pixels";
		case SortType::RATIO:
			return "ratio";
		case SortType::NUM_TAGS:
			return "num tags";
		case SortType::RANDOM:
			return "random";
	}
}

std::string describeSortKey( const SortType type )
{
	const auto spec { sortKeySpec( type ) };

	return format_ns::format(
		"sort '{}' key={} joins={} excludes_null_keys={}",
		sortTypeName( type ),
		spec.expression.empty() ? "<none>" : spec.expression,
		spec.joins.empty() ? "<none>" : spec.joins,
		spec.exclude_null );
}

SortKeyColumn emptyColumn( const SortKeyType type )
{
	switch ( type )
	{
		default:
			[[fallthrough]];
		case SortKeyType::None:
			return std::monostate {};
		case SortKeyType::Integer:
			return std::vector< std::int64_t > {};
		case SortKeyType::Real:
			return std::vector< double > {};
		case SortKeyType::Hash:
			return std::vector< SHA256 > {};
	}
}

SortKeyColumn emptyColumnLike( const SortKeyColumn& like )
{
	return std::visit( []( const auto& held ) -> SortKeyColumn { return std::decay_t< decltype( held ) > {}; }, like );
}

SortKeyType columnType( const SortKeyColumn& column )
{
	return std::visit(
		[]( const auto& held ) -> SortKeyType
		{
			using Held = std::decay_t< decltype( held ) >;
			if constexpr ( std::same_as< Held, std::vector< std::int64_t > > )
				return SortKeyType::Integer;
			else if constexpr ( std::same_as< Held, std::vector< double > > )
				return SortKeyType::Real;
			else if constexpr ( std::same_as< Held, std::vector< SHA256 > > )
				return SortKeyType::Hash;
			else
				return SortKeyType::None;
		},
		column );
}

} // namespace idhan::search
