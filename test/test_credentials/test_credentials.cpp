#include <unity.h>
#include <Credentials/Credentials.h>

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const uint8_t kMac[6] = {0xA0, 0x85, 0xE3, 0xF2, 0x8B, 0xC4};
    const uint8_t kOtherMac[6] = {0x44, 0x1B, 0xF6, 0xC1, 0x7F, 0x08};
}

void test_ssid_is_derived_from_mac()
{
    // 下位 3 バイトを使うので、同じ場所に複数台あっても区別できる
    TEST_ASSERT_EQUAL_STRING("CardCase-F28BC4", Credentials::ssidFor(kMac).c_str());
    TEST_ASSERT_EQUAL_STRING("CardCase-C17F08", Credentials::ssidFor(kOtherMac).c_str());
}

void test_credentials_are_stable_for_the_same_device()
{
    // 毎回変わるとスマホが覚えた設定で繋がらなくなる
    TEST_ASSERT_EQUAL_STRING(Credentials::ssidFor(kMac).c_str(), Credentials::ssidFor(kMac).c_str());
    TEST_ASSERT_EQUAL_STRING(Credentials::passwordFor(kMac).c_str(), Credentials::passwordFor(kMac).c_str());
}

void test_password_satisfies_wpa2_length()
{
    // WPA2 は 8 文字以上でないと接続できない
    TEST_ASSERT_EQUAL_INT(Credentials::kPasswordLength, (int)Credentials::passwordFor(kMac).length());
    TEST_ASSERT_EQUAL_INT(Credentials::kPasswordLength, (int)Credentials::passwordFor(kOtherMac).length());
}

void test_password_differs_between_devices()
{
    TEST_ASSERT_TRUE(Credentials::passwordFor(kMac) != Credentials::passwordFor(kOtherMac));
}

void test_handles_missing_mac()
{
    // MAC が取れなくても接続できる値を返す
    TEST_ASSERT_EQUAL_STRING("CardCase-000000", Credentials::ssidFor(nullptr).c_str());
    TEST_ASSERT_TRUE((int)Credentials::passwordFor(nullptr).length() >= Credentials::kPasswordLength);
}

void test_qr_payload_format()
{
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:CardCase-F28BC4;P:1A2B3C4D;;",
        Credentials::wifiQrPayload("CardCase-F28BC4", "1A2B3C4D").c_str());
}

void test_qr_payload_escapes_special_characters()
{
    // 記号を含む値でも途中で切れずに読まれること
    // SSID "a;b" -> a\;b / パスワード c:d\e,f"g -> c\:d\\e\,f\"g
    TEST_ASSERT_EQUAL_STRING(
        "WIFI:T:WPA;S:a\\;b;P:c\\:d\\\\e\\,f\\\"g;;",
        Credentials::wifiQrPayload("a;b", "c:d\\e,f\"g").c_str());
}

int runUnityTests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ssid_is_derived_from_mac);
    RUN_TEST(test_credentials_are_stable_for_the_same_device);
    RUN_TEST(test_password_satisfies_wpa2_length);
    RUN_TEST(test_password_differs_between_devices);
    RUN_TEST(test_handles_missing_mac);
    RUN_TEST(test_qr_payload_format);
    RUN_TEST(test_qr_payload_escapes_special_characters);
    return UNITY_END();
}

int main(void)
{
    return runUnityTests();
}
