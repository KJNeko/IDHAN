#pragma once

#include "DownloaderResponses.hpp"
#include "db/subscriptions.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::httpapi
{
struct SubscriptionBody
{
	std::string tags {};
	std::map< std::string, std::string > parameters {};
	bool enabled { true };
};

struct SubscriptionBatchBody
{
	std::vector< SubscriptionBody > items;
};

struct SubscriptionBatchResult
{
	std::vector< SubscriptionID > ids;
	std::int64_t added {};
	std::int64_t skipped {};
};

struct SubscriptionRecordsResponse
{
	std::vector< RecordID > record_ids;
};

struct SubscriptionGroupBody
{
	std::string name;
	std::string generator { "gelbooru:search" };
	std::string origin {};
	std::int64_t interval_seconds { 86400 };
	bool enabled { true };
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/subscription_groups/{group_id}/subscriptions/batch"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionGroupID > >{"group_id"}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< SubscriptionBatchResult > > createSubscriptionBatch( db::DBContext& db, http::RequestContext context, http::JsonBody< SubscriptionBatchBody > body );

[[= http::Endpoint{proxygen::HTTPMethod::GET, "/subscriptions/{subscription_id}/records"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionID > >{"subscription_id"}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< SubscriptionRecordsResponse > > subscriptionRecords( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/subscriptions"}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< std::vector< db::Subscription > > > listSubscriptions( db::DBContext& db, http::RequestContext );

[[= http::Endpoint{proxygen::HTTPMethod::POST, "/subscription_groups/{group_id}/subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionGroupID > >{"group_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< db::Subscription, 201 > > createSubscription( db::DBContext& db, http::RequestContext context, http::JsonBody< SubscriptionBody > body );

[[= http::Endpoint{proxygen::HTTPMethod::PATCH, "/subscriptions/{subscription_id}"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionID > >{"subscription_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< db::Subscription > > updateSubscription( db::DBContext& db, http::RequestContext context, http::JsonBody< SubscriptionBody > body );

[[= http::Endpoint{proxygen::HTTPMethod::POST, "/subscriptions/{subscription_id}/run"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionID > >{"subscription_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< db::Subscription > > runSubscription( db::DBContext& db, http::RequestContext context );

[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/subscriptions/{subscription_id}"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionID > >{"subscription_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > deleteSubscription( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/subscription_groups"}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< std::vector< db::SubscriptionGroup > > > listSubscriptionGroups( db::DBContext& db, http::RequestContext );

[[= http::Endpoint{proxygen::HTTPMethod::POST, "/subscription_groups"}]]
[[= http::Group{"Subscriptions"}]]
http::HTTPTask< http::JsonResult< db::SubscriptionGroup, 201 > > createSubscriptionGroup( db::DBContext& db, http::JsonBody< SubscriptionGroupBody > body );

[[= http::Endpoint{proxygen::HTTPMethod::PATCH, "/subscription_groups/{group_id}"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionGroupID > >{"group_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< db::SubscriptionGroup > > updateSubscriptionGroup( db::DBContext& db, http::RequestContext context, http::JsonBody< SubscriptionGroupBody > body );

[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/subscription_groups/{group_id}"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionGroupID > >{"group_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< DownloaderDeletedResponse > > deleteSubscriptionGroup( db::DBContext& db, http::RequestContext context );

[[= http::Endpoint{proxygen::HTTPMethod::POST, "/subscription_groups/{group_id}/run"}]]
[[= http::Group{"Subscriptions"}]]
[[= http::Validation< http::validation::PositiveInteger< SubscriptionGroupID > >{"group_id"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< db::SubscriptionGroup > > runSubscriptionGroup( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
