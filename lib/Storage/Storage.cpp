#include "Storage.h"

#ifdef ARDUINO

#include <SD.h>
#include <SPI.h>
#include <Board.h>

namespace
{
    // マウントを何回まで試すか
    constexpr int kRetryCount = 3;
    constexpr int kRetryIntervalMs = 200;

    // 4MHz。速くすると相性の出るカードがあるので、まず確実に読める速さにする。
    constexpr uint32_t kSpiFrequency = 4000000;

    bool mounted = false;
    SPIClass sdSpi(HSPI);
}

namespace Storage
{
    bool begin()
    {
        mounted = false;

        for (int i = 0; i < kRetryCount; i++)
        {
            if (i > 0)
            {
                end();
                delay(kRetryIntervalMs);
                log_i("retry to mount SD card (%d)", i);
            }

            sdSpi.begin(Board::kSdSclk, Board::kSdMiso, Board::kSdMosi, Board::kSdCs);
            if (SD.begin(Board::kSdCs, sdSpi, kSpiFrequency))
            {
                mounted = true;
                return true;
            }
        }
        return false;
    }

    bool isAvailable()
    {
        return mounted;
    }

    void end()
    {
        mounted = false;
        SD.end();
        sdSpi.end();
    }

    fs::FS &fs()
    {
        return SD;
    }
}

#endif
