//
// Created by kj16609 on 11/7/24.
//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "IDHANTypes.hpp"
#include "SearchStats.hpp"
#include "SearchTypes.hpp"
#include "Set.hpp"
#include "SetSource.hpp"
#include "api/APIAuth.hpp"
#include "db/dbTypes.hpp"
#include "drogon/utils/coroutine.h"
#include "threading/ExpectedTask.hpp"

namespace idhan
{

//! What a search returns. Hashes are populated only when the caller asked for them; ids always are,
//! since the algebra produces them either way.
struct SearchResults
{
	std::vector< RecordID > record_ids {};
	std::vector< SHA256 > hashes {};

	[[nodiscard]] std::size_t size() const noexcept { return record_ids.size(); }
};

/**
 * @brief Accumulates search criteria and answers them as a set algebra over record ids.
 *
 * Each term -- a tag, a resolved wildcard group, a namespace, a system predicate -- becomes one
 * search::Set fetched by one indexed query (see SetSource.hpp). Those Sets are then intersected,
 * unioned and subtracted in C++ rather than by a generated CTE chain, and the finished Set is
 * sliced to the requested page. Sort keys travel inside the Sets, so an ordinary search needs no
 * query at all after the terms are in hand.
 *
 * Negation never constructs the universe: it rides as a flag on the Set and is rewritten through De
 * Morgan at every operation. Only a result that is still inverted once the fold is done -- a search
 * made entirely of exclusions -- reaches the database again, as a `!= ALL(...)` on the page query.
 */
class SearchBuilder
{
	using SearchOperation = std::uint8_t;

	enum SearchOperationFlags : SearchOperation
	{
		GreaterThan = 1 << 0, // >
		LessThan = 1 << 1, // <
		Equal = 1 << 2, // =
		Not = 1 << 3, // !, ≠
		//! Its own bit rather than an alias of Equal: `~=` has to be distinguishable from `=` to
		//! render as a tolerance band, and aliasing them made `system:filesize ~= 50KB` an exact
		//! byte-for-byte match -- a search that essentially never returns anything.
		Approximate = 1 << 4, // ~, ≈

		// helpers
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
		//! > 0 tags
		HasTags,
		//! <= 0 tags
		NoTags,
		//! ?? N tags
		HasCount
	} m_has_tags_search { TagCountSearchType::DontCare };

	enum class ArchiveSearchType
	{
		DontCare = 0,
		InArchive,
		NoArchive
	} m_in_archive_search { ArchiveSearchType::DontCare };

	//! One comparison as it was parsed, before it is folded into a range: `> 1KB` is
	//! (GreaterThan, 1024).
	struct RangeTerm
	{
		SearchOperation operation { 0 };
		std::size_t value { 0 };
	};

	/**
	 * @brief The accumulated constraint on one numeric column.
	 *
	 * Every predicate naming the same column narrows the same pair of bounds, so `system:size > 1KB`
	 * with `system:size < 1MB` is the single range 1025..1048575 -- one indexed scan -- rather than
	 * whichever of the two happened to be parsed last, which is what a search built from a min/max
	 * pair used to collapse to. Two bounds on the same side keep the tighter one, so `< 1KB` with
	 * `< 1MB` is `< 1KB`.
	 */
	struct RangeSearchInfo
	{
		//! If true then these bounds are put into effect
		bool m_active { false };
		//! Inclusive, and nullopt for an unbounded side. `>` and `<` are folded to `>=` and `<=` as
		//! they are parsed, which is what lets narrowing be a plain max/min.
		std::optional< std::size_t > lower {};
		std::optional< std::size_t > upper {};
		//! Set by a term no value can satisfy on its own (`> SIZE_MAX`), which is not something a
		//! pair of bounds can express.
		bool m_unsatisfiable { false };
		//! Negated terms, as parsed. `!= 500` is the union of two intervals rather than one, so it
		//! cannot narrow the bounds; it is AND-ed on as its own conjunct at render time.
		std::vector< RangeTerm > negated {};

		//! True when no value can satisfy the accumulated constraint -- `< 1KB` together with
		//! `> 1MB`. The search holding it matches nothing, and does so before any query runs.
		[[nodiscard]] bool impossible() const noexcept
		{
			return m_unsatisfiable || ( lower && upper && *lower > *upper );
		}
	};

	//! Reads the operators and the number out of \p tag.
	//! \throws std::invalid_argument if it holds no number.
	static RangeTerm parseRangeTerm( std::string_view tag );

	//! Narrows \p target by \p term, keeping whichever bound is tighter on each side.
	static void narrowRange( RangeSearchInfo& target, RangeTerm term );

