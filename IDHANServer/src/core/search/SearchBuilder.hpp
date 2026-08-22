#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "SearchStats.hpp"
#include "SearchTypes.hpp"
#include "Set.hpp"
#include "SetSource.hpp"
#include "api/APIAuth.hpp"
#include "crypto/SHA256.hpp"
#include "db/dbTypes.hpp"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{

//! Hashes are populated only when the caller asked for them; ids always are.
struct SearchResults
{
	std::vector< RecordID > record_ids {};
	std::vector< SHA256 > hashes {};

	[[nodiscard]] std::size_t size() const noexcept { return record_ids.size(); }
};

//! Search terms become Sets and are combined in C++; negation stays as an inverted-set flag until a
//! page query has to materialize an all-exclusions search.
class SearchBuilder
{
	using SearchOperation = std::uint8_t;

	enum SearchOperationFlags : SearchOperation
	{
		GreaterThan = 1 << 0, // >
		LessThan = 1 << 1, // <
		Equal = 1 << 2, // =
		Not = 1 << 3, // !, ≠
		Approximate = 1 << 4, // ~, ≈

		NotLessThan = Not | LessThan, // !<
		NotGreaterThan = Not | GreaterThan, // !>

		GreaterThanEqual = GreaterThan | Equal, // >=
		LessThanEqual = LessThan | Equal, // <=
		NotGreaterThanEqual = Not | GreaterThanEqual, // !>=
		NotLessThanEqual = Not | LessThanEqual, // !<=

		NotEqual = Not | Equal, // !=
		ApproximateEqual = Approximate | Equal, // ~=
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

	enum class ExitSearchType
	{
		DontCare = 0,
		HasExif,
		NoExif
	} m_exif_search { ExitSearchType::DontCare };

	enum class TagCountSearchType
	{
		DontCare = 0,
		HasTags,
		NoTags,
		//! A count compared against m_tag_count_search.
		HasCount
	} m_has_tags_search { TagCountSearchType::DontCare };

	enum class ArchiveSearchType
	{
		DontCare = 0,
		InArchive,
		NoArchive
	} m_in_archive_search { ArchiveSearchType::DontCare };

	//! Whether the record is itself an archive, which archive_metadata records. Independent of
	//! ArchiveSearchType above: an archive can also sit inside another archive.
	enum class IsArchiveSearchType
	{
		DontCare = 0,
		IsArchive,
		NotArchive
	} m_is_archive_search { IsArchiveSearchType::DontCare };

	//! One comparison as parsed, before folding into a range: `> 1KB` is (GreaterThan, 1024).
	struct RangeTerm
	{
		SearchOperation operation { 0 };
		std::size_t value { 0 };
	};

	//! Accumulated constraint on one numeric column; repeated predicates keep tightening the bounds.
	struct RangeSearchInfo
	{
		bool m_active { false };
		//! Inclusive, nullopt for an unbounded side. `>` and `<` are folded to `>=`/`<=` as they are
		//! parsed, which lets narrowing be a plain max/min.
		std::optional< std::size_t > lower {};
		std::optional< std::size_t > upper {};
		//! Set by a term no value can satisfy on its own (`> SIZE_MAX`), which bounds cannot express.
		bool m_unsatisfiable { false };
		//! `!= 500` is the union of two intervals, so it cannot narrow the bounds; it is AND-ed on as
		//! its own conjunct at render time.
		std::vector< RangeTerm > negated {};

		//! True when no value can satisfy the accumulated constraint (`< 1KB` with `> 1MB`), decided
		//! before any query runs.
		[[nodiscard]] bool impossible() const noexcept
		{
			return m_unsatisfiable || ( lower && upper && *lower > *upper );
		}
	};

	//! Reads the operators and the number out of \p tag.
	//! \throws std::invalid_argument if it holds no number.
	static RangeTerm parseRangeTerm( std::string_view tag );

	//! One `system:mime` or `system:mime_id` term. Values inside a term are alternatives; separate
	//! terms intersect, so `mime = image/png` alongside `mime = image/jpeg` matches nothing.
	struct MimeTerm
	{
		//! Matched against mime.name, which is not unique: every id carrying the name qualifies, so
		//! `application/zip` also takes the Ugoira that reports it.
		std::vector< std::string > names {};
		std::vector< MimeID > ids {};
		bool negated { false };
	};

