/* SPDX-License-Identifier: MIT
 * SHA-256 (FIPS 180-4), small and plain: server passwords never travel as
 * themselves, only as sha256(challenge | password). */
#ifndef OFP_SHA256_H
#define OFP_SHA256_H

#include <stddef.h>
#include <stdint.h>

void sha256(const void *data, size_t len, uint8_t out[32]);
/* the answer to a server's password challenge */
void sha256_password(uint32_t nonce, const char *password, uint8_t out[32]);

#endif
