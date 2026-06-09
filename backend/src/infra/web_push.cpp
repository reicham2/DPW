#include "infra/web_push.hpp"
#include "infra/app_config.hpp"
#include "db/database.hpp"
#include "utils/strings.hpp"    // for trim_ascii
#include "json.hpp"
#include <string>
#include <optional>
#include <ctime>
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/core_names.h>
#include <openssl/kdf.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/rand.h>

namespace
{

    static size_t midata_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *out = static_cast<std::string *>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
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

} // namespace

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
