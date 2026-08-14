#include "SortKey.hpp"

namespace idhan::search
{

// Resolution is split across three mime-specific metadata tables; COALESCE picks whichever
// applies to the record's mime type. No single index can produce this cross-table order, so
// these four sort types always cost an explicit Sort -- see the note in table_file_info.
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

// NULLIF turns a zero height into a NULL ratio, so exclude_null drops those the same way it
// drops records with no resolution data at all.
constexpr std::string_view ratio_expr {
	"(COALESCE(im.width, vm.width, ipm.width)::FLOAT"
	" / NULLIF(COALESCE(im.height, vm.height, ipm.height), 0))"
};

// Timestamps become epoch microseconds rather than seconds: a whole-second key would collapse
// distinct import times into ties, and ties are resolved by record_id, which is not the order
// the caller asked for.
constexpr std::string_view store_time_expr { "(EXTRACT(EPOCH FROM fi.cluster_store_time) * 1000000)::BIGINT" };
constexpr std::string_view modified_time_expr { "(EXTRACT(EPOCH FROM fi.modified_time) * 1000000)::BIGINT" };
constexpr std::string_view creation_time_expr { "(EXTRACT(EPOCH FROM rc.creation_time) * 1000000)::BIGINT" };

constexpr std::string_view records_join { " JOIN records rc USING (record_id)" };
constexpr std::string_view video_join { " JOIN video_metadata vm USING (record_id)" };

constexpr std::string_view num_tags_join {
	" LEFT JOIN (SELECT record_id, COUNT(DISTINCT tag_id) AS tag_count"
	" FROM active_tag_mappings_final GROUP BY record_id) ntc USING (record_id)"
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
			// modified_time has no NOT NULL constraint -- it is unset until a record is actually
			// modified, and a record that never was has nothing to sort by here.
			return { {}, modified_time_expr, SortKeyType::Integer, true };
		case SortType::MIME:
			// the raw mime_id FK, not a semantic filetype-category ordering
			return { {}, "fi.mime_id", SortKeyType::Integer, false };
		case SortType::RECORD_TIME:
			return { records_join, creation_time_expr, SortKeyType::Integer, false };
		case SortType::HASH:
			return { records_join, "rc.sha256", SortKeyType::Hash, false };
		case SortType::DURATION:
			// INNER: a record with no video_metadata row is excluded from a duration-sorted search
			// rather than sorted last -- this sort doubles as a filter, as it did before the rewrite.
			return { video_join, "vm.duration", SortKeyType::Real, false };
		case SortType::FRAMERATE:
			return { video_join, "vm.framerate", SortKeyType::Real, false };
		case SortType::HAS_AUDIO:
			return { video_join, "vm.has_audio::INT", SortKeyType::Integer, false };
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
