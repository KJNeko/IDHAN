#include "OnnxShape.hpp"

#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>
#include <fcntl.h>
#include <format>
#include <span>
#include <unistd.h>

namespace premade
{

//! Protobuf wire types. Only these three appear on the path we walk.
constexpr std::uint8_t WIRE_VARINT { 0 };
constexpr std::uint8_t WIRE_64BIT { 1 };
constexpr std::uint8_t WIRE_LENGTH { 2 };
constexpr std::uint8_t WIRE_32BIT { 5 };

// Field numbers from onnx.proto. Named rather than inlined because a bare `7` in this file would be
// unreadable and, worse, unverifiable against the schema.
constexpr std::uint64_t MODEL_GRAPH { 7 }; //!< ModelProto.graph
constexpr std::uint64_t GRAPH_INPUT { 11 }; //!< GraphProto.input
constexpr std::uint64_t GRAPH_OUTPUT { 12 }; //!< GraphProto.output
constexpr std::uint64_t VALUEINFO_NAME { 1 }; //!< ValueInfoProto.name
constexpr std::uint64_t VALUEINFO_TYPE { 2 }; //!< ValueInfoProto.type
constexpr std::uint64_t TYPE_TENSOR { 1 }; //!< TypeProto.tensor_type
constexpr std::uint64_t TENSORTYPE_SHAPE { 2 }; //!< TypeProto.Tensor.shape
constexpr std::uint64_t SHAPE_DIM { 1 }; //!< TensorShapeProto.dim
constexpr std::uint64_t DIM_VALUE { 1 }; //!< TensorShapeProto.Dimension.dim_value

//! A cursor over a protobuf message body.
/** Every read is bounds-checked and reports failure rather than throwing, because the input is a file
 *  from the internet: a truncated download must produce "this model is unreadable, skipping it" and
 *  not a crash inside a module worker. */
class Reader
{
	const std::byte* m_cursor { nullptr };
	const std::byte* m_end { nullptr };

  public:

	Reader( const std::byte* const begin, const std::byte* const end ) : m_cursor( begin ), m_end( end ) {}

	[[nodiscard]] bool done() const { return m_cursor >= m_end; }

	[[nodiscard]] bool readVarint( std::uint64_t& out )
	{
		std::uint64_t value { 0 };
		int shift { 0 };

		while ( m_cursor < m_end )
		{
			const auto byte { static_cast< std::uint8_t >( *m_cursor++ ) };
			value |= static_cast< std::uint64_t >( byte & 0x7F ) << shift;

			if ( ( byte & 0x80 ) == 0 )
			{
				out = value;
				return true;
			}

			shift += 7;
			// A varint longer than ten groups cannot fit in 64 bits. Refusing here stops a corrupt
			// file from shifting past the width of the type.
			if ( shift > 63 ) return false;
		}

		return false;
	}

	//! Reads one field header. \return false at end of message or on a malformed tag.
	[[nodiscard]] bool readTag( std::uint64_t& field, std::uint8_t& wire )
	{
		std::uint64_t tag { 0 };
		if ( !readVarint( tag ) ) return false;

		field = tag >> 3;
		wire = static_cast< std::uint8_t >( tag & 0x07 );
		return field != 0;
	}

	//! Positions a sub-reader over a length-delimited field's body and steps past it.
	[[nodiscard]] bool readSubMessage( Reader& out )
	{
		std::uint64_t length { 0 };
		if ( !readVarint( length ) ) return false;

		if ( static_cast< std::uint64_t >( m_end - m_cursor ) < length ) return false;

		out = Reader { m_cursor, m_cursor + length };
		m_cursor += length;
		return true;
	}

