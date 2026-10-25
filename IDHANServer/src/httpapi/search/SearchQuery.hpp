#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "SearchFetch.hpp"
#include "core/search/SearchStats.hpp"
#include "core/search/SearchTypes.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/Response.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi
{

struct SearchResults
{
	std::vector< RecordID > record_ids {};
	std::vector< SHA256 > hashes {};

	[[nodiscard]] std::size_t size() const noexcept { return record_ids.size(); }
};

class SearchQuery
{
	using SearchOperation = std::uint8_t;

	enum SearchOperationFlags : SearchOperation
	{
		GreaterThan = 1 << 0,
		LessThan = 1 << 1,
		Equal = 1 << 2,
		Not = 1 << 3,
		Approximate = 1 << 4,

		NotLessThan = Not | LessThan,
		NotGreaterThan = Not | GreaterThan,

		GreaterThanEqual = GreaterThan | Equal,
		LessThanEqual = LessThan | Equal,
		NotGreaterThanEqual = Not | GreaterThanEqual,
		NotLessThanEqual = Not | LessThanEqual,

		NotEqual = Not | Equal,
		ApproximateEqual = Approximate | Equal,
	};

	enum class DurationSearchType
	{
		DontCare = 0,
		HasDuration,
		NoDuration
	} m_duration_search { DurationSearchType::DontCare };

	enum class AudioSearchType
	{
		DontCare = 0,
		HasAudio,
		NoAudio
	} m_audio_search { AudioSearchType::DontCare };

	enum class EmbeddedSearchType
	{
		DontCare = 0,
		Has,
		No
	};

	EmbeddedSearchType m_exif_search { EmbeddedSearchType::DontCare };
	EmbeddedSearchType m_icc_profile_search { EmbeddedSearchType::DontCare };
	EmbeddedSearchType m_embedded_metadata_search { EmbeddedSearchType::DontCare };

	enum class TagCountSearchType
	{
		DontCare = 0,
		HasTags,
		NoTags,
		HasCount
	} m_has_tags_search { TagCountSearchType::DontCare };

	enum class ArchiveSearchType
	{
		DontCare = 0,
		InArchive,
		NoArchive
	} m_in_archive_search { ArchiveSearchType::DontCare };

	enum class IsArchiveSearchType
	{
		DontCare = 0,
		IsArchive,
		NotArchive
	} m_is_archive_search { IsArchiveSearchType::DontCare };

	struct RangeTerm
	{
		SearchOperation operation { 0 };
		std::size_t value { 0 };
	};

	struct RangeSearchInfo
	{
		bool m_active { false };
		std::optional< std::size_t > lower {};
		std::optional< std::size_t > upper {};
		bool m_unsatisfiable { false };
		std::vector< RangeTerm > negated {};

		[[nodiscard]] bool impossible() const noexcept
		{
			return m_unsatisfiable || ( lower && upper && *lower > *upper );
		}
	};

	static RangeTerm parseRangeTerm( std::string_view tag );

	struct MimeTerm
	{
		std::vector< std::string > names {};
		std::vector< MimeID > ids {};
		bool negated { false };
	};

	static void validateMimeName( std::string_view name );

	void parseMimeSearch( std::string_view arguments, bool by_id );

	static std::string renderMimeTerm( const MimeTerm& term );

	static std::string describeMimeTerm( const MimeTerm& term );

	struct NearbyTerm
	{
		RecordID record_id { 0 };
		std::size_t distance { 0 };
	};

	static NearbyTerm parseNearbySearch( std::string_view arguments );

	static std::string renderNearbyTerm( const NearbyTerm& term );

	static std::string describeNearbyTerm( const NearbyTerm& term );

	static SHA256 parseHashSearch( std::string_view arguments );

	void setHashSearch( std::string_view arguments );

	static void narrowRange( RangeSearchInfo& target, RangeTerm term );

	static void parseRangeSearch( RangeSearchInfo& target, std::string_view tag );

	static void parseFilesizeSearch( RangeSearchInfo& target, std::string_view tag );

	static std::string renderComparison( std::string_view expression, SearchOperation operation, std::size_t value );

	static std::string renderBounds( std::string_view expression, const RangeSearchInfo& bounds );

	[[nodiscard]] std::optional< std::string_view > impossiblePredicate() const;

	[[nodiscard]] std::vector< searchfetch::PredicateSource > buildPredicates() const;

	bool m_search_everything { false };

	RangeSearchInfo m_tag_count_search {};

	RangeSearchInfo m_width_search {};
	RangeSearchInfo m_height_search {};

	RangeSearchInfo m_limit_search {};

	RangeSearchInfo m_archive_search {};

	RangeSearchInfo m_filesize_search {};

	std::variant< std::monostate, SHA256 > m_hash_search {};

	std::vector< MimeTerm > m_mime_terms {};

	std::vector< NearbyTerm > m_nearby_terms {};

	RangeSearchInfo m_record_search {};

	SortType m_sort_type { SortType::DEFAULT };
	SortOrder m_order { SortOrder::DEFAULT };

	std::optional< std::size_t > m_limit {};
	std::optional< std::size_t > m_offset {};

	std::vector< TagID > m_positive_tags {};
	std::vector< TagID > m_negative_tags {};
	std::vector< NamespaceID > m_namespace_ids {};

	std::unordered_map< TagID, std::string > m_tag_names {};
	std::unordered_map< NamespaceID, std::string > m_namespace_names {};

	struct WildcardGroup
	{
		std::vector< TagID > tag_ids {};
		std::string pattern {};
	};

	std::vector< WildcardGroup > m_positive_wildcards {};
	std::vector< WildcardGroup > m_negative_wildcards {};

	[[nodiscard]] static std::string wildcardLabel( char sign, const WildcardGroup& group, std::size_t index );

	HydrusDisplayType m_display_mode { HydrusDisplayType::DEFAULT };
	bool m_include_deleted { false };

	[[nodiscard]] std::optional< std::size_t > effectiveLimit() const;

	std::shared_ptr< idhan::search::SearchStats > m_stats {};

	[[nodiscard]] runtime::Task< std::expected< std::unordered_map< std::string, TagID >, http::Response > >
		resolveTags( db::DBContext& db, std::vector< std::string > tags );

  public:

	SearchQuery() = default;

	[[nodiscard]] runtime::Task< idhan::search::Set > evaluate(
		db::DBContext& db,
		std::vector< TagDomainID > tag_domain_ids,
		bool want_hashes );

	[[nodiscard]] runtime::Task< SearchResults > query(
		db::DBContext& db,
		std::vector< TagDomainID > tag_domain_ids,
		bool return_ids = true,
		bool return_hashes = false );

	[[nodiscard]] const std::shared_ptr< idhan::search::SearchStats >& stats() const noexcept { return m_stats; }

	void setSortType( SortType type );

	void setSortOrder( SortOrder value );

	void setLimit( std::optional< std::size_t > value );
	void setOffset( std::optional< std::size_t > value );

	[[nodiscard]] runtime::Task< std::optional< http::Response > > setTags(
		db::DBContext& db,
		const std::vector< std::string >& tags );

	[[nodiscard]] runtime::Task< std::optional< http::Response > > setWildcardNamespaces(
		db::DBContext& db,
		const std::vector< std::string >& vector );

	[[nodiscard]] runtime::Task< std::optional< http::Response > > setWildcardTags(
		db::DBContext& db,
		const std::vector< std::string >& vector );

	[[nodiscard]] static std::string wildcardToLikePattern( std::string_view wildcard );

	static constexpr std::string_view wildcard_tag_query {
		"SELECT tag_id FROM tags WHERE tag_text LIKE CASEFOLD(normalize($1, NFC))"
	};

	void addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );
	void addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );

	void addPositiveTags( std::vector< TagID > tag_ids );
	void addNegativeTags( std::vector< TagID > tag_ids );
	void addNamespaces( std::vector< NamespaceID > namespace_ids );
	[[nodiscard]] bool setHydrusSystemTags( std::string_view system_subtag );
	void setSystemTags( const std::vector< std::string >& vector );

	void setDisplay( HydrusDisplayType type );
	void setIncludeDeleted( bool value );
};

} // namespace idhan::httpapi
