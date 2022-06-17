//
// Created by kj16609 on 6/7/22.
//

#include <array>
#include <cstring>

#include "crypto.hpp"

std::array<uint8_t, 32> SHA256(const std::vector<uint8_t>& data)
{
	//SHA256
	EVP_MD_CTX *ctx;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	ctx = EVP_MD_CTX_create();
	EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
	EVP_DigestUpdate(ctx, data.data(), data.size());
	EVP_DigestFinal_ex(ctx, md, &md_len);
	EVP_MD_CTX_destroy(ctx);
	std::array<uint8_t, 32> output;
	memcpy(output.data(), md, md_len);
	return output;
}

std::array<uint8_t, 16> MD5(const std::vector<uint8_t>& data)
{
	//MD5
	EVP_MD_CTX *ctx;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	ctx = EVP_MD_CTX_create();
	EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
	EVP_DigestUpdate(ctx, data.data(), data.size());
	EVP_DigestFinal_ex(ctx, md, &md_len);
	EVP_MD_CTX_destroy(ctx);
	std::array<uint8_t, 16> output;
	memcpy(output.data(), md, md_len);
	return output;
}