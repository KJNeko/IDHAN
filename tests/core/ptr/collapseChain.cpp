#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "ptr/flatten/CollapseChain.hpp"

using namespace idhan::hydrus::ptr;

//! Builds one key's chain from a compact spelling: 'A' add, 'D' delete. The nth character is
//! given update index n + 1, so the index a rule selects is unambiguous in the assertions.
std::vector< MappingEvent > chain( const std::string_view spelling )
{
	std::vector< MappingEvent > events {};
	std::uint16_t index { 1 };
	for ( const char c : spelling )
	{
		const auto op = c == 'A' ? EventOp::Add : EventOp::Delete;
		events.push_back( MappingEvent { 7, 9, index, static_cast< std::uint8_t >( op ), 0 } );
		++index;
	}
	std::ranges::sort( events, eventLess );
	return events;
}

TEST( PTRCollapseChain, EmptyChainCollapsesToNothing )
{
	EXPECT_FALSE( collapseChain( {} ).has_value() );
}

TEST( PTRCollapseChain, LoneAddSurvives )
{
	const auto events = chain( "A" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, LoneDeleteSurvives )
{
	// An orphan delete: PTR petitioned a mapping whose add is not in the retained corpus.
	const auto events = chain( "D" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, AddThenDeleteKeepsTheDelete )
{
	const auto events = chain( "AD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 2 );
}

TEST( PTRCollapseChain, AddDeleteAddKeepsTheOriginalAdd )
{
	// The rule that motivated the feature: a re-add is attributed to the first add, not the last.
	const auto events = chain( "ADA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, AddDeleteAddDeleteKeepsTheFinalDelete )
{
	const auto events = chain( "ADAD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 4 );
}

TEST( PTRCollapseChain, LongAlternatingChainKeepsTheOriginalAdd )
{
	const auto events = chain( "ADADA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, RepeatedAddsAreIdempotent )
{
	const auto events = chain( "AA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, RepeatedDeletesAreIdempotent )
{
	const auto events = chain( "DD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 2 );
}

TEST( PTRCollapseChain, DeleteWinsWhenBothShareAnUpdateIndex )
{
	std::vector< MappingEvent > events {
		MappingEvent { 7, 9, 5, static_cast< std::uint8_t >( EventOp::Delete ), 0 },
		MappingEvent { 7, 9, 5, static_cast< std::uint8_t >( EventOp::Add ), 0 },
	};
	std::ranges::sort( events, eventLess );

	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 5 );
}

TEST( PTRCollapseChain, DeleteThenAddKeepsTheAddAtItsOwnIndex )
{
	const auto events = chain( "DA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 2 );
}

