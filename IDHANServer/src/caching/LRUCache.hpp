#pragma once
#include <cstddef>
#include <list>
#include <optional>
#include <type_traits>
#include <unordered_map>

// A byte-budgeted membership cache with exact LRU eviction: "have I seen this value recently?"
//
// The constructor argument is a *byte* budget, not an element count; the element capacity is that
// budget divided by the per-entry footprint (see kBytesPerEntry).
//
// Recency is exact: both add() and a successful exists() promote the value. Not thread safe.
template < typename T >
	requires std::is_trivially_copyable_v< T >
class ValidLRUCache
{
	// Per-entry memory model for libstdc++ + glibc malloc on a 64-bit target. Deliberate
	// over-estimates so the cache stays under the byte budget rather than creeping over it.
	static constexpr std::size_t kPtr { sizeof( void* ) };
	static constexpr std::size_t kAllocOverhead { 16 }; // malloc header + alignment, per allocation

	// std::list node: prev + next pointers + the value, in one allocation.
	static constexpr std::size_t kListNode { 2 * kPtr + sizeof( T ) + kAllocOverhead };
	// unordered_map node: bucket-chain next pointer + pair<const T, iterator (one pointer)>, one allocation.
	static constexpr std::size_t kMapNode { kPtr + sizeof( T ) + kPtr + kAllocOverhead };
	// unordered_map bucket array: ~one pointer per element at the default 1.0 max load factor.
	static constexpr std::size_t kMapBucket { kPtr };

	static constexpr std::size_t kBytesPerEntry { kListNode + kMapNode + kMapBucket };

	// most-recently-used at the front. list iterators stay valid across splice(), so promoting a
	// value never invalidates its map entry.
	std::size_t max_entries;
	std::list< T > order {};
	std::unordered_map< T, typename std::list< T >::iterator > index {};

  public:

	// A budget too small to hold even one entry yields a zero-capacity cache (add() becomes a no-op).
	explicit ValidLRUCache( const std::size_t byte_capacity ) : max_entries( byte_capacity / kBytesPerEntry )
	{
		index.reserve( max_entries );
	}

	// Refreshes `key` to most-recently-used. A miss leaves the cache unchanged — use add() to insert.
	bool exists( const T key )
	{
		const auto it { index.find( key ) };
		if ( it == index.end() ) return false;

		order.splice( order.begin(), order, it->second );
		return true;
	}

	// Inserts or refreshes `key`, evicting the least-recently-used value past capacity.
	void add( const T key )
	{
		if ( max_entries == 0 ) return;

		const auto it { index.find( key ) };
		if ( it != index.end() )
		{
			order.splice( order.begin(), order, it->second );
			return;
		}

		order.push_front( key );
		index.emplace( key, order.begin() );

		if ( order.size() > max_entries )
		{
			index.erase( order.back() );
			order.pop_back();
		}
	}

	[[nodiscard]] std::size_t size() const { return order.size(); }

	[[nodiscard]] std::size_t capacity() const { return max_entries; }

	// Estimated bytes one entry occupies (list node + map node + bucket slot).
	[[nodiscard]] static constexpr std::size_t bytesPerEntry() { return kBytesPerEntry; }

	void clear()
	{
		order.clear();
		index.clear();
	}
};

// ValidLRUCache, but storing a value alongside each key and handing it back on lookup. Not thread
// safe.
//
// The byte model counts only the fixed node footprint. For keys/values that own heap memory (e.g.
// std::string), the budget bounds the node count, not the total heap those nodes point at.
template < typename TKey, typename T >
class LRUCache
{
	using Node = std::pair< TKey, T >;

	static constexpr std::size_t kPtr { sizeof( void* ) };
	static constexpr std::size_t kAllocOverhead { 16 }; // malloc header + alignment, per allocation

	// std::list node: prev + next pointers + the {key, value} pair, in one allocation.
	static constexpr std::size_t kListNode { 2 * kPtr + sizeof( Node ) + kAllocOverhead };
	// unordered_map node: bucket-chain next pointer + key + iterator (one pointer), in one allocation.
	static constexpr std::size_t kMapNode { kPtr + sizeof( TKey ) + kPtr + kAllocOverhead };
	// unordered_map bucket array: ~one pointer per element at the default 1.0 max load factor.
	static constexpr std::size_t kMapBucket { kPtr };

	static constexpr std::size_t kBytesPerEntry { kListNode + kMapNode + kMapBucket };

	// most-recently-used at the front. list iterators stay valid across splice(), so promoting a node
	// never invalidates its map entry.
	std::size_t max_entries { 0 };
	std::list< Node > order {};
	std::unordered_map< TKey, typename std::list< Node >::iterator > index {};

  public:

	// A budget too small to hold even one entry yields a zero-capacity cache (add() becomes a no-op).
	explicit LRUCache( const std::size_t byte_capacity ) : max_entries( byte_capacity / kBytesPerEntry )
	{
		index.reserve( max_entries );
	}

	// Refreshes `key` to most-recently-used, or nullopt on a miss. A miss leaves the cache unchanged.
	std::optional< T > value( const TKey& key )
	{
		const auto it { index.find( key ) };
		if ( it == index.end() ) return std::nullopt;

		// splice keeps `it->second` valid
		order.splice( order.begin(), order, it->second );
		return it->second->second;
	}

	// Inserts or overwrites `key`, evicting the least-recently-used entry past capacity.
	void add( const TKey& key, T value )
	{
		if ( max_entries == 0 ) return;

		const auto it { index.find( key ) };
		if ( it != index.end() )
		{
			it->second->second = std::move( value ); // insert-or-assign
			order.splice( order.begin(), order, it->second );
			return;
		}

		order.emplace_front( key, std::move( value ) );
		index.emplace( key, order.begin() );

		if ( order.size() > max_entries )
		{
			index.erase( order.back().first );
			order.pop_back();
		}
	}

	[[nodiscard]] std::size_t size() const { return order.size(); }

	[[nodiscard]] std::size_t capacity() const { return max_entries; }

	// Estimated bytes one entry occupies (list node + map node + bucket slot).
	[[nodiscard]] static constexpr std::size_t bytesPerEntry() { return kBytesPerEntry; }

	void clear()
	{
		order.clear();
		index.clear();
	}
};