//
// Created by kj16609 on 6/7/22.
//

#include "crypto.hpp"

std::vector<uint8_t> SHA256(const std::vector<uint8_t>& data)
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
	std::vector<uint8_t> output(md, md + md_len);
	return output;
}

std::vector<uint8_t> MD5(const std::vector<uint8_t>& data)
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
	std::vector<uint8_t> output(md, md + md_len);
	return output;
}