	static void parseRangeSearch( RangeSearchInfo& target, std::string_view tag );

	//! parseRangeSearch() plus the byte unit trailing the number, so `system:filesize < 1 GB` bounds
	//! at 1073741823 rather than 0. \throws std::invalid_argument if the unit is not one we know.
	static void parseFilesizeSearch( RangeSearchInfo& target, std::string_view tag );

	//! Renders \p operation and \p value as a SQL comparison against \p expression, e.g.
	//! `NOT (COALESCE(pim.width, pvm.width) >= 500)`. An Approximate operation widens \p value into
	//! a ±15% band first, so `~=` renders as a BETWEEN and `~>`/`~<` against the band's near edge.
	static std::string renderComparison( std::string_view expression, SearchOperation operation, std::size_t value );

	//! Renders accumulated \p bounds as one SQL condition over \p expression: `BETWEEN` when both
	//! sides are bounded, a single comparison when one is, and `FALSE` when they cross. Negated
	//! terms are AND-ed on after it.
	static std::string renderBounds( std::string_view expression, const RangeSearchInfo& bounds );

	//! Names the first predicate whose bounds cannot be satisfied, or nullopt when they all can. The
	//! name is for the log and the step label; what matters to the search is only that there is one,
	//! since a single unsatisfiable term empties the whole intersection.
	[[nodiscard]] std::optional< std::string_view > impossiblePredicate() const;

	//! Every system predicate that is actually applied, as the SQL that enumerates it. Predicates
	//! the parser accepts but has never implemented (filetype, date, url, notes, ...) contribute
	//! nothing here, exactly as before the rewrite.
	[[nodiscard]] std::vector< search::PredicateSource > buildPredicates() const;

	bool m_search_everything { false };

	RangeSearchInfo m_tag_count_search {};

	RangeSearchInfo m_width_search {};
	RangeSearchInfo m_height_search {};

	RangeSearchInfo m_limit_search {};

	RangeSearchInfo m_archive_search {};

	RangeSearchInfo m_filesize_search {};

	SortType m_sort_type { SortType::DEFAULT };
	SortOrder m_order { SortOrder::DEFAULT };

	//! Explicit result-window bounds set via the API. An explicit limit takes precedence over the
	//! system:limit predicate (m_limit_search).
	std::optional< std::size_t > m_limit {};
	std::optional< std::size_t > m_offset {};

	std::vector< TagID > m_positive_tags {};
	std::vector< TagID > m_negative_tags {};
	// Namespaces wildcards to search for ( `namespace:*` )
	std::vector< NamespaceID > m_namespace_ids {};

	//! Display text for the ids above, so a step label can say `+tag:'character:reimu'` rather than
	//! `+tag:6500`. Filled in as a side effect of the lookups that resolved the ids in the first
	//! place, so the usual search path already has every name it needs; ids that arrived without one
	//! (addPositiveTags and friends, taken by id) are looked up once in evaluate().
	std::unordered_map< TagID, std::string > m_tag_names {};
	std::unordered_map< NamespaceID, std::string > m_namespace_names {};

	//! One subtag wildcard (`cat*girl`) and every tag it resolved to. Within a group the tags are
	//! OR'd (any one of them satisfies the wildcard); across groups the results are combined the same
	//! way plain tags are — positives INTERSECT, negatives UNION.
	struct WildcardGroup
	{
		std::vector< TagID > tag_ids {};
		//! The pattern as it was typed, kept only so the step label can name it. Empty when the group
		//! was added by id alone, in which case the label falls back to the group's index.
		std::string pattern {};
	};

	std::vector< WildcardGroup > m_positive_wildcards {};
	std::vector< WildcardGroup > m_negative_wildcards {};

	//! How a wildcard term reads in a step label: the pattern it came from when the group carries one,
	//! and the group's position when it does not. \p sign is '+' or '-'.
	[[nodiscard]] static std::string wildcardLabel( char sign, const WildcardGroup& group, std::size_t index );

	HydrusDisplayType m_display_mode { HydrusDisplayType::DEFAULT };

	//! The effective page limit: an explicit setLimit() wins over a system:limit predicate.
	[[nodiscard]] std::optional< std::size_t > effectiveLimit() const;

	//! Per-step row counts for the most recent evaluate(). Shared rather than owned because every
	//! concurrent fetch records into it. Null until evaluate() runs.
	std::shared_ptr< search::SearchStats > m_stats {};

  public:

	SearchBuilder() = default;

