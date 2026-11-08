#include "handlers/handlers.hpp"
#include "core/http.hpp"
#include "core/request_helpers.hpp"
#include "json.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <codecvt>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <curl/curl.h>

namespace
{
    constexpr size_t kMaxRequestBytes = 128 * 1024;
    constexpr size_t kMaxTextBytes = 20 * 1024;
    constexpr size_t kMaxResponseBytes = 1024 * 1024;
    constexpr int kMaxConcurrentRequests = 4;
    constexpr long kRequestTimeoutMs = 30000;

    std::atomic<int> requests_in_flight{0};
    std::unordered_map<std::string, std::pair<std::chrono::steady_clock::time_point, int>> dictionary_rates;

    nlohmann::json filter_known_words(Database &db, const std::string &user_id,
                                      const std::string &text, const nlohmann::json &matches)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> converter;
        const auto utf16 = converter.from_bytes(text);
        nlohmann::json words = nlohmann::json::array();
        std::vector<size_t> positions;
        for (size_t i = 0; i < matches.size(); ++i)
        {
            const auto &match = matches[i];
            if (match["rule"].value("issueType", std::string()) != "misspelling")
                continue;
            const auto offset = match["offset"].get<int64_t>();
            const auto length = match["length"].get<int64_t>();
            if (offset < 0 || length <= 0 || offset > static_cast<int64_t>(utf16.size()) ||
                length > static_cast<int64_t>(utf16.size()) - offset)
                continue;
            try
            {
                words.push_back(converter.to_bytes(utf16.substr(static_cast<size_t>(offset), static_cast<size_t>(length))));
                positions.push_back(i);
            }
            catch (const std::range_error &)
            {
                // A match splitting a surrogate pair cannot be a dictionary word.
            }
        }
        if (words.empty())
            return matches;
        std::vector<bool> omitted(matches.size(), false);
        for (int index : db.known_proofreading_words(user_id, words))
            if (index >= 0 && static_cast<size_t>(index) < positions.size())
                omitted[positions[static_cast<size_t>(index)]] = true;
        nlohmann::json filtered = nlohmann::json::array();
        for (size_t i = 0; i < matches.size(); ++i)
            if (!omitted[i])
                filtered.push_back(matches[i]);
        return filtered;
    }

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
                    std::string text, Database &db, std::string user_id)
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

        loop->defer([res, aborted, status, response_body = std::move(response_body), text = std::move(text),
                     &db, user_id = std::move(user_id)]() mutable {
            if (*aborted)
                return;
            try
            {
                if (status == 200)
                {
                    auto result = nlohmann::json::parse(response_body);
                    result["matches"] = filter_known_words(db, user_id, text, result["matches"]);
                    response_body = result.dump();
                }
                res->cork([&]() { send_json(res, status, response_body); });
            }
            catch (const std::exception &e)
            {
                res->cork([&]() { send_internal_error(res, "proofreading dictionary lookup", e); });
            }
        });
    }
}

void handle_post_proofreading_check(HttpRes *res, HttpReq *req, Database &db)
{
    TokenClaims claims;
    if (!require_auth(res, req, claims))
        return;
    std::optional<UserRecord> user;
    try { user = resolve_user(db, claims); }
    catch (const std::exception &e) { send_internal_error(res, "proofreading user", e); return; }
    if (!user)
    {
        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
        return;
    }

    auto body = std::make_shared<std::string>();
    auto aborted = std::make_shared<std::atomic_bool>(false);
    auto handled = std::make_shared<bool>(false);
    res->onAborted([aborted]() { *aborted = true; });
    uWS::Loop *loop = uWS::Loop::get();
    res->onData([res, body, aborted, handled, loop, &db, user_id = user->id](std::string_view chunk, bool last) {
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
            std::thread([res, loop, aborted, text, &db, user_id]() {
                try
                {
                    if (!*aborted)
                        check_text(res, loop, aborted, text, db, user_id);
                }

                void handle_proofreading_dictionary(HttpRes *res, HttpReq *req, Database &db, bool remove)
                {
                    TokenClaims claims;
                    if (!require_auth(res, req, claims))
                        return;
                    std::optional<UserRecord> user;
                    try { user = resolve_user(db, claims); }
                    catch (const std::exception &e) { send_internal_error(res, "proofreading user", e); return; }
                    if (!user)
                    {
                        send_json(res, 404, R"({"error":"Benutzer nicht gefunden"})");
                        return;
                    }
                    const auto now = std::chrono::steady_clock::now();
                    for (auto it = dictionary_rates.begin(); it != dictionary_rates.end(); )
                        if (now - it->second.first >= std::chrono::minutes(1)) it = dictionary_rates.erase(it);
                        else ++it;
                    auto &rate = dictionary_rates[user->id];
                    if (rate.second == 0) rate.first = now;
                    if (++rate.second > 30)
                    {
                        send_json(res, 429, R"({"error":"Zu viele Wörterbuchanfragen. Bitte später erneut versuchen."})");
                        return;
                    }
                    auto body = std::make_shared<std::string>();
                    auto handled = std::make_shared<bool>(false);
                    res->onAborted([]() {});
                    res->onData([res, body, handled, &db, user_id = user->id, remove](std::string_view chunk, bool last) {
                        if (*handled) return;
                        if (chunk.size() > 2048 - body->size())
                        {
                            *handled = true;
                            send_json(res, 413, R"({"error":"Der Anfrageinhalt ist zu gross."})");
                            return;
                        }
                        body->append(chunk.data(), chunk.size());
                        if (!last) return;
                        *handled = true;
                        const auto parsed = nlohmann::json::parse(*body, nullptr, false);
                        if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("word") || !parsed["word"].is_string())
                        {
                            send_json(res, 400, R"({"error":"Erwarte ein einzelnes Wort."})");
                            return;
                        }
                        const auto word = parsed["word"].get<std::string>();
                        if (word.empty() || word.size() > 256 || word.find('\0') != std::string::npos)
                        {
                            send_json(res, 400, R"({"error":"Ungültiges Wort."})");
                            return;
                        }
                        try
                        {
                            if (remove)
                            {
                                send_json(res, 200, nlohmann::json{{"removed", db.delete_proofreading_word(user_id, word)}}.dump());
                                return;
                            }
                            const auto result = db.add_proofreading_word(user_id, word);
                            if (result == "invalid")
                                send_json(res, 400, R"({"error":"Nur einzelne Wörter mit Buchstaben sind erlaubt."})");
                            else if (result == "limit")
                                send_json(res, 409, R"({"error":"Das persönliche Wörterbuch ist voll (maximal 500 Wörter)."})");
                            else
                                send_json(res, 200, R"({"added":true})");
                        }
                        catch (const std::exception &e) { send_internal_error(res, "proofreading dictionary", e); }
                    });
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
