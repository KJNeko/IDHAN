#include <concepts>

#include "idhan/ipc/binary/FieldCodec.hpp"
#include "idhan/ipc/binary/Frame.hpp"
#include "idhan/ipc/binary/ModuleRunner.hpp"

using namespace idhan::ipc::binary;
static_assert( header_bytes == 32 );
static_assert( envelope_bytes == 24 );
static_assert( max_descriptors == 8 );
static_assert( std::move_constructible< Frame > );
static_assert( std::same_as< decltype( std::declval< Frame >().descriptors ), std::vector< folly::File > > );
