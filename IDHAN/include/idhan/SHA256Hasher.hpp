#pragma once

#include <openssl/evp.h>

#include <cstddef>
#include <memory>
#include <span>

#include "idhan/SHA256.hpp"

namespace idhan
{

//! Incremental SHA-256 for inputs that cannot be held in one buffer.
class SHA256Hasher
{
	std::unique_ptr< EVP_MD_CTX, decltype( &EVP_MD_CTX_free ) > m_context { EVP_MD_CTX_new(), EVP_MD_CTX_free };
	bool m_finished { false };

  public:

	SHA256Hasher();

	SHA256Hasher( const SHA256Hasher& ) = delete;
	SHA256Hasher& operator=( const SHA256Hasher& ) = delete;

	void update( std::span< const std::byte > bytes );
	[[nodiscard]] SHA256 finish();
};

} // namespace idhan
