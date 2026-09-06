#include "Credentials.h"

namespace
{
    const char kHexDigits[] = "0123456789ABCDEF";

    void appendHex(String &out, uint8_t value)
    {
        out += kHexDigits[(value >> 4) & 0x0F];
        out += kHexDigits[value & 0x0F];
    }

    /**
     * WIFI: の書式で意味を持つ文字を退避する。
     * これをしないと、記号を含む SSID やパスワードが途中で切れて読まれる。
     */
    void appendEscaped(String &out, const String &value)
    {
        for (size_t i = 0; i < value.length(); i++)
        {
            char c = value[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
            {
                out += '\\';
            }
            out += c;
        }
    }
}

namespace Credentials
{
    String ssidFor(const uint8_t *mac)
    {
        String ssid = "CardCase-";
        if (mac == nullptr)
        {
            return ssid + "000000";
        }

        // 下位 3 バイトあれば同じ場所に複数台あっても区別できる
        for (int i = 3; i < 6; i++)
        {
            appendHex(ssid, mac[i]);
        }
        return ssid;
    }

    String passwordFor(const uint8_t *mac)
    {
        if (mac == nullptr)
        {
            return String("cardcase");
        }

        // MAC 全体を混ぜて 8 桁の 16 進にする
        uint32_t seed = 0;
        for (int i = 0; i < 6; i++)
        {
            seed = seed * 33u + mac[i];
        }

        String password;
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            password += kHexDigits[(seed >> shift) & 0x0F];
        }
        return password;
    }

    String wifiQrPayload(const String &ssid, const String &password)
    {
        String payload = "WIFI:T:WPA;S:";
        appendEscaped(payload, ssid);
        payload += ";P:";
        appendEscaped(payload, password);
        payload += ";;";
        return payload;
    }
}
