//
// Created by kj16609 on 5/11/22.
//

#ifndef IDHAN_CACHE_HPP
#define IDHAN_CACHE_HPP

#include <tuple>
#include <unordered_map>
#include <optional>
#include <functional>

#include <chrono>


template < typename Key, class... TArgs >
class Cache
{
	static constexpr bool useTuple = sizeof...( TArgs ) > 1;

	using Tuple = std::conditional_t<useTuple, std::tuple<TArgs...>, std::tuple_element_t<0, std::tuple<TArgs...>>>;

	std::unordered_map<Key, std::optional<Tuple>> cache{};
	std::unordered_map<Key, std::chrono::time_point<std::chrono::high_resolution_clock>> lastUsed{};


	std::function<std::optional<Tuple>( Key )> getterFunc {};

public:

	std::optional<Tuple> get( Key value )
	{
		auto it = cache.find( value );
		if ( it != cache.end())
		{
			auto lastUsedIt = lastUsed.find( value );
			if ( lastUsedIt != lastUsed.end())
			{
				lastUsedIt->second = std::chrono::high_resolution_clock::now();
			}
			else [[unlikely]]
			{
				lastUsed.emplace( value, std::chrono::high_resolution_clock::now());
			}

			return it->second.value();
		}
		else
		{
			return std::nullopt;
		}
	}

	size_t size()
	{
		return cache.size();
	}

	void place(Key value, Tuple tup)
	{
		cache.emplace(value, tup);
		lastUsed.emplace(value, std::chrono::high_resolution_clock::now());
	}

	void wipe()
	{
		cache.clear();
	}

	void cleanup( std::chrono::seconds seconds )
	{
		auto now = std::chrono::high_resolution_clock::now();

		for ( auto& [ key, timePoint ]: lastUsed )
		{
			if ( now - timePoint > seconds )
			{
				cache.erase( key );
				lastUsed.erase( key );
			}
		}
	}

};


#endif //IDHAN_CACHE_HPP
