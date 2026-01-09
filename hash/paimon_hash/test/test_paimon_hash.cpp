#include <gtest/gtest.h>
#include <paimon_hash.h>

#include <bit>
#include <numeric>
#include <type_traits>

int32_t single_hash_null() {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.set_null_at(0);
    return builder.hash_code();
}

int32_t single_hash_bool(bool value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_boolean(0, value);
    return builder.hash_code();
}

int32_t single_hash_tinyint(int8_t value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_byte(0, value);
    return builder.hash_code();
}

int32_t single_hash_smallint(int16_t value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_short(0, value);
    return builder.hash_code();
}

int32_t single_hash_int(int32_t value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_int(0, value);
    return builder.hash_code();
}

int32_t single_hash_bigint(int64_t value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_long(0, value);
    return builder.hash_code();
}

int32_t single_hash_float(float value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_float(0, value);
    return builder.hash_code();
}

int32_t single_hash_double(double value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_double(0, value);
    return builder.hash_code();
}

int32_t single_hash_string(std::string value) {
    paimon_hash::BinaryRowBuilder builder(1);
    builder.write_string(0, value);
    return builder.hash_code();
}

template <typename>
inline constexpr bool always_false_v = false;

template <typename... Ts>
int32_t multi_hash(const std::optional<Ts>&... values) {
    paimon_hash::BinaryRowBuilder builder(sizeof...(Ts));
    size_t index = 0;

    auto value_writer = [&builder](size_t index, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            builder.write_boolean(index, value);
        } else if constexpr (std::is_same_v<T, int8_t>) {
            builder.write_byte(index, value);
        } else if constexpr (std::is_same_v<T, int16_t>) {
            builder.write_short(index, value);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            builder.write_int(index, value);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            builder.write_long(index, value);
        } else if constexpr (std::is_same_v<T, float>) {
            builder.write_float(index, value);
        } else if constexpr (std::is_same_v<T, double>) {
            builder.write_double(index, value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            builder.write_string(index, value);
        } else {
            static_assert(always_false_v<T>, "Unsupported type");
        }
    };

    ((values.has_value() ? value_writer(index, values.value()) : builder.set_null_at(index), ++index), ...);
    return builder.hash_code();
}

TEST(PaimonHashTest, testNull) {
    ASSERT_EQ(-1748325344, single_hash_null());
}

TEST(PaimonHashTest, testBool) {
    ASSERT_EQ(1465514398, single_hash_bool(true));
    ASSERT_EQ(-300363099, single_hash_bool(false));
}

TEST(PaimonHashTest, testTinyint) {
    ASSERT_EQ(1465514398, single_hash_tinyint(1));
    ASSERT_EQ(-300363099, single_hash_tinyint(0));
    ASSERT_EQ(2004758659, single_hash_tinyint(-1));
    ASSERT_EQ(1260004151, single_hash_tinyint(std::numeric_limits<int8_t>::max()));
    ASSERT_EQ(-1226381822, single_hash_tinyint(std::numeric_limits<int8_t>::min()));
    ASSERT_EQ(1085547692, single_hash_tinyint(std::numeric_limits<int8_t>::max() / 2));
    ASSERT_EQ(133406334, single_hash_tinyint(std::numeric_limits<int8_t>::min() / 2));
}

TEST(PaimonHashTest, testSmallint) {
    ASSERT_EQ(1465514398, single_hash_smallint(1));
    ASSERT_EQ(-300363099, single_hash_smallint(0));
    ASSERT_EQ(2143727727, single_hash_smallint(-1));
    ASSERT_EQ(589084209, single_hash_smallint(std::numeric_limits<int16_t>::max()));
    ASSERT_EQ(-141722409, single_hash_smallint(std::numeric_limits<int16_t>::min()));
    ASSERT_EQ(-2099834969, single_hash_smallint(std::numeric_limits<int16_t>::max() / 2));
    ASSERT_EQ(1710620104, single_hash_smallint(std::numeric_limits<int16_t>::min() / 2));
}

