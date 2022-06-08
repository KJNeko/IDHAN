//
// Created by kj16609 on 6/7/22.
//

#ifndef MAIN_CRYPTO_HPP
#define MAIN_CRYPTO_HPP

#include <openssl/evp.h>
#include <vector>

std::vector<uint8_t> SHA256(std::vector<uint8_t> data);

std::vector<uint8_t> MD5(std::vector<uint8_t> data);


#endif //MAIN_CRYPTO_HPP