	/**
	 * @brief Fetches every term and folds them into the search's answer.
	 *
	 * The returned Set is the whole result, in ascending composite order and not yet paged. It is
	 * inverted when the search consists only of exclusions, in which case it denotes everything
	 * @em except the ids it holds.
	 */
	[[nodiscard]] Task< search::Set > evaluate(
		DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool want_hashes );

	//! Runs the search and returns the requested page.
	//! \param tag_domain_ids Restricts mapping lookups; empty searches every domain.
	//! \param return_ids Retained for call-site compatibility; ids are always produced.
	//! \param return_hashes Populates SearchResults::hashes.
	[[nodiscard]] Task< SearchResults > query(
		DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool return_ids = true,
		bool return_hashes = false );

	//! The SQL for the unfiltered browse case: every record with a mime, in the configured sort,
	//! limit and offset. Exposed so the sort-type tests can pin an ordering without a drogon client.
	[[nodiscard]] std::string browseQuery( bool return_hashes = false ) const;

	//! Per-step row counts from the most recent evaluate()/query(). Null before either has run.
	//! Always written to the debug log; exposed here so an endpoint can also return it.
	[[nodiscard]] const std::shared_ptr< search::SearchStats >& stats() const noexcept { return m_stats; }

	//! Sets the column/metric results are ordered by.
	void setSortType( SortType type );

	//! Sets the ordering direction.
	void setSortOrder( SortOrder value );

	//! Limits the number of rows returned. Overrides any system:limit predicate. Pass nullopt to clear.
	void setLimit( std::optional< std::size_t > value );
	//! Skips this many leading rows. Pass nullopt or 0 for none.
	void setOffset( std::optional< std::size_t > value );

	//! Restricts the search to a tag domain; may be called repeatedly to allow several.
	void filterTagDomain( TagDomainID value );

	//! Restricts the search to records in the given file domain.
	void addFileDomain( FileDomainID value );

	//! Resolves tag strings to IDs and splits them into the positive/negative sets, also extracting
	//! any "system:" predicates. \return an error response if a tag cannot be resolved.
	Task< std::optional< drogon::HttpResponsePtr > > setTags( const std::vector< std::string >& tags );

	//! Resolves `namespace:*` wildcards to namespace IDs. \return an error response if a namespace
	//! cannot be resolved, or if the wildcard is negated (not supported yet).
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardNamespaces( const std::vector< std::string >& vector );

	//! Resolves subtag wildcards (`cat*girl`, `*:cat girl`, `cat girl*`) to the tags they match and
	//! adds each match set as a wildcard group. A leading `-` negates. \return an error response if
	//! a wildcard matches no existing tag.
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardTags( const std::vector< std::string >& vector );

	//! Translates a user-facing wildcard into a SQL LIKE pattern for `tags.tag_text`: `*` becomes
	//! `%`, and any `%`, `_` or `\` the user typed is backslash-escaped so it matches literally.
	//! LIKE is implicitly anchored at both ends, which is what makes `cat*girl` reject `cat girls`.
	[[nodiscard]] static std::string wildcardToLikePattern( std::string_view wildcard );

	//! Resolves \p wildcard against the tags table. Exposed so tests can pin the match set a pattern
	//! produces without standing up the coroutine/HTTP path around setWildcardTags(). $1 is the LIKE
	//! pattern from wildcardToLikePattern().
	static constexpr std::string_view wildcard_tag_query { "SELECT tag_id FROM tags WHERE tag_text LIKE $1" };

	//! Adds a resolved wildcard match set that a record must carry at least one of. \p pattern is the
	//! wildcard the set came from, used only to name the term in the search's step labels.
	void addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );
	//! Adds a resolved wildcard match set that a record must carry none of. \p pattern as above.
	void addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );

	//! Sets the tags a record must have.
	void addPositiveTags( std::vector< TagID > tag_ids );
	//! Sets the tags a record must not have.
	void addNegativeTags( std::vector< TagID > tag_ids );
	//! Sets the namespaces a record must carry at least one tag in (the `namespace:*` wildcard).
	void addNamespaces( std::vector< NamespaceID > namespace_ids );
	//! Parses a single Hydrus "system:" predicate into search criteria. \return false if unrecognised.
	[[nodiscard]] bool setHydrusSystemTags( std::string_view system_subtag );
	//! Parses IDHAN system predicates (width, height, filesize, limit, tag count, ...) from tag strings.
	void setSystemTags( const std::vector< std::string >& vector );

	//! Selects STORED vs DISPLAY (sibling/parent-resolved) mappings for the search.
	void setDisplay( HydrusDisplayType type );
};

} // namespace idhan
