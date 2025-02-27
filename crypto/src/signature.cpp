#include "signature.h"
#include "utils.hpp"

extern "C" {
#include "oxen/crypto-ops/crypto-ops.h"
#include "oxen/crypto-ops/hash-ops.h"
}

#include <sodium/crypto_generichash.h>
#include <sodium/crypto_generichash_blake2b.h>
#include <sodium/randombytes.h>
#include <sispopmq/base32z.h>
#include <sispopmq/base64.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring> // for memcmp
#include <iterator>
#include <string>

static_assert(crypto_generichash_BYTES == oxen::HASH_SIZE, "Wrong hash size!");

namespace oxen {

using ec_point = std::array<uint8_t, 32>;
struct s_comm {
    uint8_t h[32];
    uint8_t key[32];
    uint8_t comm[32];
};

void random_scalar(ec_scalar& k) {
    for (size_t i = 0; i < k.size() / 4; ++i) {
        const uint32_t random = randombytes_random();
        k[i + 0] = (random & 0xFF000000) >> 24;
        k[i + 1] = (random & 0x00FF0000) >> 16;
        k[i + 2] = (random & 0x0000FF00) >> 8;
        k[i + 3] = (random & 0x000000FF) >> 0;
    }
}

bool hash_to_scalar(const void* input, size_t size, ec_scalar& output) {
    cn_fast_hash(input, size, reinterpret_cast<char*>(output.data()));
    sc_reduce32(output.data());
    return true;
}

hash hash_data(const std::string& data) {
    hash hash{{0}};
    crypto_generichash(hash.data(), hash.size(),
                       reinterpret_cast<const unsigned char*>(data.c_str()),
                       data.size(), nullptr, 0);
    return hash;
}

signature generate_signature(const hash& prefix_hash,
                             const oxend_key_pair_t& key_pair) {
    ge_p3 tmp3;
    ec_scalar k;
    s_comm buf;
    signature sig;
#if !defined(NDEBUG)
    {
        ge_p3 t;
        public_key_t t2;
        assert(sc_check(key_pair.private_key.data()) == 0);
        ge_scalarmult_base(&t, key_pair.private_key.data());
        ge_p3_tobytes(t2.data(), &t);
        assert(key_pair.public_key == t2);
    }
#endif
    std::copy(prefix_hash.begin(), prefix_hash.end(), std::begin(buf.h));
    std::copy(key_pair.public_key.begin(), key_pair.public_key.end(),
              std::begin(buf.key));
try_again:
    random_scalar(k);
    if (k[7] == 0) // we don't want tiny numbers here
        goto try_again;
    ge_scalarmult_base(&tmp3, k.data());
    ge_p3_tobytes(buf.comm, &tmp3);
    hash_to_scalar(&buf, sizeof(s_comm), sig.c);
    if (!sc_isnonzero((const unsigned char*)sig.c.data()))
        goto try_again;
    sc_mulsub(sig.r.data(), sig.c.data(), key_pair.private_key.data(),
              k.data());
    if (!sc_isnonzero((const unsigned char*)sig.r.data()))
        goto try_again;
    return sig;
}

bool check_signature(const signature& sig, const hash& prefix_hash,
                     const public_key_t& pub) {
    ge_p2 tmp2;
    ge_p3 tmp3;
    ec_scalar c;
    s_comm buf;
    //    assert(check_key(pub));
    std::copy(prefix_hash.begin(), prefix_hash.end(), std::begin(buf.h));
    std::copy(pub.begin(), pub.end(), std::begin(buf.key));
    if (ge_frombytes_vartime(&tmp3, pub.data()) != 0) {
        return false;
    }
    if (sc_check(sig.c.data()) != 0 || sc_check(sig.r.data()) != 0 ||
        !sc_isnonzero(sig.c.data())) {
        return false;
    }
    ge_double_scalarmult_base_vartime(&tmp2, sig.c.data(), &tmp3, sig.r.data());
    ge_tobytes(buf.comm, &tmp2);
    static const ec_point infinity = {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    if (memcmp(buf.comm, &infinity, 32) == 0)
        return false;
    hash_to_scalar(&buf, sizeof(s_comm), c);
    sc_sub(c.data(), c.data(), sig.c.data());
    return sc_isnonzero(c.data()) == 0;
}

bool check_signature(const std::string& signature_b64, const hash& hash,
                     const std::string& public_key_b32z) {
    if (!sispopmq::is_base64(signature_b64))
        return false;

    // 64 bytes bytes -> 86/88 base64 encoded bytes with/without padding
    if (!(signature_b64.size() == 86 ||
                (signature_b64.size() == 88 && signature_b64[86] == '=')))
        return false;

    // convert signature
    signature sig;
    static_assert(sizeof(sig) == 64);
    sispopmq::from_base64(signature_b64.begin(), signature_b64.end(),
            reinterpret_cast<uint8_t*>(&sig));

    // 32 bytes -> 52 base32z encoded characters
    if (public_key_b32z.size() != 52 || !sispopmq::is_base32z(public_key_b32z))
        return false;

    // convert public key
    public_key_t public_key;
    static_assert(sizeof(public_key) == 32);
    sispopmq::from_base32z(public_key_b32z.begin(), public_key_b32z.end(),
            public_key.begin());

    return check_signature(sig, hash, public_key);
}

} // namespace oxen
