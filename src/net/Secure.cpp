#include "Secure.h"

#include <vector>

#include <esp_random.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

namespace net {

namespace {

const char *kHex = "0123456789abcdef";

String toHex(const uint8_t *data, size_t len) {
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0x0F];
    }
    return out;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse a hex string into `out`. Returns false on odd length / bad chars.
bool fromHex(const String &in, std::vector<uint8_t> &out) {
    if (in.length() % 2 != 0) return false;
    out.clear();
    out.reserve(in.length() / 2);
    for (size_t i = 0; i < in.length(); i += 2) {
        const int hi = hexNibble(in[i]);
        const int lo = hexNibble(in[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

// AES-256 key bound to this device (eFuse MAC) + a versioned label.
void deriveKey(uint8_t key[32]) {
    const uint64_t mac = ESP.getEfuseMac();
    String material = "hapanel-aes-key-v1";
    uint8_t buf[8 + 18];
    memcpy(buf, &mac, sizeof(mac));
    memcpy(buf + 8, material.c_str(), 18);
    mbedtls_sha256(buf, sizeof(buf), key, 0);
}

} // namespace

String sha256Hex(const String &salt, const String &password) {
    String in = salt + password;
    uint8_t digest[32];
    mbedtls_sha256(reinterpret_cast<const uint8_t *>(in.c_str()), in.length(), digest, 0);
    return toHex(digest, sizeof(digest));
}

String randomHex(size_t nbytes) {
    std::vector<uint8_t> buf(nbytes);
    for (size_t i = 0; i < nbytes; i++) buf[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    return toHex(buf.data(), nbytes);
}

String encryptToken(const String &plain) {
    if (plain.length() == 0) return "";

    uint8_t key[32];
    deriveKey(key);

    // Random IV.
    uint8_t iv[16];
    for (auto &b : iv) b = static_cast<uint8_t>(esp_random() & 0xFF);
    uint8_t ivWork[16];
    memcpy(ivWork, iv, sizeof(iv));

    // PKCS#7 pad to a 16-byte boundary.
    const size_t inLen = plain.length();
    const size_t padded = ((inLen / 16) + 1) * 16;
    std::vector<uint8_t> buf(padded);
    memcpy(buf.data(), plain.c_str(), inLen);
    const uint8_t pad = static_cast<uint8_t>(padded - inLen);
    for (size_t i = inLen; i < padded; i++) buf[i] = pad;

    std::vector<uint8_t> ct(padded);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    bool ok = mbedtls_aes_setkey_enc(&ctx, key, 256) == 0 &&
              mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, padded, ivWork,
                                    buf.data(), ct.data()) == 0;
    mbedtls_aes_free(&ctx);
    if (!ok) return "";

    return toHex(iv, sizeof(iv)) + toHex(ct.data(), ct.size());
}

String decryptToken(const String &blob) {
    if (blob.length() < 32 + 32) return "";  // 16-byte IV + at least one block

    std::vector<uint8_t> raw;
    if (!fromHex(blob, raw) || raw.size() < 32 || (raw.size() - 16) % 16 != 0) return "";

    uint8_t key[32];
    deriveKey(key);

    uint8_t iv[16];
    memcpy(iv, raw.data(), 16);
    const size_t ctLen = raw.size() - 16;
    std::vector<uint8_t> pt(ctLen);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    bool ok = mbedtls_aes_setkey_dec(&ctx, key, 256) == 0 &&
              mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, ctLen, iv,
                                    raw.data() + 16, pt.data()) == 0;
    mbedtls_aes_free(&ctx);
    if (!ok) return "";

    // Strip PKCS#7 padding.
    const uint8_t pad = pt[ctLen - 1];
    if (pad == 0 || pad > 16 || pad > ctLen) return "";
    return String(reinterpret_cast<const char *>(pt.data()), ctLen - pad);
}

bool base64Decode(const String &in, String &out) {
    size_t needed = 0;
    // First call sizes the output buffer.
    mbedtls_base64_decode(nullptr, 0, &needed,
                          reinterpret_cast<const uint8_t *>(in.c_str()), in.length());
    std::vector<uint8_t> buf(needed + 1);
    size_t written = 0;
    if (mbedtls_base64_decode(buf.data(), buf.size(), &written,
                              reinterpret_cast<const uint8_t *>(in.c_str()),
                              in.length()) != 0) {
        return false;
    }
    out = String(reinterpret_cast<const char *>(buf.data()), written);
    return true;
}

} // namespace net