	//! Steps over a field whose contents are not wanted.
	/** This is what makes the walk cheap: an initializer holding hundreds of megabytes of weights is
	 *  skipped by advancing the cursor, so the mapping's pages are never touched. */
	[[nodiscard]] bool skip( const std::uint8_t wire )
	{
		switch ( wire )
		{
			case WIRE_VARINT:
				{
					std::uint64_t ignored { 0 };
					return readVarint( ignored );
				}
			case WIRE_64BIT:
				if ( m_end - m_cursor < 8 ) return false;
				m_cursor += 8;
				return true;
			case WIRE_32BIT:
				if ( m_end - m_cursor < 4 ) return false;
				m_cursor += 4;
				return true;
			case WIRE_LENGTH:
				{
					std::uint64_t length { 0 };
					if ( !readVarint( length ) ) return false;
					if ( static_cast< std::uint64_t >( m_end - m_cursor ) < length ) return false;
					m_cursor += length;
					return true;
				}
			default:
				// Groups (3, 4) are deprecated and appear in no ONNX file. Anything else is corruption.
				return false;
		}
	}

	[[nodiscard]] std::string readString()
	{
		std::uint64_t length { 0 };
		if ( !readVarint( length ) ) return {};
		if ( static_cast< std::uint64_t >( m_end - m_cursor ) < length ) return {};

		std::string value( reinterpret_cast< const char* >( m_cursor ), length );
		m_cursor += length;
		return value;
	}
};

//! Reads a TensorShapeProto into a flat list, with a dynamic axis reported as zero.
[[nodiscard]] std::vector< std::int64_t > readShape( Reader shape )
{
	std::vector< std::int64_t > dims {};

	std::uint64_t field { 0 };
	std::uint8_t wire { 0 };

	while ( !shape.done() && shape.readTag( field, wire ) )
	{
		if ( field != SHAPE_DIM || wire != WIRE_LENGTH )
		{
			if ( !shape.skip( wire ) ) break;
			continue;
		}

		Reader dim { nullptr, nullptr };
		if ( !shape.readSubMessage( dim ) ) break;

		// A Dimension carries either dim_value or dim_param; a symbolic axis has no dim_value, and
		// its absence is exactly what "dynamic" means here. Left as zero.
		std::int64_t value { 0 };

		std::uint64_t dim_field { 0 };
		std::uint8_t dim_wire { 0 };

		while ( !dim.done() && dim.readTag( dim_field, dim_wire ) )
		{
			if ( dim_field == DIM_VALUE && dim_wire == WIRE_VARINT )
			{
				std::uint64_t raw { 0 };
				if ( !dim.readVarint( raw ) ) break;
				value = static_cast< std::int64_t >( raw );
				continue;
			}

			if ( !dim.skip( dim_wire ) ) break;
		}

		dims.push_back( value );
	}

	return dims;
}

//! Reads one ValueInfoProto: its name, and the shape of its tensor type.
[[nodiscard]] GraphTensorInfo readValueInfo( Reader value_info )
{
	GraphTensorInfo info {};

	std::uint64_t field { 0 };
	std::uint8_t wire { 0 };

	while ( !value_info.done() && value_info.readTag( field, wire ) )
	{
		if ( field == VALUEINFO_NAME && wire == WIRE_LENGTH )
		{
			info.m_name = value_info.readString();
			continue;
		}

		if ( field == VALUEINFO_TYPE && wire == WIRE_LENGTH )
		{
			Reader type { nullptr, nullptr };
			if ( !value_info.readSubMessage( type ) ) break;

			std::uint64_t type_field { 0 };
			std::uint8_t type_wire { 0 };

			while ( !type.done() && type.readTag( type_field, type_wire ) )
			{
				if ( type_field != TYPE_TENSOR || type_wire != WIRE_LENGTH )
				{
					if ( !type.skip( type_wire ) ) break;
					continue;
				}

				Reader tensor { nullptr, nullptr };
				if ( !type.readSubMessage( tensor ) ) break;

				std::uint64_t tensor_field { 0 };
				std::uint8_t tensor_wire { 0 };

				while ( !tensor.done() && tensor.readTag( tensor_field, tensor_wire ) )
				{
					if ( tensor_field == TENSORTYPE_SHAPE && tensor_wire == WIRE_LENGTH )
					{
						Reader shape { nullptr, nullptr };
						if ( !tensor.readSubMessage( shape ) ) break;
						info.m_shape = readShape( shape );
						continue;
					}

					if ( !tensor.skip( tensor_wire ) ) break;
				}
			}

			continue;
		}

		if ( !value_info.skip( wire ) ) break;
	}

	return info;
}

//! A read-only memory mapping that closes itself.
class Mapping
{
	const std::byte* m_data { nullptr };
	std::size_t m_size { 0 };

