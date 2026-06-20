/**
 * Small crypto helpers built on mbedTLS (bundled with the ESP32 SDK):
 *   - salted SHA-256 for the config-portal password (never stored in clear),
 *   - AES-256-CBC encryption of the HA token at rest (key derived from the
 *     device eFuse MAC, so a raw flash dump alone does not reveal the token).
 *
 * The token key lives on the device, so this protects against a passive flash
 * read, not against an attacker who also has the firmware. For stronger
 * guarantees, enable ESP32 flash/NVS encryption at the platform level.
 */
#ifndef NET_SECURE_H
#define NET_SECURE_H

#include <Arduino.h>

namespace net {

/** Lowercase-hex SHA-256 of (salt || password). */
String sha256Hex(const String &salt, const String &password);

/** `nbytes` of CSPRNG output as lowercase hex (2*nbytes chars). */
String randomHex(size_t nbytes);

/** Encrypt a token for at-rest storage. Returns a hex blob (iv + ciphertext),
 *  or "" if `plain` is empty / on failure. */
String encryptToken(const String &plain);

/** Inverse of encryptToken(). Returns "" on empty input or failure. */
String decryptToken(const String &blob);

/** Decode a standard base64 string into `out`. Returns false on malformed input. */
bool base64Decode(const String &in, String &out);

} // namespace net

#endif /* NET_SECURE_H */
