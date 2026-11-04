#include "test_harness.hpp"
#include "infra/midata_client.hpp"
#include "infra/weather_client.hpp"

using nlohmann::json;

TEST(midata_counts_participants_with_linked_roles_not_leaders)
{
    auto payload = json::parse(R"({
        "people": [
            {"id":"1","links":{"roles":["10","11"]}},
            {"id":"2","links":{"roles":["11"]}},
            {"id":"3","links":{"roles":["12"]}},
            {"id":"4","links":{"roles":[]}}
        ],
        "linked":{"roles":[
            {"id":"10","role_class":"Group::Pfadi::Pfadi","links":{"group":"42"}},
            {"id":"11","role_class":"Group::Pfadi::Mitleitung","links":{"group":"42"}},
            {"id":"12","role_class":"Group::Pfadi::Pfadi","links":{"group":"99"}}
        ]}
    })");
    std::string error = "previous-error";
    auto count = count_midata_children(payload, "42", error);
    ASSERT_TRUE(count.has_value());
    ASSERT_EQ(*count, 2);
    ASSERT_TRUE(error.empty());
}

TEST(midata_counts_each_pbs_participant_once)
{
    const std::string classes[] = {
        "Group::Biber::Biber", "Group::Woelfe::Wolf", "Group::Woelfe::Leitwolf",
        "Group::Pfadi::Pfadi", "Group::Pfadi::Leitpfadi", "Group::Pio::Pio",
        "Group::Rover::Rover", "Group::Pfadi::Teilnehmer"};
    for (const auto &role_class : classes)
    {
        json role = {{"role_class", role_class}, {"links", {{"group", 42}}}};
        json payload = {{"people", json::array({{{"roles", json::array({role, role})}}})}};
        std::string error;
        auto count = count_midata_children(payload, "42", error);
        ASSERT_TRUE(count.has_value());
        ASSERT_EQ(*count, 1);
    }
}

TEST(midata_supports_json_api_relationships)
{
    auto payload = json::parse(R"({
        "data":[
            {"type":"people","id":"1","relationships":{"roles":{"data":[{"type":"roles","id":"7"}]}}},
            {"type":"people","id":"2","relationships":{"roles":{"data":[{"type":"roles","id":"8"}]}}}
        ],
        "included":[
            {"type":"roles","id":"7","attributes":{"type":"Group::Woelfe::Wolf","group_id":42},
             "relationships":{"group":{"data":{"type":"groups","id":"42"}}}},
            {"type":"roles","id":"8","attributes":{"type":"Group::Woelfe::Wolf","group_id":99},
             "relationships":{"group":{"data":{"type":"groups","id":"99"}}}}
        ]
    })");
    std::string error;
    auto count = count_midata_children(payload, "42", error);
    ASSERT_TRUE(count.has_value());
    ASSERT_EQ(*count, 1);
}

TEST(midata_missing_role_details_is_not_a_zero_count)
{
    std::string error;
    auto count = count_midata_children(
        json::parse(R"({"people":[{"links":{"roles":["10"]}}]})"), "42", error);
    ASSERT_FALSE(count.has_value());
    ASSERT_STR_EQ(error, "role-details-missing");
    count = count_midata_children(json::parse(R"({"people":[{"id":"1"}]})"), "42", error);
    ASSERT_FALSE(count.has_value());
    ASSERT_STR_EQ(error, "person-roles-missing");
}

TEST(midata_empty_group_is_a_valid_zero)
{
    std::string error;
    auto count = count_midata_children(json::parse(R"({"people":[]})"), "42", error);
    ASSERT_TRUE(count.has_value());
    ASSERT_EQ(*count, 0);
    ASSERT_FALSE(count_midata_children(json::object(), "42", error).has_value());
    ASSERT_STR_EQ(error, "people-array-missing");
}

