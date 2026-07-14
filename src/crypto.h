#pragma once
#include <string>

std::string cryptoSha256(const std::string& input);
std::string cryptoMd5(const std::string& input);
std::string cryptoBase64Encode(const std::string& input);
std::string cryptoBase64Decode(const std::string& input);
std::string cryptoUuid();
