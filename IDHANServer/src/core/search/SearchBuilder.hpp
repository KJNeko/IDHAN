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

/**
 * @brief Accumulates search criteria and answers them as a set algebra over record ids.
 *
 * Each term becomes one search::Set fetched by one indexed query (see SetSource.hpp). Those Sets are
 * intersected, unioned and subtracted in C++ rather than by a generated CTE chain. Sort keys travel
 * inside the Sets, so an ordinary search needs no query once the terms are in hand.
 *
 * Negation never constructs the universe: it rides as a flag rewritten through De Morgan at every
 * operation. Only a search made entirely of exclusions reaches the database again, as a
 * `!= ALL(...)` on the page query.
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
		//! Its own bit rather than an alias of Equal: aliasing them made `system:filesize ~= 50KB` an
		//! exact byte-for-byte match.
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

	//! One comparison as parsed, before folding into a range: `> 1KB` is (GreaterThan, 1024).
	struct RangeTerm
	{
		SearchOperation operation { 0 };
		std::size_t value { 0 };
	};

	/**
	 * @brief The accumulated constraint on one numeric column.
	 *
	 * Every predicate naming the same column narrows the same pair of bounds, so `> 1KB` with `< 1MB`
	 * is one range and one indexed scan rather than whichever was parsed last. Two bounds on the same
	 * side keep the tighter one, so `< 1KB` with `< 1MB` is `< 1KB`.
	 */
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

	/**
	 * @brief Reads the hash out of \p arguments, the part of a hash predicate following its keyword.
	 *
	 * Exactly one hash, and only `=`. A list would read as OR, which the search cannot express since
	 * every term it holds is intersected, so accepting one would answer a different question than it
	 * appears to ask.
	 *
	 * \throws std::invalid_argument if it names no hash, more than one, a malformed one, a comparison
	 *         other than `=`, or an algorithm other than sha256 -- the only one stored.
	 */
	static SHA256 parseHashSearch( std::string_view arguments );

	//! parseHashSearch() plus the check that no earlier predicate already named a hash.
	//! \throws std::invalid_argument on the second one, which would otherwise silently intersect to
	//!         the empty set.
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

	/**
	 * @brief `system:sha256 = <hex>`, in the hash's own binary form.
	 *
	 * Stored as the 32 bytes rather than the 64 hex characters typed: the column it compares against
	 * is `bytea`. A variant rather than an optional because everything this will grow (md5, sha1) is
	 * another kind of hash with its own width and column, so an optional would have to become a
	 * variant the moment a second algorithm is stored.
	 */
	std::variant< std::monostate, SHA256 > m_hash_search {};

	//! `system:record = 1234`; nothing else addresses a single known record. A range rather than an
	//! equality so `system:record > 5000` falls out of the same code.
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
	//! OR'd; across groups they combine like plain tags -- positives INTERSECT, negatives UNION.
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

	//! The SQL for the unfiltered browse case. Exposed so the sort-type tests can pin an ordering
	//! without a drogon client.
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

	//! Resolves tag strings to IDs, splits them into the positive/negative sets, and extracts any
	//! `system:` predicates. \return an error response if a tag cannot be resolved.
	Task< std::optional< drogon::HttpResponsePtr > > setTags( const std::vector< std::string >& tags );

	//! Resolves `namespace:*` wildcards to namespace IDs. \return an error response if a namespace
	//! cannot be resolved, or if the wildcard is negated (not supported yet).
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardNamespaces( const std::vector< std::string >& vector );

	//! Resolves subtag wildcards (`cat*girl`, `*:cat girl`, `cat girl*`) to the tags they match, each
	//! as its own wildcard group. A leading `-` negates. \return an error response if a wildcard
	//! matches no existing tag.
	Task< std::optional< drogon::HttpResponsePtr > > setWildcardTags( const std::vector< std::string >& vector );

	//! Translates a user-facing wildcard into a SQL LIKE pattern for `tags.tag_text`: `*` becomes `%`,
	//! and any `%`, `_` or `\` the user typed is backslash-escaped. LIKE is implicitly anchored at
	//! both ends, which is what makes `cat*girl` reject `cat girls`.
	[[nodiscard]] static std::string wildcardToLikePattern( std::string_view wildcard );

	//! Exposed so tests can pin the match set a pattern produces without standing up the
	//! coroutine/HTTP path around setWildcardTags(). $1 is the LIKE pattern.
	static constexpr std::string_view wildcard_tag_query { "SELECT tag_id FROM tags WHERE tag_text LIKE $1" };

	//! A record must carry at least one of \p tag_ids. \p pattern names the term in step labels.
	void addPositiveWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );
	//! A record must carry none of \p tag_ids. \p pattern as above.
	void addNegativeWildcard( std::vector< TagID > tag_ids, std::string pattern = {} );

	void addPositiveTags( std::vector< TagID > tag_ids );
	void addNegativeTags( std::vector< TagID > tag_ids );
	//! Namespaces a record must carry at least one tag in (the `namespace:*` wildcard).
	void addNamespaces( std::vector< NamespaceID > namespace_ids );
	//! \return false if the predicate is unrecognised.
	[[nodiscard]] bool setHydrusSystemTags( std::string_view system_subtag );
	//! Parses IDHAN system predicates (width, height, filesize, limit, tag count, ...) from tag strings.
	void setSystemTags( const std::vector< std::string >& vector );

	//! Selects STORED vs DISPLAY (sibling/parent-resolved) mappings for the search.
	void setDisplay( HydrusDisplayType type );
};

} // namespace idhan
