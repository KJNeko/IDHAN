#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

#include "db/subscriptions.hpp"
#include "idhan/http/response/ReflectedResponses.hpp"

namespace idhan::subscriptions
{
// Partial objects deliberately use the same field names and null semantics as REST responses.
template < typename T >
[[nodiscard]] std::optional< http::JsonDocument > changedFields( const T* previous, const T& current )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	bool first { true };
	bool changed { previous == nullptr };
	template for (
		constexpr auto member :
		std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
	{
		constexpr auto name { http::requestFieldName< member >() };
		const bool different { previous == nullptr || previous->[:member:] != current.[:member:] };
		if ( different || name == "id" )
		{
			if ( !first ) builder.append_comma();

			first = false;
			changed = changed || different;
			builder.escape_and_append_with_quotes( name );
			builder.append_colon();
			auto appended_e { http::appendResponseJson( builder, current.[:member:] ) };
			if ( !appended_e.has_value() ) throw std::runtime_error( appended_e.error().message );
		}
	}
	builder.end_object();
	if ( !changed ) return std::nullopt;

	std::string_view text;
	const auto error { builder.view().get( text ) };
	if ( error != simdjson::SUCCESS ) throw std::runtime_error( "Unable to serialize subscription patch" );

	return http::JsonDocument { std::string { text } };
}

template < typename T >
[[nodiscard]] http::ValidatedJson subscriptionJson( const T& value )
{
	auto text_e { http::serializeJson( value ) };
	if ( !text_e.has_value() ) throw std::runtime_error( text_e.error().message );

	auto json_e { http::ValidatedJson::parse( std::move( text_e.value() ) ) };
	if ( !json_e.has_value() ) throw std::runtime_error( json_e.error().message );

	return std::move( json_e.value() );
}

class SubscriptionEventState
{
	std::map< SubscriptionID, db::Subscription > m_items {};
	std::map< SubscriptionGroupID, db::SubscriptionGroup > m_groups {};

	template < typename ID, typename T >
	static void reconcileRows(
		std::map< ID, T >& stored,
		std::vector< T > rows,
		std::vector< http::JsonDocument >& patches,
		std::vector< ID >& removed,
		std::optional< SubscriptionGroupID > group_id )
	{
		std::map< ID, T > next;
		for ( auto& row : rows )
		{
			const auto previous { stored.find( row.id ) };
			auto patch { changedFields( previous == stored.end() ? nullptr : &previous->second, row ) };
			if ( patch.has_value() ) patches.push_back( std::move( patch.value() ) );

			next.emplace( row.id, std::move( row ) );
		}
		for ( auto it { stored.begin() }; it != stored.end(); )
		{
			bool in_scope { true };
			if ( group_id.has_value() )
			{
				if constexpr ( std::same_as< T, db::Subscription > )
					in_scope = it->second.group_id == group_id.value();
				else
					in_scope = it->first == group_id.value();
			}

			if ( in_scope && !next.contains( it->first ) )
			{
				removed.push_back( it->first );
				it = stored.erase( it );
			}
			else
				++it;
		}
		for ( auto& [ id, row ] : next ) stored.insert_or_assign( id, std::move( row ) );
	}

  public:

	// A group scope also detects cascade deletions without emitting every remaining entity.
	[[nodiscard]] std::vector< http::ValidatedJson > reconcile(
		std::vector< db::SubscriptionGroup > groups,
		std::vector< db::Subscription > items,
		std::optional< SubscriptionGroupID > group_id = std::nullopt )
	{
		struct Patch
		{
			std::string type { "patch" };
			http::possiblyMissing< std::vector< http::JsonDocument > > groups {};
			http::possiblyMissing< std::vector< http::JsonDocument > > items {};
		};

		struct Removal
		{
			std::string type { "remove" };
			http::possiblyMissing< std::vector< SubscriptionGroupID > > groups {};
			http::possiblyMissing< std::vector< SubscriptionID > > items {};
		};

		std::vector< http::JsonDocument > group_patches, item_patches;
		std::vector< SubscriptionGroupID > removed_groups;
		std::vector< SubscriptionID > removed_items;
		reconcileRows( m_groups, std::move( groups ), group_patches, removed_groups, group_id );
		reconcileRows( m_items, std::move( items ), item_patches, removed_items, group_id );
		Patch patch {};
		const bool changed { !group_patches.empty() || !item_patches.empty() };
		if ( !group_patches.empty() ) patch.groups = std::move( group_patches );

		if ( !item_patches.empty() ) patch.items = std::move( item_patches );

		std::vector< http::ValidatedJson > events;
		if ( !removed_groups.empty() || !removed_items.empty() )
		{
			Removal removal {};
			if ( !removed_groups.empty() ) removal.groups = std::move( removed_groups );

			if ( !removed_items.empty() ) removal.items = std::move( removed_items );

			events.push_back( subscriptionJson( removal ) );
		}

		if ( changed ) events.push_back( subscriptionJson( patch ) );

		return events;
	}

	[[nodiscard]] http::ValidatedJson snapshot() const
	{
		struct Snapshot
		{
			std::string type { "snapshot" };
			std::vector< db::SubscriptionGroup > groups {};
			std::vector< db::Subscription > items {};
		};

		Snapshot result {};
		for ( const auto& [ id, group ] : m_groups ) result.groups.push_back( group );
		for ( const auto& [ id, item ] : m_items ) result.items.push_back( item );
		return subscriptionJson( result );
	}
};
} // namespace idhan::subscriptions