TEST(weather_selects_newest_run_and_skips_empty_next_day)
{
    auto payload = json::parse(R"({"features":[
        {"id":"20261105-ch","assets":{}},
        {"id":"20261104-ch","assets":{
            "vnut12.lssw.202611040000.tre200h0.csv":{"href":"https://example.test/old.csv"},
            "vnut12.lssw.202611042200.tre200h0.csv":{"href":"https://example.test/latest.csv"},
            "vnut12.lssw.202611042200.rre150h0.csv":{"href":"https://example.test/rain.csv"}
        }},
        {"id":"20261103-ch","assets":{
            "vnut12.lssw.202611032300.tre200h0.csv":{"href":"https://example.test/yesterday.csv"}
        }}
    ]})");
    std::string error;
    auto url = select_latest_weather_asset(payload, "tre200h0.csv", error);
    ASSERT_TRUE(url.has_value());
    ASSERT_STR_EQ(*url, "https://example.test/latest.csv");
    url = select_latest_weather_asset(payload, "rre150h0.csv", error);
    ASSERT_TRUE(url.has_value());
    ASSERT_STR_EQ(*url, "https://example.test/rain.csv");
    ASSERT_TRUE(error.empty());
}

TEST(weather_missing_asset_reports_an_error)
{
    std::string error;
    ASSERT_FALSE(select_latest_weather_asset(json::object(), "tre200h0.csv", error).has_value());
    ASSERT_STR_EQ(error, "meteo-items-invalid");
    ASSERT_FALSE(select_latest_weather_asset(
        json::parse(R"({"features":[{"assets":{}}]})"), "tre200h0.csv", error).has_value());
    ASSERT_STR_EQ(error, "meteo-asset-missing-tre200h0.csv");
}

TEST(weather_parses_sorted_hourly_series_for_selected_point)
{
    std::string error;
    auto series = parse_weather_series(
        "point_id;point_type_id;Date;tre200h0\r\n"
        "42;2;202611061400;8.5\r\n"
        "420;2;202611061300;99\r\n"
        "42;2;202611061300;7.2\r\n", 42, error);
    ASSERT_TRUE(series.has_value());
    ASSERT_EQ(series->size(), 2u);
    ASSERT_EQ(series->at(1).first - series->at(0).first, 3600);
    ASSERT_EQ(series->at(0).second, 7.2);
    ASSERT_EQ(series->at(1).second, 8.5);
}

TEST(weather_rejects_missing_or_invalid_point_data)
{
    std::string error;
    ASSERT_FALSE(parse_weather_series("header\n1;2;202611061400;8.5\n", 42, error).has_value());
    ASSERT_STR_EQ(error, "meteo-temp-point-missing");
    ASSERT_FALSE(parse_weather_series("header\n42;2;not-a-date!!;8.5\n", 42, error).has_value());
    ASSERT_STR_EQ(error, "meteo-csv-row-invalid");
    ASSERT_FALSE(parse_weather_series("header\n42;2;202611061400;NaN\n", 42, error).has_value());
    ASSERT_STR_EQ(error, "meteo-csv-row-invalid");
    ASSERT_FALSE(parse_weather_series("header\n42;2;202613061400;8.5\n", 42, error).has_value());
    ASSERT_STR_EQ(error, "meteo-csv-row-invalid");
    ASSERT_FALSE(parse_weather_series("header\n42;2;202611061400;8.5bad\n", 42, error).has_value());
    ASSERT_STR_EQ(error, "meteo-csv-row-invalid");
}

TEST(weather_distant_activity_uses_seasonal_average_without_network)
{
    Activity activity;
    activity.date = "2099-07-01";
    activity.start_time = "14:00";
    activity.end_time = "17:00";
    std::string error;
    auto weather = fetch_expected_weather_for_activity(activity, "6300 Zug", error);
    ASSERT_TRUE(weather.has_value());
    ASSERT_STR_EQ(weather->mode, "seasonal-average");
    ASSERT_TRUE(error.empty());
}
