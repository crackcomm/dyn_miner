// WHISKERZ CODE
#pragma once

#include "dyn_stratum.h"
#include "version.h"

#include <string>

constexpr auto BLACK = 0;
constexpr auto BLUE = 1;
constexpr auto GREEN = 2;
constexpr auto CYAN = 3;
constexpr auto RED = 4;
constexpr auto MAGENTA = 5;
constexpr auto BROWN = 6;
constexpr auto LIGHTGRAY = 7;
constexpr auto DARKGRAY = 8;
constexpr auto LIGHTBLUE = 9;
constexpr auto LIGHTGREEN = 10;
constexpr auto LIGHTCYAN = 11;
constexpr auto LIGHTRED = 12;
constexpr auto LIGHTMAGENTA = 13;
constexpr auto YELLOW = 14;
constexpr auto WHITE = 15;

// For displaying Hashrate in easy-to-read format.
constexpr float tb = 1099511627776;
constexpr float gb = 1073741824;
constexpr float mb = 1048576;
constexpr float kb = 1024;

static std::string seconds_to_uptime(int n);

#ifdef _WIN32
#define SET_COLOR(color) SetConsoleTextAttribute(hConsole, color);
#else
#define SET_COLOR(color)
#endif

static bool output_stats(time_t now, time_t start, stats_t& stats) {
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    uint32_t nonce = stats.nonce_count.load(std::memory_order_relaxed);

    struct tm* timeinfo;
    char timestamp[80];
    timeinfo = localtime(&now);
    strftime(timestamp, 80, "%F %T", timeinfo);
    char display[256];
    float hashrate = (float)nonce / (float)(now - start);
    if (hashrate >= tb)
        sprintf(display, "%.2f TH/s", (float)hashrate / tb);
    else if (hashrate >= gb && hashrate < tb)
        sprintf(display, "%.2f GH/s", (float)hashrate / gb);
    else if (hashrate >= mb && hashrate < gb)
        sprintf(display, "%.2f MH/s", (float)hashrate / mb);
    else if (hashrate >= kb && hashrate < mb)
        sprintf(display, "%.2f KH/s", (float)hashrate / kb);
    else if (hashrate < kb)
        sprintf(display, "%.2f H/s ", hashrate);
    else
        sprintf(display, "%.2f H/s", hashrate);

    std::string uptime = seconds_to_uptime(difftime(now, start));

    SET_COLOR(LIGHTBLUE);
    printf("%s: ", timestamp);
    SET_COLOR(GREEN);
    printf("%s", display);
    // SET_COLOR(LIGHTGRAY);
    // printf(" | ");
    // SET_COLOR(LIGHTGREEN);
    // printf("%d", dynProgram->height);
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(BLUE);
    printf("Uptime: %6s", uptime.c_str());
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(LIGHTGREEN);
    printf("S: %4d", stats.share_count.load(std::memory_order_relaxed));
    SET_COLOR(GREEN);
    printf("/%-4d", stats.accepted_share_count.load(std::memory_order_relaxed));
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(RED);
    printf("R: %4d", stats.rejected_share_count.load(std::memory_order_relaxed));
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(LIGHTGREEN);
    printf(" D:%-4d", stats.latest_diff.load(std::memory_order_relaxed));
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(LIGHTGREEN);
    printf("N:%-8d", nonce);
    SET_COLOR(LIGHTGRAY);
    printf(" | ");
    SET_COLOR(LIGHTMAGENTA);
    printf("DynMiner %s\n", minerVersion);
    SET_COLOR(LIGHTGRAY);

    return (true);
}

static std::string seconds_to_uptime(int n) {
    int days = n / (24 * 3600);

    n = n % (24 * 3600);
    int hours = n / 3600;

    n %= 3600;
    int minutes = n / 60;

    n %= 60;
    int seconds = n;

    std::string uptimeString;
    if (days > 0) {
        uptimeString = std::to_string(days) + "d" + std::to_string(hours) + "h" + std::to_string(minutes) + "m"
                       + std::to_string(seconds) + "s";
    } else if (hours > 0) {
        uptimeString = std::to_string(hours) + "h" + std::to_string(minutes) + "m" + std::to_string(seconds) + "s";
    } else if (minutes > 0) {
        uptimeString = std::to_string(minutes) + "m" + std::to_string(seconds) + "s";
    } else {
        uptimeString = std::to_string(seconds) + "s";
    }

    return (uptimeString);
}
