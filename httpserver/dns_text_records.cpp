#include "dns_text_records.h"
#include <nlohmann/json.hpp>
#include "pow.hpp"
#include "version.h"
#include <netinet/in.h>
#include <resolv.h>
#include <charconv>

#include <boost/algorithm/string.hpp>

using json = nlohmann::json;

static constexpr char POW_DIFFICULTY_URL[] = "sentinel.messenger.sispop.network";
static constexpr char LATEST_VERSION_URL[] = "storage.version.sispop.network";

namespace oxen {

namespace dns {

static std::string get_dns_record(const char* url, std::error_code& ec) {

    std::string data;
    unsigned char query_buffer[1024] = {};

    // don't want to assume that ec has default value
    ec = std::error_code{};

    int response =
        res_query(url, ns_c_in, ns_t_txt, query_buffer, sizeof(query_buffer));

    if (response == -1) {
        OXEN_LOG(warn, "res_query failed while retrieving dns entry");
        ec = std::make_error_code(std::errc::bad_message);
        return data;
    }

    ns_msg nsMsg;

    if (ns_initparse(query_buffer, response, &nsMsg) == -1) {
        OXEN_LOG(warn, "ns_initparse failed while retrieving dns entry");
        ec = std::make_error_code(std::errc::bad_message);
        return data;
    }

    // We get back a sequence of N...[N...] values where N is a byte indicating
    // the length of the immediately following ... data.
    const auto count = ns_msg_count(nsMsg, ns_s_an);

    constexpr size_t DNS_MAX_CHUNK_LENGTH = 255;

    data.reserve(DNS_MAX_CHUNK_LENGTH * count);
    for (int i = 0; i < count; i++) {
        ns_rr rr;
        if (ns_parserr(&nsMsg, ns_s_an, i, &rr) == -1) {
            OXEN_LOG(warn, "ns_parserr failed while parsing dns entry");
            ec = std::make_error_code(std::errc::bad_message);
            return data;
        }
        auto* rdata = ns_rr_rdata(rr);
        data.append(reinterpret_cast<const char*>(rdata + 1), rdata[0]);
    }

    return data;
}

std::vector<pow_difficulty_t> query_pow_difficulty(std::error_code& ec) {
    OXEN_LOG(debug, "Querying PoW difficulty...");

    std::vector<pow_difficulty_t> new_history;
    const std::string data = get_dns_record(POW_DIFFICULTY_URL, ec);
    if (ec) {
        return new_history;
    }

    try {
        const json history = json::parse(data, nullptr, true);
        for (const auto& el : history.items()) {
            const std::chrono::milliseconds timestamp(std::stoul(el.key()));
            const int difficulty = el.value().get<int>();
            new_history.push_back(pow_difficulty_t{timestamp, difficulty});
        }
        return new_history;
    } catch (const std::exception& e) {
        OXEN_LOG(warn, "JSON parsing of PoW data failed: {}", e.what());
        ec = std::make_error_code(std::errc::bad_message);
        return new_history;
    }
}

static std::string query_latest_version() {
    OXEN_LOG(debug, "Querying Latest Version...");

    std::error_code ec;
    const std::string version_str = get_dns_record(LATEST_VERSION_URL, ec);

    if (ec) {
        return "";
    }

    return version_str;
}

using version_t = std::array<uint16_t, 3>;

static bool parse_version(const std::string& str, version_t& version_out) {
    std::vector<std::string> strs;
    strs.reserve(3);
    boost::split(strs, str, boost::is_any_of("."));
    if (strs.size() != 3)
        return false;

    for (size_t i = 0; i < 3; i++) {
        auto* end = strs[i].data() + strs[i].size();
        auto [p, ec] = std::from_chars(strs[i].data(), end, version_out[i]);
        if (ec != std::errc() || p != end)
            return false;
    }

    return true;
}

void check_latest_version() {

    const auto latest_version_str = query_latest_version();

    if (latest_version_str.empty()) {
        OXEN_LOG(warn, "Failed to retrieve or parse the latest version number "
                       "from DNS record");
        return;
    }

    version_t latest_version;
    if (!parse_version(latest_version_str, latest_version)) {
        OXEN_LOG(warn, "Could not parse the latest version: {}",
                 latest_version_str);
        return;
    }

    if (STORAGE_SERVER_VERSION < latest_version) {
        OXEN_LOG(warn,
                 "You are using an outdated version of the storage server "
                 "({}), please update to {}!",
                 STORAGE_SERVER_VERSION_STRING, latest_version_str);
    } else {
        OXEN_LOG(debug,
                 "You are using the latest version of the storage server ({})",
                 STORAGE_SERVER_VERSION_STRING);
    }
}

} // namespace dns
} // namespace oxen
