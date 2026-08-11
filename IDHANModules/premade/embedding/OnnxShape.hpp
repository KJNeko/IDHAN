//
// Created by kj16609 on 8/11/26.
//
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
/** All of them, not just the first, because picking the first is wrong for the models this loads.
 *  `vision_model.onnx` declares `last_hidden_state` ([batch, 196, 768] -- one row per patch) ahead of
 *  `pooler_output` ([batch, 768] -- the actual embedding). A loader taking output 0 would read a
 *  plausible trailing dimension of 768 off the wrong tensor and then embed patch grids. */
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
/** The host needs a model's output width before it can create that model's halfvec(N) column, and it
 *  asks for it during --describe -- which runs before startup() and is deliberately cheap, because a
 *  server enumerating its models must not pay session creation for each one. Creating a session here
 *  would mean reading 1.5 GB per model twice per server start.
 *
 *  This used to come from a hand-written model.json sidecar. That file cannot be required any more:
 *  setup is one `git clone` of an upstream repository, and upstream does not ship one. Its config.json
 *  is no help either -- the onnx-community configs omit hidden_size entirely and inherit it from
 *  transformers' Python-side class defaults, which are not in the repository at all.
 *
 *  So the shape is read from the only artifact that actually knows it: the graph. An ONNX file is a
 *  protobuf ModelProto, and the declaration we want sits in fields that can be reached by walking
 *  length-delimited records and stepping over the ones we do not want. The weights are by far the
 *  largest of those, and stepping over them is pointer arithmetic on a memory mapping -- their pages
 *  are never faulted in. Nothing here decodes a tensor.
 *
 *  \return the graph's first input and output, or a description of what made the file unreadable. */
[[nodiscard]] std::expected< GraphInterface, std::string > readGraphInterface( const std::filesystem::path& onnx_path );

} // namespace premade
