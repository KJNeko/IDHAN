//
// Created by kj16609 on 6/23/23.
//

#pragma once
#ifndef ATLASGAMEMANAGER_FUNCTIONDECOMP_HPP
#define ATLASGAMEMANAGER_FUNCTIONDECOMP_HPP

#include <concepts>
#include <tuple>

// Everything below here is just pure magic.
// I'll try to decipher how the hell this works someday....
template < typename Function >
struct FunctionDecomp;

template < typename Function >
struct FunctionDecomp : public FunctionDecomp< decltype( &std::remove_reference< Function >::type::operator() ) >
{};

template < typename ClassType, typename ReturnType, typename... Args >
struct FunctionDecomp< ReturnType ( ClassType::* )( Args... ) const > : FunctionDecomp< ReturnType ( * )( Args... ) >
{};

template < typename ClassType, typename ReturnType, typename... Args >
struct FunctionDecomp< ReturnType ( ClassType::* )( Args... ) > : FunctionDecomp< ReturnType ( * )( Args... ) >
{};

template < typename ClassType, typename ReturnType, typename... Args >
struct FunctionDecomp< ReturnType ( ClassType::* )( Args... ) noexcept > : FunctionDecomp< ReturnType ( * )( Args... ) >
{};

template < typename ClassType, typename ReturnType, typename... Args >
struct FunctionDecomp< ReturnType ( ClassType::* )( Args... ) const noexcept > :
  FunctionDecomp< ReturnType ( * )( Args... ) >
{};

template < typename ReturnType, typename... Args >
struct FunctionDecomp< ReturnType ( * )( Args... ) >
{
	using ResultType = ReturnType;

	using ArgTuple = std::tuple< Args... >;
	template < std::size_t Index >
	using arg = typename std::tuple_element< Index, ArgTuple >::type;
	static constexpr std::size_t arg_size = sizeof...( Args );
};

template < typename Func >
using FunctionReturn = FunctionDecomp< Func >::ResultType;

template < typename Func >
using FunctionTuple = FunctionDecomp< Func >::ArgTuple;
#endif //ATLASGAMEMANAGER_FUNCTIONDECOMP_HPP
