#include "pow.hpp"
#include "utils.hpp"

#include <array>
#include <iomanip>
#include <limits>
#include <sispopmq/base64.h>
#include <openssl/sha.h>
#include <sstream>
#include <string.h>

using namespace std::chrono_literals;

constexpr int BYTE_LEN = 8;
constexpr std::chrono::milliseconds TIMESTAMP_VARIANCE = 15min;

using uint64Bytes = std::array<uint8_t, BYTE_LEN>;

// This enforces that the result array has the most significant byte at index 0
void u64ToU8Array(uint64_t numberVal, uint64Bytes& result) {
    for (int idx = result.size() - 1; idx >= 0; idx--) {
        // Grab the least significant byte
        result[idx] = numberVal & (uint64_t)0xFF;
        // Bitshift right one byte
        numberVal >>= BYTE_LEN;
    }
}

bool addWillOverflow(uint64_t x, uint64_t add) {
    return std::numeric_limits<uint64_t>::max() - x < add;
}

bool multWillOverflow(uint64_t left, uint64_t right) {
    return left != 0 &&
           (std::numeric_limits<std::uint64_t>::max() / left < right);
}

bool calcTarget(const std::string& payload, const uint64_t ttlInt,
                const int difficulty, uint64Bytes& target) {
    bool overflow = addWillOverflow(payload.size(), BYTE_LEN);
    if (overflow)
        return false;
    uint64_t totalLen = payload.size() + BYTE_LEN;
    overflow = multWillOverflow(ttlInt, totalLen);
    if (overflow)
        return false;
    uint64_t ttlMult = ttlInt * totalLen;
    uint64_t innerFrac = ttlMult / std::numeric_limits<uint16_t>::max();
    overflow = addWillOverflow(totalLen, innerFrac);
    if (overflow)
        return false;
    uint64_t lenPlusInnerFrac = totalLen + innerFrac;
    overflow = multWillOverflow(difficulty, lenPlusInnerFrac);
    if (overflow)
        return false;
    uint64_t denominator = difficulty * lenPlusInnerFrac;
    uint64_t targetNum = std::numeric_limits<uint64_t>::max() / denominator;

    u64ToU8Array(targetNum, target);
    return true;
}

int get_valid_difficulty(const std::string& timestamp,
                         const std::vector<pow_difficulty_t>& history) {
    uint64_t timestamp_long;
    try {
        timestamp_long = std::stoull(timestamp);
    } catch (...) {
        // Should never happen, checked previously
        return false;
    }
    const auto msg_timestamp = std::chrono::milliseconds(timestamp_long);

    int difficulty = std::numeric_limits<int>::max();
    int most_recent_difficulty = std::numeric_limits<int>::max();
    std::chrono::milliseconds most_recent(0);
    const std::chrono::milliseconds lower = msg_timestamp - TIMESTAMP_VARIANCE;
    const std::chrono::milliseconds upper = msg_timestamp + TIMESTAMP_VARIANCE;

    for (const auto& this_difficulty : history) {
        const std::chrono::milliseconds t = this_difficulty.timestamp;
        if (t < msg_timestamp && t >= most_recent) {
            most_recent = t;
            most_recent_difficulty = this_difficulty.difficulty;
        }

        if (t >= lower && t <= upper) {
            difficulty = std::min(this_difficulty.difficulty, difficulty);
        }
    }
    return std::min(most_recent_difficulty, difficulty);
}

bool checkPoW(const std::string& nonce, const std::string& timestamp,
              const std::string& ttl, const std::string& recipient,
              const std::string& data, std::string& messageHash,
              const int difficulty) {
    std::string payload;
    payload.reserve(timestamp.size() + ttl.size() + recipient.size() +
                    data.size());
    payload += timestamp;
    payload += ttl;
    payload += recipient;
    payload += data;

    uint64_t ttlInt;
    if (!util::parseTTL(ttl, ttlInt))
        return false;
    // ttl is in milliseconds, but target calculation wants seconds
    ttlInt = ttlInt / 1000;
    uint64Bytes target;
    calcTarget(payload, ttlInt, difficulty, target);

    uint8_t hashResult[SHA512_DIGEST_LENGTH];
    // Initial hash
    SHA512((const unsigned char*)payload.data(), payload.size(), hashResult);
    // Convert nonce to binary
    if (!sispopmq::is_base64(nonce))
        return false;
    std::string decodedNonce = sispopmq::from_base64(nonce);
    // Convert decoded nonce string into uint8_t vector. Will have length 8
    std::vector<uint8_t> innerPayload;
    innerPayload.reserve(decodedNonce.size() + SHA512_DIGEST_LENGTH);
    innerPayload.insert(std::end(innerPayload), std::begin(decodedNonce),
                        std::end(decodedNonce));
    innerPayload.insert(std::end(innerPayload), hashResult,
                        hashResult + SHA512_DIGEST_LENGTH);
    // Final hash
    SHA512(innerPayload.data(), innerPayload.size(), hashResult);
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA512_DIGEST_LENGTH; i++)
        ss << std::setw(2) << static_cast<unsigned>(hashResult[i]);
    messageHash = ss.str();

    return memcmp(hashResult, target.data(), BYTE_LEN) < 0;
}