  public:

	Mapping() = default;

	Mapping( const Mapping& ) = delete;
	Mapping& operator=( const Mapping& ) = delete;
	Mapping( Mapping&& ) = delete;
	Mapping& operator=( Mapping&& ) = delete;

	~Mapping()
	{
		if ( m_data != nullptr ) ::munmap( const_cast< std::byte* >( m_data ), m_size );
	}

	[[nodiscard]] std::expected< void, std::string > open( const std::filesystem::path& path )
	{
		const int fd { ::open( path.c_str(), O_RDONLY | O_CLOEXEC ) };
		if ( fd < 0 )
			return std::unexpected( std::format( "could not open {}: {}", path.string(), std::strerror( errno ) ) );

		struct stat info {};
		if ( ::fstat( fd, &info ) < 0 )
		{
			const auto reason { std::string { std::strerror( errno ) } };
			::close( fd );
			return std::unexpected( std::format( "could not stat {}: {}", path.string(), reason ) );
		}

		if ( info.st_size <= 0 )
		{
			::close( fd );
			return std::unexpected( std::format( "{} is empty", path.string() ) );
		}

		m_size = static_cast< std::size_t >( info.st_size );

		void* const mapped { ::mmap( nullptr, m_size, PROT_READ, MAP_PRIVATE, fd, 0 ) };

		// The descriptor has done its job; the mapping outlives it.
		::close( fd );

		if ( mapped == MAP_FAILED )
			return std::unexpected( std::format( "could not map {}: {}", path.string(), std::strerror( errno ) ) );

		m_data = static_cast< const std::byte* >( mapped );
		return {};
	}

	[[nodiscard]] const std::byte* begin() const { return m_data; }

	[[nodiscard]] const std::byte* end() const { return m_data + m_size; }
};

std::expected< GraphInterface, std::string > readGraphInterface( const std::filesystem::path& onnx_path )
{
	Mapping mapping {};
	if ( const auto opened { mapping.open( onnx_path ) }; !opened ) return std::unexpected( opened.error() );

	Reader model { mapping.begin(), mapping.end() };

	std::uint64_t field { 0 };
	std::uint8_t wire { 0 };

	while ( !model.done() && model.readTag( field, wire ) )
	{
		if ( field != MODEL_GRAPH || wire != WIRE_LENGTH )
		{
			if ( !model.skip( wire ) )
				return std::unexpected( std::format( "{} is not a readable ONNX model", onnx_path.string() ) );
			continue;
		}

		Reader graph { nullptr, nullptr };
		if ( !model.readSubMessage( graph ) )
			return std::unexpected( std::format( "{} has a truncated graph", onnx_path.string() ) );

		GraphInterface interface {};

		std::uint64_t graph_field { 0 };
		std::uint8_t graph_wire { 0 };

		while ( !graph.done() && graph.readTag( graph_field, graph_wire ) )
		{
			const bool wanted { graph_field == GRAPH_INPUT || graph_field == GRAPH_OUTPUT };

			if ( !wanted || graph_wire != WIRE_LENGTH )
			{
				// Everything else, including every initializer, is stepped over without its bytes
				// being read. This is where the cost of not creating a session is avoided.
				if ( !graph.skip( graph_wire ) ) break;
				continue;
			}

			Reader value_info { nullptr, nullptr };
			if ( !graph.readSubMessage( value_info ) ) break;

			if ( graph_field == GRAPH_INPUT )
				interface.m_inputs.emplace_back( readValueInfo( value_info ) );
			else
				interface.m_outputs.emplace_back( readValueInfo( value_info ) );
		}

		if ( interface.m_inputs.empty() || interface.m_outputs.empty() )
			return std::unexpected( std::format( "{} declares no input or no output", onnx_path.string() ) );

		return interface;
	}

	return std::unexpected( std::format( "{} contains no graph", onnx_path.string() ) );
}

} // namespace premade
