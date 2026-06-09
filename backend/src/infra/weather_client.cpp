#include "infra/weather_client.hpp"
#include "utils/strings.hpp"   // for to_lower_ascii, trim_ascii
#include "json.hpp"
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <chrono>
#include <sstream>
#include <limits>
#include <algorithm>
#include <regex>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <curl/curl.h>

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

namespace
{

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

    struct MeteoPointMeta
    {
        int point_id = 0;
        int point_type_id = 0;
        std::string postal_code;
        std::string point_name;
    };

    std::mutex meteo_cache_mutex;
    std::vector<MeteoPointMeta> meteo_point_cache;
    std::chrono::steady_clock::time_point meteo_point_cache_expires;
    // Keep Meteo upstream calls short to avoid blocking the single API event loop.
    constexpr long kMeteoHttpTimeoutMs = 500;

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
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
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

} // namespace

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

bool should_use_frozen_values(const Activity &activity)
{
    const std::string today = utc_today_ymd();
    if (today.empty())
        return false;
    return !activity.date.empty() && activity.date < today;
}
