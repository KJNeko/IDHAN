#pragma once

#include <arpa/inet.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "db/bandwidth.hpp"

namespace idhan::downloader
{
//! Group DNS names by suffix, stopping before the top-level label. This is a
//! display hierarchy, not a public-suffix/registrable-domain classification.
inline std::vector< std::string > bandwidthHostPath( const std::string& host )
{
	in_addr ipv4 {};
	in6_addr ipv6 {};
	if ( inet_pton( AF_INET, host.c_str(), &ipv4 ) == 1 ) return { host };

	if ( host.starts_with( '[' ) ) return { host };

	if ( inet_pton( AF_INET6, host.c_str(), &ipv6 ) == 1 ) return { host };

	std::vector< std::string > path { host };
	auto dot { host.find( '.' ) };
	while ( dot != std::string::npos )
	{
		const auto suffix { host.substr( dot + 1 ) };
		if ( !suffix.contains( '.' ) ) break;

		path.push_back( suffix );
		dot = host.find( '.', dot + 1 );
	}
	std::ranges::reverse( path );
	return path;
}

template < typename Node >
std::vector< Node > bandwidthTree( const std::vector< db::HostBandwidth >& totals )
{
	std::vector< Node > roots;
	for ( const auto& total : totals )
	{
		auto* level { &roots };
		Node* node {};
		for ( const auto& host : bandwidthHostPath( total.host ) )
		{
			const auto found { std::ranges::lower_bound( *level, host, {}, &Node::host ) };
			if ( found == level->end() || found->host != host )
				node = &*level->insert( found, Node { .host = host } );
			else
				node = &*found;

			level = &node->children;
		}
		// Never roll totals up or down the tree: only the exact host owns them.
		node->bytes_received = total.bytes_received;
	}
	return roots;
}
} // namespace idhan::downloader
