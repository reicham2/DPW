#include "handlers.hpp"
#include "handlers/internal/shared.hpp"
#include "app_config.hpp"
#include "models.hpp"
#include "json.hpp"
#include "graph.hpp"
#include "cache.hpp"
#include "utils.hpp"
#include "wp_client.hpp"
#include <string>
#include <memory>
#include <algorithm>
#include <ctime>
#include <map>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <limits>
#include <vector>
#include <utility>
#include <array>
#include <cstdio>
#include <sys/wait.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/rand.h>
#include <curl/curl.h>

#if !defined(DPW_ENABLE_DEBUG_AUTH)
#define DPW_ENABLE_DEBUG_AUTH 0
#endif

namespace
{

    static size_t midata_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *out = static_cast<std::string *>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    std::optional<std::string> configured_public_base_url(Database &db)
    {
        std::string out = app_config::get_or(db, app_config::kPublicBaseUrl, "");
        out = trim_ascii(out);
        while (!out.empty() && out.back() == '/')
            out.pop_back();
        if (out.empty())
            return std::nullopt;
        return out;
    }

    std::string public_base_url(Database &db)
    {
        auto out = configured_public_base_url(db);
        if (!out)
            throw std::runtime_error("Öffentliche Basis-URL ist nicht konfiguriert");
        return *out;
    }

    std::string activity_absolute_link(Database &db, const std::string &activity_id)
    {
        return public_base_url(db) + "/activities/" + activity_id;
    }

    std::string base64url_encode(const std::string &raw)
    {
        if (raw.empty())
            return "";
        std::string out;
        out.resize(4 * ((raw.size() + 2) / 3));
        int len = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                                  reinterpret_cast<const unsigned char *>(raw.data()),
                                  static_cast<int>(raw.size()));
        if (len < 0)
            return "";
        out.resize(static_cast<size_t>(len));
        for (char &c : out)
        {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        while (!out.empty() && out.back() == '=')
            out.pop_back();
        return out;
    }