TEST(PaimonHashTest, testInt) {
    ASSERT_EQ(1465514398, single_hash_int(1));
    ASSERT_EQ(-300363099, single_hash_int(0));
    ASSERT_EQ(1133687267, single_hash_int(-1));
    ASSERT_EQ(-1125657321, single_hash_int(std::numeric_limits<int32_t>::max()));
    ASSERT_EQ(916225219, single_hash_int(std::numeric_limits<int32_t>::min()));
    ASSERT_EQ(-85672531, single_hash_int(std::numeric_limits<int32_t>::max() / 2));
    ASSERT_EQ(446748170, single_hash_int(std::numeric_limits<int32_t>::min() / 2));
}

TEST(PaimonHashTest, testBigint) {
    ASSERT_EQ(1465514398, single_hash_bigint(1));
    ASSERT_EQ(-300363099, single_hash_bigint(0));
    ASSERT_EQ(-821098432, single_hash_bigint(-1));
    ASSERT_EQ(-1566569095, single_hash_bigint(std::numeric_limits<int64_t>::max()));
    ASSERT_EQ(302122119, single_hash_bigint(std::numeric_limits<int64_t>::min()));
    ASSERT_EQ(-1869071721, single_hash_bigint(std::numeric_limits<int64_t>::max() / 2));
    ASSERT_EQ(-1758468991, single_hash_bigint(std::numeric_limits<int64_t>::min() / 2));
}

TEST(PaimonHashTest, testFloat) {
    ASSERT_EQ(1657394889, single_hash_float(1));
    ASSERT_EQ(-300363099, single_hash_float(0));
    ASSERT_EQ(1475197116, single_hash_float(-1));
    ASSERT_EQ(-1125657321, single_hash_float(std::bit_cast<float>(std::numeric_limits<int32_t>::max())));
    ASSERT_EQ(916225219, single_hash_float(std::bit_cast<float>(std::numeric_limits<int32_t>::min())));
    ASSERT_EQ(-85672531, single_hash_float(std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2)));
    ASSERT_EQ(446748170, single_hash_float(std::bit_cast<float>(std::numeric_limits<int32_t>::min() / 2)));
}

TEST(PaimonHashTest, testDouble) {
    ASSERT_EQ(-764008013, single_hash_double(1));
    ASSERT_EQ(-300363099, single_hash_double(0));
    ASSERT_EQ(-2032504484, single_hash_double(-1));
    ASSERT_EQ(-1566569095, single_hash_double(std::bit_cast<double>(std::numeric_limits<int64_t>::max())));
    ASSERT_EQ(302122119, single_hash_double(std::bit_cast<double>(std::numeric_limits<int64_t>::min())));
    ASSERT_EQ(-1869071721, single_hash_double(std::bit_cast<double>(std::numeric_limits<int64_t>::max() / 2)));
    ASSERT_EQ(-1758468991, single_hash_double(std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2)));
}

TEST(PaimonHashTest, testString) {
    ASSERT_EQ(188698932, single_hash_string("hello world."));
    ASSERT_EQ(-2057560262, single_hash_string("hello\nworld."));
    ASSERT_EQ(-1764217487, single_hash_string("你好，世界！"));
    ASSERT_EQ(1946177714, single_hash_string("你好，\n世界！"));
}

TEST(PaimonHashTest, testMultiColumns) {
    ASSERT_EQ(-1937236088,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      true, 1, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                      std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(-1875445593,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      std::nullopt, 1, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                      std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(-688447248,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      true, std::nullopt, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                      std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(373659277,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      true, 1, std::nullopt, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                      std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(-974857177, (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                                  true, 1, -1, std::nullopt, std::numeric_limits<int64_t>::min(),
                                  std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                                  std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(-194924779, (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                                  true, 1, -1, std::numeric_limits<int32_t>::max(), std::nullopt,
                                  std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                                  std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(2110069866,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      true, 1, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::nullopt, std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), "hello world")));
    ASSERT_EQ(-930418670,
              (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                      true, 1, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                      std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2), std::nullopt, "hello world")));
    ASSERT_EQ(50887171, (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                                true, 1, -1, std::numeric_limits<int32_t>::max(), std::numeric_limits<int64_t>::min(),
                                std::bit_cast<float>(std::numeric_limits<int32_t>::max() / 2),
                                std::bit_cast<double>(std::numeric_limits<int64_t>::min() / 2), std::nullopt)));
    ASSERT_EQ(1531819297, (multi_hash<bool, int8_t, int16_t, int32_t, int64_t, float, double, std::string>(
                                  std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                                  std::nullopt, std::nullopt)));
}
