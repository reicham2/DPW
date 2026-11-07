#include "handlers/handlers.hpp"
#include "core/http.hpp"
#include "core/request_helpers.hpp"
#include "json.hpp"

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <curl/curl.h>

namespace
{
    constexpr size_t kMaxRequestBytes = 128 * 1024;
    constexpr size_t kMaxTextBytes = 20 * 1024;
    constexpr size_t kMaxResponseBytes = 1024 * 1024;
    constexpr int kMaxConcurrentRequests = 4;
    constexpr long kRequestTimeoutMs = 12000;

    std::atomic<int> requests_in_flight{0};

    size_t append_bounded_response(char *data, size_t size, size_t count, void *userdata)
    {
        const size_t bytes = size * count;
        auto *response = static_cast<std::string *>(userdata);
        if (bytes > kMaxResponseBytes - response->size())
            return 0;
        response->append(data, bytes);
        return bytes;
    }

    std::string url_encode(CURL *curl, const std::string &value)
    {
        char *encoded = curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
        if (!encoded)
            throw std::runtime_error("LanguageTool request encoding failed");
        std::string result(encoded);
        curl_free(encoded);
        return result;
    }

    nlohmann::json select_matches(const std::string &response)
    {
        const auto parsed = nlohmann::json::parse(response, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() ||
            !parsed.contains("matches") || !parsed["matches"].is_array())
            throw std::runtime_error("Invalid response from local proofreading service");

        nlohmann::json matches = nlohmann::json::array();
        const auto limit = std::min<size_t>(parsed["matches"].size(), 100);
        for (size_t i = 0; i < limit; ++i)
        {
            const auto &match = parsed["matches"][i];
            if (!match.is_object() ||
                !match.contains("offset") || !match["offset"].is_number_integer() ||
                !match.contains("length") || !match["length"].is_number_integer() ||
                !match.contains("message") || !match["message"].is_string() ||
                !match.contains("replacements") || !match["replacements"].is_array())
                continue;

            nlohmann::json replacements = nlohmann::json::array();
            for (const auto &replacement : match["replacements"])
            {
                if (replacements.size() == 10)
                    break;
                if (replacement.is_object() && replacement.contains("value") &&
                    replacement["value"].is_string())
                    replacements.push_back({{"value", replacement["value"]}});
            }

            nlohmann::json rule = nlohmann::json::object();
            if (match.contains("rule") && match["rule"].is_object())
            {
                for (const char *key : {"id", "description", "issueType"})
                {
                    if (match["rule"].contains(key) && match["rule"][key].is_string())
                        rule[key] = match["rule"][key];
                }
            }

            matches.push_back({
                {"offset", match["offset"]},
                {"length", match["length"]},
                {"message", match["message"]},
                {"shortMessage", match.value("shortMessage", std::string())},
                {"replacements", std::move(replacements)},
                {"rule", std::move(rule)},
            });
        }
        return matches;
    }

    std::string langtool_url()
    {
        const char *configured = std::getenv("LANGUAGETOOL_URL");
        return configured && *configured
            ? configured
            : "http://languagetool:8010/v2/check";
    }

    void check_text(HttpRes *res, uWS::Loop *loop,
                    const std::shared_ptr<std::atomic_bool> &aborted,
                    std::string text)
    {
        int status = 503;
        std::string response_body = R"({"error":"Der lokale Korrekturdienst ist nicht erreichbar."})";
        CURL *curl = curl_easy_init();
        if (curl)
        {
            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            headers = curl_slist_append(headers, "Accept: application/json");
            std::string upstream_response;
            const std::string form = "language=de-CH&text=" + url_encode(curl, text);
            const std::string url = langtool_url();
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_bounded_response);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &upstream_response);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");

            const CURLcode result = curl_easy_perform(curl);
            long upstream_status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &upstream_status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (result == CURLE_OK && upstream_status >= 200 && upstream_status < 300)
            {
                try
                {
                    response_body = nlohmann::json{
                        {"language", "de-CH"},
                        {"matches", select_matches(upstream_response)},
                    }.dump();
                    status = 200;
                }
                catch (const std::exception &e)
                {
                    std::fprintf(stderr, "[proofreading] Invalid LanguageTool response: %s\n", e.what());
                    status = 502;
                    response_body = R"({"error":"Der Korrekturdienst hat eine ungültige Antwort geliefert."})";
                }
            }
            else
            {
                std::fprintf(stderr, "[proofreading] LanguageTool request failed (curl=%s, http=%ld)\n",
                             curl_easy_strerror(result), upstream_status);
            }
        }
        else
        {
            std::fprintf(stderr, "[proofreading] curl_easy_init failed\n");
        }

        loop->defer([res, aborted, status, response_body = std::move(response_body)]() mutable {
            if (*aborted)
                return;
            res->cork([&]() { send_json(res, status, response_body); });
        });
    }
}

void handle_post_proofreading_check(HttpRes *res, HttpReq *req)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<std::atomic_bool>(false);
    auto handled = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    uWS::Loop *loop = uWS::Loop::get();
    res->onData([res, body, aborted, handled, loop](std::string_view chunk, bool last) {
        if (*handled)
            return;
        if (chunk.size() > kMaxRequestBytes - body->size())
        {
            *handled = true;
            send_json(res, 413, R"({"error":"Der Anfrageinhalt ist zu gross."})");
            return;
        }
        body->append(chunk.data(), chunk.size());
        if (!last)
            return;
        *handled = true;

        const auto parsed = nlohmann::json::parse(*body, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() ||
            !parsed.contains("text") || !parsed["text"].is_string())
        {
            send_json(res, 400, R"({"error":"Erwarte ein JSON-Objekt mit Text."})");
            return;
        }
        const std::string text = parsed["text"].get<std::string>();
        if (text.empty())
        {
            send_json(res, 400, R"({"error":"Der Text darf nicht leer sein."})");
            return;
        }
        if (text.size() > kMaxTextBytes)
        {
            send_json(res, 413, R"({"error":"Der Text ist zu lang (maximal 20 KiB)."})");
            return;
        }

        if (requests_in_flight.fetch_add(1) >= kMaxConcurrentRequests)
        {
            requests_in_flight.fetch_sub(1);
            send_json(res, 503, R"({"error":"Der Korrekturdienst ist ausgelastet. Bitte später erneut versuchen."})");
            return;
        }
        try
        {
            std::thread([res, loop, aborted, text]() {
                try
                {
                    if (!*aborted)
                        check_text(res, loop, aborted, text);
                }
                catch (const std::exception &e)
                {
                    std::fprintf(stderr, "[proofreading] Request processing failed: %s\n", e.what());
                    loop->defer([res, aborted]() {
                        if (*aborted)
                            return;
                        res->cork([&]() {
                            send_json(res, 503,
                                      R"({"error":"Der lokale Korrekturdienst ist nicht verfügbar."})");
                        });
                    });
                }
                catch (...)
                {
                    std::fprintf(stderr, "[proofreading] Request processing failed unexpectedly\n");
                    loop->defer([res, aborted]() {
                        if (*aborted)
                            return;
                        res->cork([&]() {
                            send_json(res, 503,
                                      R"({"error":"Der lokale Korrekturdienst ist nicht verfügbar."})");
                        });
                    });
                }
                requests_in_flight.fetch_sub(1);
            }).detach();
        }
        catch (...)
        {
            requests_in_flight.fetch_sub(1);
            send_json(res, 503, R"({"error":"Der Korrekturdienst ist nicht verfügbar."})");
        }
    });
}
