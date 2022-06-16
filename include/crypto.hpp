//
// Created by kj16609 on 6/7/22.
//

#ifndef MAIN_CRYPTO_HPP
#define MAIN_CRYPTO_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#include <openssl/evp.h>
#pragma GCC diagnostic pop


#include <vector>

std::vector<uint8_t> SHA256(const std::vector<uint8_t>& data);

std::vector<uint8_t> MD5(const std::vector<uint8_t>& data);


#endif //MAIN_CRYPTO_HPP