    std::string base64url_decode(const std::string &in)
    {
        if (in.empty())
            return "";
        std::string b64 = in;
        for (char &c : b64)
        {
            if (c == '-')
                c = '+';
            else if (c == '_')
                c = '/';
        }
        while (b64.size() % 4 != 0)
            b64.push_back('=');

        std::string out;
        out.resize((b64.size() / 4) * 3);
        int len = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                                  reinterpret_cast<const unsigned char *>(b64.data()),
                                  static_cast<int>(b64.size()));
        if (len < 0)
            return "";
        out.resize(static_cast<size_t>(len));
        return out;
    }

    std::string endpoint_origin(const std::string &endpoint)
    {
        auto scheme_pos = endpoint.find("://");
        if (scheme_pos == std::string::npos)
            return "";
        auto start = scheme_pos + 3;
        auto slash = endpoint.find('/', start);
        if (slash == std::string::npos)
            return endpoint;
        return endpoint.substr(0, slash);
    }

    std::optional<std::string> build_vapid_jwt_for_audience(Database &db, const std::string &audience)
    {
        const std::string vapid_pub = app_config::get_or(db, app_config::kVapidPublicKey, "");
        const std::string vapid_priv_b64u = app_config::get_or(db, app_config::kVapidPrivateKey, "");
        const std::string vapid_sub = app_config::get_or(db, app_config::kVapidSubject, "mailto:admin@localhost");
        if (vapid_pub.empty() || vapid_priv_b64u.empty() || audience.empty())
            return std::nullopt;

        const std::string header = R"({"typ":"JWT","alg":"ES256"})";
        const std::time_t now = std::time(nullptr);
        const std::time_t exp = now + (12 * 60 * 60);
        nlohmann::json payload = {
            {"aud", audience},
            {"exp", exp},
            {"sub", vapid_sub},
        };

        const std::string enc_header = base64url_encode(header);
        const std::string enc_payload = base64url_encode(payload.dump());
        if (enc_header.empty() || enc_payload.empty())
            return std::nullopt;

        const std::string signing_input = enc_header + "." + enc_payload;

        std::string priv_raw = base64url_decode(vapid_priv_b64u);
        if (priv_raw.size() < 32)
            return std::nullopt;
        if (priv_raw.size() > 32)
            priv_raw.resize(32);

        EVP_PKEY_CTX *pkey_ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!pkey_ctx)
            return std::nullopt;

        EVP_PKEY *pkey = nullptr;
        if (EVP_PKEY_fromdata_init(pkey_ctx) <= 0)
        {
            EVP_PKEY_CTX_free(pkey_ctx);
            return std::nullopt;
        }

        OSSL_PARAM params[3];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                     const_cast<char *>("prime256v1"),
                                                     0);
        params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PRIV_KEY,
                                                      const_cast<char *>(priv_raw.data()),
                                                      priv_raw.size());
        params[2] = OSSL_PARAM_construct_end();

        if (EVP_PKEY_fromdata(pkey_ctx, &pkey, EVP_PKEY_KEYPAIR, params) <= 0 || !pkey)
        {
            EVP_PKEY_CTX_free(pkey_ctx);
            return std::nullopt;
        }
        EVP_PKEY_CTX_free(pkey_ctx);

        EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
        if (!md_ctx)
        {
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        if (EVP_DigestSignInit(md_ctx, nullptr, EVP_sha256(), nullptr, pkey) <= 0 ||
            EVP_DigestSignUpdate(md_ctx, signing_input.data(), signing_input.size()) <= 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        size_t der_len = 0;
        if (EVP_DigestSignFinal(md_ctx, nullptr, &der_len) <= 0 || der_len == 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }

        std::string der_sig(der_len, '\0');
        if (EVP_DigestSignFinal(md_ctx,
                                reinterpret_cast<unsigned char *>(&der_sig[0]),
                                &der_len) <= 0)
        {
            EVP_MD_CTX_free(md_ctx);
            EVP_PKEY_free(pkey);
            return std::nullopt;
        }
        EVP_MD_CTX_free(md_ctx);
        EVP_PKEY_free(pkey);

        const unsigned char *der_ptr = reinterpret_cast<const unsigned char *>(der_sig.data());
        ECDSA_SIG *sig = d2i_ECDSA_SIG(nullptr, &der_ptr, static_cast<long>(der_len));
        if (!sig)
            return std::nullopt;

        const BIGNUM *r = nullptr;
        const BIGNUM *s = nullptr;
        ECDSA_SIG_get0(sig, &r, &s);
        if (!r || !s)
        {
            ECDSA_SIG_free(sig);
            return std::nullopt;
        }

        std::string sig_raw(64, '\0');
        if (BN_bn2binpad(r, reinterpret_cast<unsigned char *>(&sig_raw[0]), 32) != 32 ||
            BN_bn2binpad(s, reinterpret_cast<unsigned char *>(&sig_raw[32]), 32) != 32)
        {
            ECDSA_SIG_free(sig);
            return std::nullopt;
        }
        ECDSA_SIG_free(sig);

        const std::string enc_sig = base64url_encode(sig_raw);
        if (enc_sig.empty())
            return std::nullopt;
        return signing_input + "." + enc_sig;
    }

    std::optional<std::string> hkdf_sha256(const std::string &key,
                                           const std::string &salt,
                                           const std::string &info,
                                           size_t out_len)
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!ctx)
            return std::nullopt;

        std::string out(out_len, '\0');
        size_t len = out_len;
        bool ok = EVP_PKEY_derive_init(ctx) > 0 &&
                  EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0 &&
                  EVP_PKEY_CTX_set1_hkdf_salt(ctx,
                                              reinterpret_cast<const unsigned char *>(salt.data()),
                                              static_cast<int>(salt.size())) > 0 &&
                  EVP_PKEY_CTX_set1_hkdf_key(ctx,
                                             reinterpret_cast<const unsigned char *>(key.data()),
                                             static_cast<int>(key.size())) > 0 &&
                  EVP_PKEY_CTX_add1_hkdf_info(ctx,
                                              reinterpret_cast<const unsigned char *>(info.data()),
                                              static_cast<int>(info.size())) > 0 &&
                  EVP_PKEY_derive(ctx,
                                  reinterpret_cast<unsigned char *>(&out[0]),
                                  &len) > 0;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return std::nullopt;
        out.resize(len);
        return out;
    }

    std::optional<EVP_PKEY *> ec_public_key_from_uncompressed(const std::string &raw_public_key)
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!ctx)
            return std::nullopt;

        EVP_PKEY *pkey = nullptr;
        if (EVP_PKEY_fromdata_init(ctx) <= 0)
        {
            EVP_PKEY_CTX_free(ctx);
            return std::nullopt;
        }

        OSSL_PARAM params[3];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                     const_cast<char *>("prime256v1"),
                                                     0);
        params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY,
                                                      const_cast<char *>(raw_public_key.data()),
                                                      raw_public_key.size());
        params[2] = OSSL_PARAM_construct_end();

        bool ok = EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) > 0 && pkey;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return std::nullopt;
        return pkey;
    }

    std::optional<EVP_PKEY *> generate_ec_keypair()
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!ctx)
            return std::nullopt;

        EVP_PKEY *pkey = nullptr;
        OSSL_PARAM params[2];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME,
                                                     const_cast<char *>("prime256v1"),
                                                     0);
        params[1] = OSSL_PARAM_construct_end();

        bool ok = EVP_PKEY_keygen_init(ctx) > 0 &&
                  EVP_PKEY_CTX_set_params(ctx, params) > 0 &&
                  EVP_PKEY_generate(ctx, &pkey) > 0 &&
                  pkey;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return std::nullopt;
        return pkey;
    }

    std::optional<std::string> ecdh_shared_secret(EVP_PKEY *private_key, EVP_PKEY *peer_key)
    {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(private_key, nullptr);
        if (!ctx)
            return std::nullopt;

        size_t secret_len = 0;
        bool ok = EVP_PKEY_derive_init(ctx) > 0 &&
                  EVP_PKEY_derive_set_peer(ctx, peer_key) > 0 &&
                  EVP_PKEY_derive(ctx, nullptr, &secret_len) > 0;
        if (!ok || secret_len == 0)
        {
            EVP_PKEY_CTX_free(ctx);
            return std::nullopt;
        }

        std::string secret(secret_len, '\0');
        ok = EVP_PKEY_derive(ctx,
                             reinterpret_cast<unsigned char *>(&secret[0]),
                             &secret_len) > 0;
        EVP_PKEY_CTX_free(ctx);
        if (!ok)
            return std::nullopt;
        secret.resize(secret_len);
        return secret;
    }

    std::optional<std::string> ec_public_key_bytes(EVP_PKEY *pkey)
    {
        size_t pub_len = 0;
        if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &pub_len) <= 0 || pub_len == 0)
            return std::nullopt;

        std::string pub(pub_len, '\0');
        if (EVP_PKEY_get_octet_string_param(pkey,
                                            OSSL_PKEY_PARAM_PUB_KEY,
                                            reinterpret_cast<unsigned char *>(&pub[0]),
                                            pub.size(),
                                            &pub_len) <= 0)
            return std::nullopt;
        pub.resize(pub_len);
        return pub;
    }

    std::optional<std::string> aes_128_gcm_encrypt(const std::string &key,
                                                   const std::string &nonce,
                                                   const std::string &plaintext)
    {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
            return std::nullopt;

        std::string ciphertext(plaintext.size(), '\0');
        int out_len = 0;
        int total_len = 0;
        unsigned char tag[16]{};

        bool ok = EVP_EncryptInit_ex(ctx,
                                     EVP_aes_128_gcm(),
                                     nullptr,
                                     reinterpret_cast<const unsigned char *>(key.data()),
                                     reinterpret_cast<const unsigned char *>(nonce.data())) > 0 &&
                  EVP_EncryptUpdate(ctx,
                                    reinterpret_cast<unsigned char *>(&ciphertext[0]),
                                    &out_len,
                                    reinterpret_cast<const unsigned char *>(plaintext.data()),
                                    static_cast<int>(plaintext.size())) > 0;
        if (ok)
        {
            total_len = out_len;
            ok = EVP_EncryptFinal_ex(ctx,
                                     reinterpret_cast<unsigned char *>(&ciphertext[0]) + total_len,
                                     &out_len) > 0;
            total_len += out_len;
            ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) > 0;
        }

        EVP_CIPHER_CTX_free(ctx);
        if (!ok)
            return std::nullopt;

        ciphertext.resize(total_len);
        ciphertext.append(reinterpret_cast<const char *>(tag), sizeof(tag));
        return ciphertext;
    }

    void append_u32_be(std::string &out, uint32_t value)
    {
        out.push_back(static_cast<char>((value >> 24) & 0xff));
        out.push_back(static_cast<char>((value >> 16) & 0xff));
        out.push_back(static_cast<char>((value >> 8) & 0xff));
        out.push_back(static_cast<char>(value & 0xff));
    }

    std::string notification_push_body(const NotificationRecord &notification)
    {
        if (notification.payload.is_object() &&
            notification.payload.contains("activity_date_display") &&
            notification.payload["activity_date_display"].is_string())
        {
            std::string date_short = trim_ascii(notification.payload["activity_date_display"].get<std::string>());
            if (!date_short.empty())
                return "Datum: " + date_short;
        }
        return "Neue Benachrichtigung";
    }

    std::optional<std::string> build_web_push_body(const PushSubscriptionRecord &subscription,
                                                   const NotificationRecord &notification)
    {
        const std::string receiver_public = base64url_decode(subscription.p256dh);
        const std::string auth_secret = base64url_decode(subscription.auth);
        if (receiver_public.empty() || auth_secret.empty())
            return std::nullopt;

        auto local_key = generate_ec_keypair();
        if (!local_key)
            return std::nullopt;
        auto peer_key = ec_public_key_from_uncompressed(receiver_public);
        if (!peer_key)
        {
            EVP_PKEY_free(*local_key);
            return std::nullopt;
        }

        auto local_public = ec_public_key_bytes(*local_key);
        auto shared_secret = ecdh_shared_secret(*local_key, *peer_key);
        EVP_PKEY_free(*peer_key);
        if (!local_public || !shared_secret)
        {
            EVP_PKEY_free(*local_key);
            return std::nullopt;
        }

        std::string key_info = "WebPush: info";
        key_info.push_back('\0');
        key_info += receiver_public;
        key_info += *local_public;

        auto ikm = hkdf_sha256(*shared_secret, auth_secret, key_info, 32);
        if (!ikm)
        {
            EVP_PKEY_free(*local_key);
            return std::nullopt;
        }

        unsigned char salt_raw[16]{};
        if (RAND_bytes(salt_raw, sizeof(salt_raw)) != 1)
        {
            EVP_PKEY_free(*local_key);
            return std::nullopt;
        }
        std::string salt(reinterpret_cast<const char *>(salt_raw), sizeof(salt_raw));

        auto cek = hkdf_sha256(*ikm, salt, std::string("Content-Encoding: aes128gcm\0", 27), 16);
        auto nonce = hkdf_sha256(*ikm, salt, std::string("Content-Encoding: nonce\0", 25), 12);
        if (!cek || !nonce)
        {
            EVP_PKEY_free(*local_key);
            return std::nullopt;
        }

        nlohmann::json json_payload = {
            {"title", notification.title},
            {"body", notification_push_body(notification)},
            {"url", notification.link ? *notification.link : std::string("/profile")}};
        std::string plaintext = json_payload.dump();
        plaintext.push_back('\x02');

        auto encrypted = aes_128_gcm_encrypt(*cek, *nonce, plaintext);
        EVP_PKEY_free(*local_key);
        if (!encrypted)
            return std::nullopt;

        std::string out;
        out.reserve(16 + 4 + 1 + local_public->size() + encrypted->size());
        out += salt;
        append_u32_be(out, 4096);
        out.push_back(static_cast<char>(local_public->size()));
        out += *local_public;
        out += *encrypted;
        return out;
    }

    long send_web_push(const PushSubscriptionRecord &subscription,
                       const NotificationRecord &notification,
                       const std::string &jwt,
                       const std::string &vapid_pub)
    {
        auto body = build_web_push_body(subscription, notification);
        if (!body)
            return 0;

        CURL *curl = curl_easy_init();
        if (!curl)
            return 0;

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "TTL: 60");
        headers = curl_slist_append(headers, "Urgency: normal");
        headers = curl_slist_append(headers, "Content-Encoding: aes128gcm");
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        std::string auth = "Authorization: vapid t=" + jwt + ", k=" + vapid_pub;
        headers = curl_slist_append(headers, auth.c_str());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, subscription.endpoint.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, midata_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        if (rc == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK)
            return 0;
        return status;
    }

    void deliver_web_push_for_user(Database &db, const UserRecord &user, const NotificationRecord &notification)
    {
        const std::string vapid_pub = app_config::get_or(db, app_config::kVapidPublicKey, "");
        if (vapid_pub.empty())
            return;

        auto subscriptions = db.list_push_subscriptions_for_user(user.id);
        for (const auto &sub : subscriptions)
        {
            std::string aud = endpoint_origin(sub.endpoint);
            if (aud.empty())
                continue;

            auto jwt = build_vapid_jwt_for_audience(db, aud);
            if (!jwt)
                continue;

            long status = send_web_push(sub, notification, *jwt, vapid_pub);
            // Endpoint no longer valid.
            if (status == 404 || status == 410)
                db.delete_push_subscription_by_endpoint(sub.endpoint);
        }
    }

    std::string format_date_ddmmyyyy(const std::string &iso)
    {
        if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-')
            return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
        return iso;
    }

    std::string join_display(const std::vector<std::string> &items)
    {
        std::string out;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].empty())
                continue;
            if (!out.empty())
                out += ", ";
            out += items[i];
        }
        if (out.empty())
            return "-";
        return out;
    }

    std::vector<std::string> unique_names_from_material(const std::vector<MaterialItem> &material)
    {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;
        for (const auto &m : material)
        {
            for (const auto &name : m.responsible)
            {
                std::string t = trim_ascii(name);
                if (t.empty())
                    continue;
                std::string key = to_lower_ascii(t);
                if (seen.insert(key).second)
                    out.push_back(t);
            }
        }
        return out;
    }

    std::string normalize_assignee_key(const std::string &name)
    {
        return to_lower_ascii(trim_ascii(name));
    }

    std::string normalize_material_key(const std::string &name)
    {
        return to_lower_ascii(trim_ascii(name));
    }

    bool user_matches_assignee(const UserRecord &user, const std::string &assignee)
    {
        const std::string assignee_key = normalize_assignee_key(assignee);
        if (assignee_key.empty())
            return false;

        if (assignee_key == normalize_assignee_key(user.display_name))
            return true;
        if (assignee_key == normalize_assignee_key(user.email))
            return true;

        const auto at = user.email.find('@');
        if (at != std::string::npos)
        {
            const std::string mail_local = user.email.substr(0, at);
            if (assignee_key == normalize_assignee_key(mail_local))
                return true;
        }

        return false;
    }

    bool user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user)
    {
        for (const auto &assignee : assignees)
        {
            if (user_matches_assignee(user, assignee))
                return true;
        }
        return false;
    }

    std::vector<std::string> newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                      const std::vector<MaterialItem> &new_material)
    {
        std::unordered_set<std::string> old_pairs;
        for (const auto &m : old_material)
        {
            const std::string material_key = normalize_material_key(m.name);
            if (material_key.empty())
                continue;
            for (const auto &name : m.responsible)
            {
                const std::string assignee_key = normalize_assignee_key(name);
                if (assignee_key.empty())
                    continue;
                old_pairs.insert(material_key + "\n" + assignee_key);
            }
        }

        std::vector<std::string> out;
        std::unordered_set<std::string> seen_assignees;
        for (const auto &m : new_material)
        {
            const std::string material_key = normalize_material_key(m.name);
            if (material_key.empty())
                continue;
            for (const auto &name : m.responsible)
            {
                const std::string trimmed_name = trim_ascii(name);
                const std::string assignee_key = normalize_assignee_key(trimmed_name);
                if (assignee_key.empty())
                    continue;

                const std::string pair_key = material_key + "\n" + assignee_key;
                if (old_pairs.find(pair_key) != old_pairs.end())
                    continue;

                if (seen_assignees.insert(assignee_key).second)
                    out.push_back(trimmed_name);
            }
        }
        return out;
    }

    bool contains_name_ci(const std::vector<std::string> &names, const std::string &needle)
    {
        std::string key = to_lower_ascii(trim_ascii(needle));
        if (key.empty())
            return false;
        for (const auto &n : names)
        {
            if (to_lower_ascii(trim_ascii(n)) == key)
                return true;
        }
        return false;
    }

    std::string sanitize_meteo_text(std::string s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c >= 32 && c <= 126)
            {
                out.push_back(static_cast<char>(c));
                continue;
            }

            switch (c)
            {
            case 0xE4:
                out += "ae";
                break; // ä
            case 0xF6:
                out += "oe";
                break; // ö
            case 0xFC:
                out += "ue";
                break; // ü
            case 0xC4:
                out += "Ae";
                break; // Ä
            case 0xD6:
                out += "Oe";
                break; // Ö
            case 0xDC:
                out += "Ue";
                break; // Ü
            case 0xDF:
                out += "ss";
                break; // ß
            default:
                // skip non-ASCII control/extended bytes to keep JSON UTF-8 safe
                break;
            }
        }
        return trim_ascii(out);
    }

    const nlohmann::json *find_people_array(const nlohmann::json &payload)
    {
        if (payload.is_array())
            return &payload;
        if (!payload.is_object())
            return nullptr;

        static const char *keys[] = {"people", "members", "participants", "entries", "data"};
        for (const char *k : keys)
        {
            if (payload.contains(k) && payload[k].is_array())
                return &payload[k];
        }
        return nullptr;
    }

    std::optional<int> fetch_midata_children_count(Database &db,
                                                   const std::string &group_id,
                                                   std::string &error)
    {
        std::string api_key = app_config::get_or(db, app_config::kMidataApiKey, "");
        if (api_key.empty())
        {
            error = "not-configured";
            return std::nullopt;
        }

        std::string url_tmpl = app_config::get_or(db,
                                                  app_config::kMidataApiUrlTemplate,
                                                  "https://db.scout.ch/de/groups/{group_id}/people.json");

        CURL *escape_curl = curl_easy_init();
        if (!escape_curl)
        {
            error = "curl-init-failed";
            return std::nullopt;
        }
        char *escaped_group = curl_easy_escape(escape_curl, group_id.c_str(), static_cast<int>(group_id.size()));
        if (!escaped_group)
        {
            curl_easy_cleanup(escape_curl);
            error = "url-escape-failed";
            return std::nullopt;
        }

        std::string url = url_tmpl;
        auto pos = url.find("{group_id}");
        if (pos != std::string::npos)
            url.replace(pos, std::string("{group_id}").size(), escaped_group);
        curl_free(escaped_group);
        curl_easy_cleanup(escape_curl);

        if (url.find("include=") == std::string::npos)
            url += (url.find('?') == std::string::npos) ? "?include=roles" : "&include=roles";

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            error = "curl-init-failed";
            return std::nullopt;
        }

        std::string body;
        struct curl_slist *headers = nullptr;
        std::string header_name = "X-Token";
        std::string auth_header = header_name + ": " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
        headers = curl_slist_append(headers, "Accept: application/json");

        long timeout_ms = app_config::get_int_or(db, app_config::kMidataApiTimeoutMs, 8000, 1000, 60000);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, midata_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK)
        {
            error = "request-failed";
            return std::nullopt;
        }
        if (status < 200 || status >= 300)
        {
            error = "http-status-" + std::to_string(status);
            return std::nullopt;
        }

        auto payload = nlohmann::json::parse(body, nullptr, false);
        if (payload.is_discarded())
        {
            error = "invalid-json";
            return std::nullopt;
        }

        const nlohmann::json *people = find_people_array(payload);
        if (!people)
        {
            error = "people-array-missing";
            return std::nullopt;
        }

        int count = 0;
        for (const auto &entry : *people)
        {
            if (!entry.is_object())
                continue;
            bool has_roles = entry.contains("links") &&
                             entry["links"].is_object() &&
                             entry["links"].contains("roles") &&
                             entry["links"]["roles"].is_array() &&
                             !entry["links"]["roles"].empty();
            if (!has_roles)
                ++count;
        }
        return count;
    }

    struct MeteoPointMeta
    {
        int point_id = 0;
        int point_type_id = 0;
        std::string postal_code;
        std::string point_name;
    };

    struct MidataCountCacheEntry
    {
        std::optional<int> count;
        bool configured = false;
        std::string error;
        std::chrono::steady_clock::time_point expires_at;
    };

    std::mutex meteo_cache_mutex;
    std::vector<MeteoPointMeta> meteo_point_cache;
    std::chrono::steady_clock::time_point meteo_point_cache_expires;
    // Keep Meteo upstream calls short to avoid blocking the single API event loop.
    constexpr long kMeteoHttpTimeoutMs = 500;
    std::mutex midata_cache_mutex;
    std::unordered_map<std::string, MidataCountCacheEntry> midata_count_cache;
    constexpr auto kMidataCountCacheTtl = std::chrono::minutes(5);

    std::optional<int> get_cached_midata_children_count(const std::string &group_id,
                                                        std::string &error,
                                                        bool &configured)
    {
        std::lock_guard<std::mutex> lock(midata_cache_mutex);
        auto it = midata_count_cache.find(group_id);
        if (it == midata_count_cache.end())
            return std::nullopt;
        if (std::chrono::steady_clock::now() >= it->second.expires_at)
        {
            midata_count_cache.erase(it);
            return std::nullopt;
        }

        error = it->second.error;
        configured = it->second.configured;
        return it->second.count;
    }

    void store_cached_midata_children_count(const std::string &group_id,
                                            const std::optional<int> &count,
                                            bool configured,
                                            const std::string &error)
    {
        std::lock_guard<std::mutex> lock(midata_cache_mutex);
        midata_count_cache[group_id] = MidataCountCacheEntry{
            .count = count,
            .configured = configured,
            .error = error,
            .expires_at = std::chrono::steady_clock::now() + kMidataCountCacheTtl,
        };
    }

    void clear_cached_midata_children_counts()
    {
        std::lock_guard<std::mutex> lock(midata_cache_mutex);
        midata_count_cache.clear();
    }

    std::optional<int> fetch_midata_children_count_cached(Database &db,
                                                          const std::string &group_id,
                                                          std::string &error)
    {
        bool configured = false;
        auto cached = get_cached_midata_children_count(group_id, error, configured);
        if (cached || configured || !error.empty())
            return cached;

        auto count = fetch_midata_children_count(db, group_id, error);
        configured = count.has_value() || error != "not-configured";
        store_cached_midata_children_count(group_id, count, configured, error);
        return count;
    }

    std::optional<std::string> http_get_text(const std::string &url, long timeout_ms, std::string &error)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            error = "curl-init-failed";
            return std::nullopt;
        }
        std::string body;
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json,text/csv,*/*");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, midata_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK)
        {
            error = "request-failed";
            return std::nullopt;
        }
        if (status < 200 || status >= 300)
        {
            error = "http-status-" + std::to_string(status);
            return std::nullopt;
        }
        return body;
    }

    time_t parse_compact_utc(const std::string &s)
    {
        if (s.size() != 12)
            return -1;
        std::tm tm{};
        tm.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(s.substr(4, 2)) - 1;
        tm.tm_mday = std::stoi(s.substr(6, 2));
        tm.tm_hour = std::stoi(s.substr(8, 2));
        tm.tm_min = std::stoi(s.substr(10, 2));
        tm.tm_sec = 0;
        return timegm(&tm);
    }

    int days_in_month(int year, int month)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12)
            return 31;
        if (month != 2)
            return days[month - 1];
        bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap_year ? 29 : 28;
    }

    int weekday_utc(int year, int month, int day)
    {
        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = 12;
        time_t ts = timegm(&tm);
        std::tm *utc = gmtime(&ts);
        return utc ? utc->tm_wday : 0;
    }

    int last_sunday_of_month(int year, int month)
    {
        int last_day = days_in_month(year, month);
        int weekday = weekday_utc(year, month, last_day);
        return last_day - weekday;
    }

    bool is_switzerland_dst_local(int year, int month, int day, int hour)
    {
        if (month < 3 || month > 10)
            return false;
        if (month > 3 && month < 10)
            return true;

        if (month == 3)
        {
            int change_day = last_sunday_of_month(year, 3);
            if (day > change_day)
                return true;
            if (day < change_day)
                return false;
            return hour >= 2;
        }

        int change_day = last_sunday_of_month(year, 10);
        if (day < change_day)
            return true;
        if (day > change_day)
            return false;
        return hour < 3;
    }

    time_t parse_activity_local_to_utc(const std::string &date, const std::string &time)
    {
        if (date.size() < 10 || time.size() < 5)
            return -1;

        int year = std::stoi(date.substr(0, 4));
        int month = std::stoi(date.substr(5, 2));
        int day = std::stoi(date.substr(8, 2));
        int hour = std::stoi(time.substr(0, 2));
        int minute = std::stoi(time.substr(3, 2));

        std::tm tm{};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = 0;

        time_t local_as_utc = timegm(&tm);
        int offset_seconds = is_switzerland_dst_local(year, month, day, hour) ? 2 * 3600 : 1 * 3600;
        return local_as_utc - offset_seconds;
    }

    time_t parse_activity_start_utc(const std::string &date, const std::string &start_time)
    {
        return parse_activity_local_to_utc(date, start_time);
    }

    time_t parse_activity_end_utc(const std::string &date, const std::string &end_time, time_t start_ts)
    {
        time_t end_ts = parse_activity_local_to_utc(date, end_time);
        if (start_ts > 0 && end_ts > 0 && end_ts <= start_ts)
            end_ts += 24 * 3600;
        return end_ts;
    }

    std::string season_from_month(int month)
    {
        if (month == 12 || month <= 2)
            return "Winter";
        if (month <= 5)
            return "Fruehling";
        if (month <= 8)
            return "Sommer";
        return "Herbst";
    }

    double seasonal_avg_temp_for_month(int month)
    {
        if (month == 12 || month <= 2)
            return 2.0;
        if (month <= 5)
            return 11.0;
        if (month <= 8)
            return 21.0;
        return 12.0;
    }

    WeatherResult seasonal_average_weather(time_t activity_ts, const std::string &note)
    {
        std::tm *ptm = gmtime(&activity_ts);
        int month = ptm ? (ptm->tm_mon + 1) : 1;
        WeatherResult out;
        out.mode = "seasonal-average";
        out.temperature_c = seasonal_avg_temp_for_month(month);
        out.temperature_min_c = out.temperature_c - 3.0;
        out.temperature_max_c = out.temperature_c + 3.0;
        out.season = season_from_month(month);
        out.weather_symbol = (month == 12 || month <= 2) ? "cloud" : "partly-cloudy";
        out.source = "MeteoSwiss (saisonaler Durchschnitt CH)";
        out.note = note;
        return out;
    }

    std::string pick_weather_symbol(double temp_c, const std::optional<int> &rain_probability_percent)
    {
        if (rain_probability_percent && *rain_probability_percent >= 65)
            return "rain";
        if (temp_c <= 0.0 && rain_probability_percent && *rain_probability_percent >= 45)
            return "snow";
        if (rain_probability_percent && *rain_probability_percent >= 35)
            return "partly-cloudy";
        return "sun";
    }

    std::optional<std::vector<MeteoPointMeta>> load_meteoswiss_points(std::string &error)
    {
        {
            std::lock_guard<std::mutex> lock(meteo_cache_mutex);
            if (!meteo_point_cache.empty() && std::chrono::steady_clock::now() < meteo_point_cache_expires)
                return meteo_point_cache;
        }

        auto collection_body = http_get_text("https://data.geo.admin.ch/api/stac/v1/collections/ch.meteoschweiz.ogd-local-forecasting", kMeteoHttpTimeoutMs, error);
        if (!collection_body)
            return std::nullopt;
        auto collection = nlohmann::json::parse(*collection_body, nullptr, false);
        if (collection.is_discarded() || !collection.is_object() || !collection.contains("assets"))
        {
            error = "meteo-collection-invalid";
            return std::nullopt;
        }
        std::string point_meta_url;
        if (collection["assets"].contains("ogd-local-forecasting_meta_point.csv") &&
            collection["assets"]["ogd-local-forecasting_meta_point.csv"].is_object() &&
            collection["assets"]["ogd-local-forecasting_meta_point.csv"].contains("href"))
        {
            point_meta_url = collection["assets"]["ogd-local-forecasting_meta_point.csv"]["href"].get<std::string>();
        }
        if (point_meta_url.empty())
        {
            error = "meteo-point-meta-url-missing";
            return std::nullopt;
        }

        auto csv_body = http_get_text(point_meta_url, kMeteoHttpTimeoutMs, error);
        if (!csv_body)
            return std::nullopt;

        std::vector<MeteoPointMeta> points;
        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 5)
                continue;
            MeteoPointMeta p;
            try
            {
                p.point_id = std::stoi(cols[0]);
                p.point_type_id = std::stoi(cols[1]);
            }
            catch (...)
            {
                continue;
            }
            p.postal_code = sanitize_meteo_text(cols[3]);
            p.point_name = sanitize_meteo_text(cols[4]);
            points.push_back(std::move(p));
        }

        if (points.empty())
        {
            error = "meteo-point-meta-empty";
            return std::nullopt;
        }

        {
            std::lock_guard<std::mutex> lock(meteo_cache_mutex);
            meteo_point_cache = points;
            meteo_point_cache_expires = std::chrono::steady_clock::now() + std::chrono::hours(6);
        }
        return points;
    }

    std::optional<MeteoPointMeta> find_point_for_location(const std::string &location,
                                                          const std::vector<MeteoPointMeta> &points)
    {
        std::smatch m;
        std::regex zip_re("(^|[^0-9])([0-9]{4})([^0-9]|$)");
        if (std::regex_search(location, m, zip_re) && m.size() >= 3)
        {
            std::string zip = m[2].str();
            for (const auto &p : points)
            {
                if (p.postal_code == zip && p.point_type_id != 1)
                    return p;
            }
            for (const auto &p : points)
            {
                if (p.postal_code == zip)
                    return p;
            }

            // Fallback for postal-code-only inputs: choose nearest available postal code.
            int zip_num = -1;
            try
            {
                zip_num = std::stoi(zip);
            }
            catch (...)
            {
                zip_num = -1;
            }
            if (zip_num >= 0)
            {
                const MeteoPointMeta *best = nullptr;
                int best_diff = std::numeric_limits<int>::max();
                for (const auto &p : points)
                {
                    if (p.postal_code.size() != 4)
                        continue;
                    int p_zip_num = -1;
                    try
                    {
                        p_zip_num = std::stoi(p.postal_code);
                    }
                    catch (...)
                    {
                        continue;
                    }
                    int diff = std::abs(p_zip_num - zip_num);
                    if (!best || diff < best_diff ||
                        (diff == best_diff && best->point_type_id == 1 && p.point_type_id != 1))
                    {
                        best = &p;
                        best_diff = diff;
                    }
                }
                if (best)
                    return *best;
            }
        }

        std::string loc_lower = to_lower_ascii(trim_ascii(location));
        if (loc_lower.empty())
            return std::nullopt;

        for (const auto &p : points)
        {
            if (!p.point_name.empty())
            {
                std::string point_lower = to_lower_ascii(p.point_name);
                if (loc_lower.find(point_lower) != std::string::npos || point_lower.find(loc_lower) != std::string::npos)
                    return p;
            }
        }

        // Token fallback for city names inside longer location strings.
        std::string token;
        std::vector<std::string> tokens;
        for (char c : loc_lower)
        {
            if (std::isalnum(static_cast<unsigned char>(c)))
            {
                token.push_back(c);
            }
            else if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        }
        if (!token.empty())
            tokens.push_back(token);

        for (const auto &t : tokens)
        {
            if (t.size() < 3)
                continue;
            bool digits_only = std::all_of(t.begin(), t.end(), [](unsigned char c)
                                           { return std::isdigit(c); });
            if (digits_only)
                continue;

            for (const auto &p : points)
            {
                if (p.point_name.empty())
                    continue;
                std::string point_lower = to_lower_ascii(p.point_name);
                if (point_lower.find(t) != std::string::npos)
                    return p;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> get_latest_asset_url(const std::string &asset_suffix, std::string &error)
    {
        auto body = http_get_text("https://data.geo.admin.ch/api/stac/v1/collections/ch.meteoschweiz.ogd-local-forecasting/items?limit=1", kMeteoHttpTimeoutMs, error);
        if (!body)
            return std::nullopt;
        auto payload = nlohmann::json::parse(*body, nullptr, false);
        if (payload.is_discarded() || !payload.contains("features") || !payload["features"].is_array() || payload["features"].empty())
        {
            error = "meteo-items-invalid";
            return std::nullopt;
        }
        const auto &feature = payload["features"][0];
        if (!feature.contains("assets") || !feature["assets"].is_object())
        {
            error = "meteo-assets-missing";
            return std::nullopt;
        }
        for (auto it = feature["assets"].begin(); it != feature["assets"].end(); ++it)
        {
            if (!it.value().is_object() || !it.value().contains("href") || !it.value()["href"].is_string())
                continue;
            std::string href = it.value()["href"].get<std::string>();
            if (href.find(asset_suffix) != std::string::npos)
                return href;
        }
        error = "meteo-asset-missing-" + asset_suffix;
        return std::nullopt;
    }

    std::optional<std::string> get_latest_tre200h0_url(std::string &error)
    {
        return get_latest_asset_url("tre200h0.csv", error);
    }

    std::optional<std::string> get_latest_rre150h0_url(std::string &error)
    {
        return get_latest_asset_url("rre150h0.csv", error);
    }

    std::optional<double> lookup_forecast_temp_for_point(const std::string &csv_url,
                                                         int point_id,
                                                         time_t target_ts,
                                                         std::string &error)
    {
        auto csv_body = http_get_text(csv_url, kMeteoHttpTimeoutMs, error);
        if (!csv_body)
            return std::nullopt;

        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        long long best_diff = std::numeric_limits<long long>::max();
        std::optional<double> best_temp = std::nullopt;
        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 4)
                continue;
            int row_point_id = 0;
            try
            {
                row_point_id = std::stoi(cols[0]);
            }
            catch (...)
            {
                continue;
            }
            if (row_point_id != point_id)
                continue;
            time_t row_ts = parse_compact_utc(cols[2]);
            if (row_ts <= 0)
                continue;
            double temp = 0.0;
            try
            {
                temp = std::stod(cols[3]);
            }
            catch (...)
            {
                continue;
            }
            long long diff = std::llabs(static_cast<long long>(row_ts) - static_cast<long long>(target_ts));
            if (diff < best_diff)
            {
                best_diff = diff;
                best_temp = temp;
            }
        }
        if (!best_temp)
        {
            error = "meteo-temp-point-missing";
            return std::nullopt;
        }
        return best_temp;
    }

    std::optional<int> lookup_rain_probability_for_point(const std::string &csv_url,
                                                         int point_id,
                                                         time_t target_ts,
                                                         std::string &error)
    {
        auto precip_mm = lookup_forecast_temp_for_point(csv_url, point_id, target_ts, error);
        if (!precip_mm)
            return std::nullopt;

        auto precip_mm_to_probability = [](double precip_mm)
        {
            // rre150h0 is precipitation amount; map it to a simple probability for UI display.
            if (precip_mm <= 0.0)
                return 5;

            int probability = static_cast<int>(15.0 + (precip_mm * 25.0));
            if (probability < 5)
                probability = 5;
            if (probability > 95)
                probability = 95;
            return probability;
        };

        return precip_mm_to_probability(*precip_mm);
    }

    std::optional<std::vector<std::pair<time_t, double>>> lookup_forecast_series_for_point(const std::string &csv_url,
                                                                                           int point_id,
                                                                                           time_t start_ts,
                                                                                           time_t end_ts,
                                                                                           std::string &error)
    {
        auto csv_body = http_get_text(csv_url, kMeteoHttpTimeoutMs, error);
        if (!csv_body)
            return std::nullopt;

        std::istringstream iss(*csv_body);
        std::string line;
        bool first = true;
        std::vector<std::pair<time_t, double>> points;

        while (std::getline(iss, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (first)
            {
                first = false;
                continue;
            }

            std::vector<std::string> cols;
            std::stringstream ls(line);
            std::string col;
            while (std::getline(ls, col, ';'))
                cols.push_back(col);
            if (cols.size() < 4)
                continue;

            int row_point_id = 0;
            try
            {
                row_point_id = std::stoi(cols[0]);
            }
            catch (...)
            {
                continue;
            }
            if (row_point_id != point_id)
                continue;

            time_t row_ts = parse_compact_utc(cols[2]);
            if (row_ts <= 0 || row_ts < start_ts || row_ts > end_ts)
                continue;

            double temp = 0.0;
            try
            {
                temp = std::stod(cols[3]);
            }
            catch (...)
            {
                continue;
            }

            points.push_back({row_ts, temp});
        }

        if (points.empty())
        {
            error = "meteo-temp-range-missing";
            return std::nullopt;
        }

        std::sort(points.begin(), points.end(), [](const auto &a, const auto &b)
                  { return a.first < b.first; });
        return points;
    }

    std::optional<WeatherResult> fetch_expected_weather_for_activity(const Activity &activity,
                                                                     const std::string &location_input,
                                                                     std::string &error)
    {
        time_t activity_ts = parse_activity_start_utc(activity.date, activity.start_time);
        if (activity_ts <= 0)
        {
            error = "activity-datetime-invalid";
            return std::nullopt;
        }

        time_t activity_end_ts = parse_activity_end_utc(activity.date, activity.end_time, activity_ts);
        if (activity_end_ts <= 0)
        {
            activity_end_ts = activity_ts;
        }

        time_t now = time(nullptr);
        constexpr time_t kForecastHorizonSeconds = 9 * 24 * 3600;
        if (activity_ts > now + kForecastHorizonSeconds)
        {
            return seasonal_average_weather(activity_ts, "Termin liegt ausserhalb des MeteoSwiss-Vorhersagehorizonts.");
        }

        if (location_input.empty())
        {
            error = "location-missing";
            return std::nullopt;
        }

        auto points = load_meteoswiss_points(error);
        if (!points)
            return seasonal_average_weather(activity_ts, "MeteoSwiss-Datenquelle derzeit nicht erreichbar; saisonaler Durchschnitt verwendet.");
        auto point = find_point_for_location(location_input, *points);
        if (!point)
        {
            return seasonal_average_weather(activity_ts, "Ort konnte nicht auf MeteoSwiss-Punkt gemappt werden.");
        }

        auto tre_url = get_latest_tre200h0_url(error);
        if (!tre_url)
            return seasonal_average_weather(activity_ts, "MeteoSwiss-Vorhersagedaten momentan nicht verfuegbar; saisonaler Durchschnitt verwendet.");
        auto temp = lookup_forecast_temp_for_point(*tre_url, point->point_id, activity_ts, error);
        if (!temp)
        {
            return seasonal_average_weather(activity_ts, "Keine punktgenaue Vorhersage verfuegbar; saisonaler Durchschnitt verwendet.");
        }

        std::string range_error;
        auto temp_series = lookup_forecast_series_for_point(*tre_url, point->point_id, activity_ts, activity_end_ts, range_error);

        WeatherResult out;
        out.mode = "forecast";
        out.temperature_c = *temp;
        if (temp_series)
        {
            out.hourly_temps = *temp_series;
            double min_temp = out.hourly_temps.front().second;
            double max_temp = out.hourly_temps.front().second;
            for (const auto &p : out.hourly_temps)
            {
                min_temp = std::min(min_temp, p.second);
                max_temp = std::max(max_temp, p.second);
            }
            out.temperature_min_c = min_temp;
            out.temperature_max_c = max_temp;
            out.temperature_c = (min_temp + max_temp) / 2.0;
        }
        else
        {
            out.temperature_min_c = *temp;
            out.temperature_max_c = *temp;
        }
        std::string rain_error;
        auto rain_url = get_latest_rre150h0_url(rain_error);
        if (rain_url)
        {
            out.rain_probability_percent = lookup_rain_probability_for_point(*rain_url, point->point_id, activity_ts, rain_error);
            auto rain_series = lookup_forecast_series_for_point(*rain_url, point->point_id, activity_ts, activity_end_ts, rain_error);
            if (rain_series)
            {
                for (const auto &p : *rain_series)
                {
                    double precip_mm = p.second;
                    int probability = 5;
                    if (precip_mm > 0.0)
                    {
                        probability = static_cast<int>(15.0 + (precip_mm * 25.0));
                        if (probability > 95)
                            probability = 95;
                    }
                    out.hourly_rain_probability.push_back({p.first, probability});
                }
            }
        }
        out.weather_symbol = pick_weather_symbol(out.temperature_c, out.rain_probability_percent);
        out.point_name = point->point_name;
        out.postal_code = point->postal_code;
        out.source = "MeteoSwiss Local Forecast";
        out.note = "Vorhersage basiert auf tre200h0 (stundlicher Mittelwert, naechster Zeitpunkt).";
        return out;
    }

    struct PublicSubmitBucket
    {
        std::deque<std::chrono::steady_clock::time_point> attempts;
    };

    std::mutex public_submit_rate_limit_mutex;
    std::unordered_map<std::string, PublicSubmitBucket> public_submit_rate_limit;

    constexpr size_t kMaxPublicFormPayloadBytes = 128 * 1024;
    constexpr size_t kPublicSubmitBurstLimit = 3;
    constexpr size_t kPublicSubmitSustainedLimit = 12;
    constexpr auto kPublicSubmitBurstWindow = std::chrono::seconds(30);
    constexpr auto kPublicSubmitSustainedWindow = std::chrono::minutes(10);
    constexpr size_t kMaxPublicAnswerLength = 4000;

    std::string normalize_public_submit_client_key(const std::string &ip_address, const std::string &user_agent)
    {
        if (!ip_address.empty())
            return ip_address;
        if (user_agent.empty())
            return "anonymous";
        return user_agent.substr(0, std::min<size_t>(user_agent.size(), 160));
    }

    bool is_public_submit_rate_limited(const std::string &public_slug, const std::string &client_key)
    {
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - kPublicSubmitSustainedWindow;
        std::lock_guard<std::mutex> lock(public_submit_rate_limit_mutex);
        auto &bucket = public_submit_rate_limit[public_slug + "|" + client_key];

        while (!bucket.attempts.empty() && bucket.attempts.front() < cutoff)
            bucket.attempts.pop_front();

        size_t burst_count = 0;
        auto burst_cutoff = now - kPublicSubmitBurstWindow;
        for (auto it = bucket.attempts.rbegin(); it != bucket.attempts.rend(); ++it)
        {
            if (*it < burst_cutoff)
                break;
            ++burst_count;
        }

        if (burst_count >= kPublicSubmitBurstLimit || bucket.attempts.size() >= kPublicSubmitSustainedLimit)
            return true;

        bucket.attempts.push_back(now);
        return false;
    }

    std::string utc_today_ymd()
    {
        time_t now = time(nullptr);
        std::tm *tm = gmtime(&now);
        char buf[11] = {0};
        if (!tm)
            return "";
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return std::string(buf);
    }

    bool should_use_frozen_values(const Activity &activity)
    {
        const std::string today = utc_today_ymd();
        if (today.empty())
            return false;
        return !activity.date.empty() && activity.date < today;
    }

} // namespace

std::optional<int> shared_fetch_midata_children_count_cached(Database &db,
                                                             const std::string &group_id,
                                                             std::string &error)
{
    return fetch_midata_children_count_cached(db, group_id, error);
}

std::optional<WeatherResult> shared_fetch_expected_weather_for_activity(const Activity &activity,
                                                                        const std::string &location_input,
                                                                        std::string &error)
{
    return fetch_expected_weather_for_activity(activity, location_input, error);
}

bool shared_should_use_frozen_values(const Activity &activity)
{
    return should_use_frozen_values(activity);
}

std::string shared_activity_absolute_link(Database &db, const std::string &activity_id)
{
    return activity_absolute_link(db, activity_id);
}

void shared_deliver_web_push_for_user(Database &db, const UserRecord &user, const NotificationRecord &notification)
{
    deliver_web_push_for_user(db, user, notification);
}

std::string shared_format_date_ddmmyyyy(const std::string &iso)
{
    return format_date_ddmmyyyy(iso);
}

std::string shared_join_display(const std::vector<std::string> &items)
{
    return join_display(items);
}

std::vector<std::string> shared_unique_names_from_material(const std::vector<MaterialItem> &material)
{
    return unique_names_from_material(material);
}

bool shared_user_in_assignee_list(const std::vector<std::string> &assignees, const UserRecord &user)
{
    return user_in_assignee_list(assignees, user);
}

bool shared_contains_name_ci(const std::vector<std::string> &names, const std::string &needle)
{
    return contains_name_ci(names, needle);
}

void shared_clear_cached_midata_children_counts()
{
    clear_cached_midata_children_counts();
}

std::string shared_normalize_assignee_key(const std::string &name)
{
    return normalize_assignee_key(name);
}

bool shared_user_matches_assignee(const UserRecord &user, const std::string &assignee)
{
    return user_matches_assignee(user, assignee);
}

std::vector<std::string> shared_newly_assigned_users_from_material_delta(const std::vector<MaterialItem> &old_material,
                                                                          const std::vector<MaterialItem> &new_material)
{
    return newly_assigned_users_from_material_delta(old_material, new_material);
}