	//! Rejects anything outside the character set a mime name may use. The name reaches SQL as a bare
	//! literal, so this is what keeps a quote out of it.
	//! \throws std::invalid_argument
	static void validateMimeName( std::string_view name );

	//! Parses the arguments of `mime = a, b` / `mime_id != 3004`. \p by_id reads the values as ids
	//! rather than names. \throws std::invalid_argument
	void parseMimeSearch( std::string_view arguments, bool by_id );

	//! `fi.mime_id IN (...)` over \p term's names, ids, or both, inverted when it is negated.
	static std::string renderMimeTerm( const MimeTerm& term );

	//! How \p term reads back in a step label, e.g. `system:mime = application/zip`.
	static std::string describeMimeTerm( const MimeTerm& term );

	//! Accepts exactly one `sha256 = <hash>` predicate; hash OR-lists are not representable here.
	static SHA256 parseHashSearch( std::string_view arguments );

	//! Rejects a second hash predicate, which would otherwise silently intersect to empty.
	void setHashSearch( std::string_view arguments );

	//! Narrows \p target by \p term, keeping whichever bound is tighter on each side.
	static void narrowRange( RangeSearchInfo& target, RangeTerm term );

	static void parseRangeSearch( RangeSearchInfo& target, std::string_view tag );

	//! parseRangeSearch() plus the byte unit trailing the number, so `system:filesize < 1 GB` bounds
	//! at 1073741823 rather than 0. \throws std::invalid_argument on an unknown unit.
	static void parseFilesizeSearch( RangeSearchInfo& target, std::string_view tag );

	//! Renders \p operation and \p value as a SQL comparison against \p expression. An Approximate
	//! operation widens \p value into a ±15% band first, so `~=` renders as a BETWEEN and `~>`/`~<`
	//! against the band's near edge.
	static std::string renderComparison( std::string_view expression, SearchOperation operation, std::size_t value );

	//! Renders accumulated \p bounds as one SQL condition over \p expression: `BETWEEN` when both
	//! sides are bounded, a single comparison when one is, and `FALSE` when they cross. Negated terms
	//! are AND-ed on after it.
	static std::string renderBounds( std::string_view expression, const RangeSearchInfo& bounds );

	//! Names the first predicate whose bounds cannot be satisfied, for the log and step label. A
	//! single unsatisfiable term empties the whole intersection.
	[[nodiscard]] std::optional< std::string_view > impossiblePredicate() const;

	//! Every system predicate actually applied, as the SQL that enumerates it. Predicates the parser
	//! accepts but has never implemented (filetype, date, url, notes, ...) contribute nothing.
	[[nodiscard]] std::vector< search::PredicateSource > buildPredicates() const;

	bool m_search_everything { false };

	RangeSearchInfo m_tag_count_search {};

	RangeSearchInfo m_width_search {};
	RangeSearchInfo m_height_search {};

	RangeSearchInfo m_limit_search {};

	RangeSearchInfo m_archive_search {};

	RangeSearchInfo m_filesize_search {};

	//! `system:sha256 = <hex>`, stored in bytea form.
	std::variant< std::monostate, SHA256 > m_hash_search {};

	//! One entry per `system:mime`/`system:mime_id` term, each rendered as its own predicate.
	std::vector< MimeTerm > m_mime_terms {};

	//! `system:record = 1234`; nothing else addresses a single known record.
	RangeSearchInfo m_record_search {};

	SortType m_sort_type { SortType::DEFAULT };
	SortOrder m_order { SortOrder::DEFAULT };

	//! Explicit result-window bounds set via the API, taking precedence over m_limit_search.
	std::optional< std::size_t > m_limit {};
	std::optional< std::size_t > m_offset {};

	std::vector< TagID > m_positive_tags {};
	std::vector< TagID > m_negative_tags {};
	//! `namespace:*` wildcards.
	std::vector< NamespaceID > m_namespace_ids {};

	//! Display text for the ids above, so a step label can say `+tag:'character:reimu'` rather than
	//! `+tag:6500`. Filled in as a side effect of the lookups that resolved the ids; ids that arrived
	//! without one (addPositiveTags and friends) are looked up once in evaluate().
	std::unordered_map< TagID, std::string > m_tag_names {};
	std::unordered_map< NamespaceID, std::string > m_namespace_names {};

