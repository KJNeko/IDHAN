#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace premade
{

//! What one graph declares about a tensor, without an onnxruntime session having been created.
struct GraphTensorInfo
{
	std::string m_name {};
	//! Declared dimensions. A zero entry is a dynamic axis (batch, sequence length).
	std::vector< std::int64_t > m_shape {};
};

//! Every declared input and output of an ONNX graph, in declaration order.
struct GraphInterface
{
	std::vector< GraphTensorInfo > m_inputs {};
	std::vector< GraphTensorInfo > m_outputs {};

	//! Finds a tensor by exact name. Null when the graph has no such tensor.
	[[nodiscard]] const GraphTensorInfo* findOutput( std::string_view name ) const
	{
		for ( const auto& output : m_outputs )
		{
			if ( output.m_name == name ) return &output;
		}

		return nullptr;
	}
};

//! Reads a graph's declared input and output without loading it.
/**
 *  \return the graph's first input and output, or a description of what made the file unreadable. */
[[nodiscard]] std::expected< GraphInterface, std::string > readGraphInterface( const std::filesystem::path& onnx_path );

} // namespace premade
