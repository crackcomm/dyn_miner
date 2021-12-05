#pragma once

#include <string>
#include <cstring>
#include <vector>

inline constexpr unsigned char decodeHex(char in) {
    in = toupper(in);
    if ((in >= '0') && (in <= '9'))
        return in - '0';
    else if ((in >= 'A') && (in <= 'F'))
        return in - 'A' + 10;
    else
        return 0; // todo raise error
}

inline void parseHex(std::string input, unsigned char* output) {
    for (int i = 0; i < input.length(); i += 2) {
        unsigned char value = decodeHex(input[i]) * 16 + decodeHex(input[i + 1]);
        output[i / 2] = value;
    }
}

//inline void hex2bin(const 

const std::vector<char> hexDigit = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

inline std::string makeHex(unsigned char* in, int len) {
    std::string result;
    for (int i = 0; i < len; i++) {
        result += hexDigit[in[i] / 16];
        result += hexDigit[in[i] % 16];
    }
    return result;
}

inline std::string makeHex(char* in, int len) {
    return makeHex((unsigned char*)in, len);
}

inline void bin2hex(char* s, const unsigned char* p, size_t len) {
    int i;
    for (i = 0; i < len; i++)
#ifdef __linux__
        sprintf(s + (i * 2), "%02x", (unsigned int)p[i]);
#else
        sprintf_s(s + (i * 2), 3, "%02x", (unsigned int)p[i]);
#endif
}

inline bool hex2bin(unsigned char* p, const char* hexstr, size_t len) {
    if (hexstr == NULL) return false;

    size_t hexstr_len = strlen(hexstr);
    if ((hexstr_len % 2) != 0) {
        return false;
    }

    size_t bin_len = hexstr_len / 2;
    if (bin_len > len) {
        return false;
    }

    memset(p, 0, len);

    size_t i = 0;
    while (i < hexstr_len) {
        char c = hexstr[i];
        unsigned char nibble;
        if (c >= '0' && c <= '9') {
            nibble = (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nibble = (10 + (c - 'A'));
        } else if (c >= 'a' && c <= 'f') {
            nibble = (10 + (c - 'a'));
        } else {
            return false;
        }
        p[(i / 2)] |= (nibble << ((1 - (i % 2)) * 4));
        i++;
    }

    return true;
}

