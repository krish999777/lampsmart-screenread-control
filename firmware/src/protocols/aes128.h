#pragma once

#include <cstdint>

namespace lampsmart {

/// Encrypts a single 16-byte block with AES-128-ECB.
void aes128EcbEncrypt(const uint8_t key[16], const uint8_t input[16], uint8_t output[16]);

} // namespace lampsmart