	//! One subtag wildcard (`cat*girl`) and every tag it resolved to. Within a group the tags are
	//! OR'd; across groups they combine like plain tags: positives INTERSECT, negatives UNION.
	struct WildcardGroup
	{
		std::vector< TagID > tag_ids {};
		//! Kept only so the step label can name it. Empty when the group was added by id alone, in
		//! which case the label falls back to the group's index.
		std::string pattern {};
	};

	std::vector< WildcardGroup > m_positive_wildcards {};
	std::vector< WildcardGroup > m_negative_wildcards {};

	//! How a wildcard term reads in a step label: its pattern when the group carries one, otherwise
	//! the group's position. \p sign is '+' or '-'.
	[[nodiscard]] static std::string wildcardLabel( char sign, const WildcardGroup& group, std::size_t index );

	HydrusDisplayType m_display_mode { HydrusDisplayType::DEFAULT };

	//! An explicit setLimit() wins over a system:limit predicate.
	[[nodiscard]] std::optional< std::size_t > effectiveLimit() const;

	//! Per-step row counts for the most recent evaluate(). Shared rather than owned because every
	//! concurrent fetch records into it. Null until evaluate() runs.
	std::shared_ptr< search::SearchStats > m_stats {};

  public:

	SearchBuilder() = default;

	//! Whole unpaged result; inverted only when the search consists entirely of exclusions.
	[[nodiscard]] Task< search::Set > evaluate(
		DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool want_hashes );

	[[nodiscard]] Task< SearchResults > query(
		DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool return_ids = true,
		bool return_hashes = false );

	//! The SQL for the unfiltered browse case.
	[[nodiscard]] std::string browseQuery( bool return_hashes = false ) const;

	//! Null before evaluate()/query() has run. Always written to the debug log; exposed here so an
	//! endpoint can also return it.
	[[nodiscard]] const std::shared_ptr< search::SearchStats >& stats() const noexcept { return m_stats; }

	void setSortType( SortType type );

	void setSortOrder( SortOrder value );

	//! Overrides any system:limit predicate. Pass nullopt to clear.
	void setLimit( std::optional< std::size_t > value );
	//! Pass nullopt or 0 for none.
	void setOffset( std::optional< std::size_t > value );

	//! May be called repeatedly to allow several domains.
	void filterTagDomain( TagDomainID value );

	void addFileDomain( FileDomainID value );

	//! Resolves tag strings to IDs, splits them into sets, and extracts `system:` predicates.
	//! Errors if a tag cannot be resolved.
	Task< std::optional< drogon::HttpResponsePtr > > setTags( const std::vector< std::string >& tags );

	//! Errors if a namespace cannot be resolved, or if the wildcard is negated (not supported yet).
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardNamespaces( const std::vector< std::string >& vector );

	//! A wildcard that matches no existing tag is an error. A leading `-` negates.
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardTags( const std::vector< std::string >& vector );

	//! Translates a user-facing wildcard into a SQL LIKE pattern for `tags.tag_text`: `*` becomes `%`,
	//! and any `%`, `_` or `\` the user typed is backslash-escaped. LIKE is implicitly anchored at
	//! both ends, which is what makes `cat*girl` reject `cat girls`.
	[[nodiscard]] static std::string wildcardToLikePattern( std::string_view wildcard );

	static constexpr std::string_view wildcard_tag_query {
		"SELECT tag_id FROM tags WHERE tag_text LIKE CASEFOLD(normalize($1, NFC))"
	};

	//! A record must carry at least one of \p tag_ids. \p pattern names the term in step labels.
	void addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );
	//! A record must carry none of \p tag_ids. \p pattern as above.
	void addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );

	void addPositiveTags( std::vector< TagID > tag_ids );
	void addNegativeTags( std::vector< TagID > tag_ids );
	//! Namespaces a record must carry at least one tag in (the `namespace:*` wildcard).
	void addNamespaces( std::vector< NamespaceID > namespace_ids );
	//! False if the predicate is unrecognised.
	[[nodiscard]] bool setHydrusSystemTags( std::string_view system_subtag );
	//! Parses IDHAN system predicates (width, height, filesize, limit, tag count, ...) from tag strings.
	void setSystemTags( const std::vector< std::string >& vector );

	//! Selects STORED vs DISPLAY (sibling/parent-resolved) mappings for the search.
	void setDisplay( HydrusDisplayType type );
};

} // namespace idhan
