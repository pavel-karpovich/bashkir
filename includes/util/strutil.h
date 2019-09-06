#pragma once
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>

namespace bashkir::util
{

inline std::string &ltrim(std::string &str)
{
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) {
                  return !std::isspace(ch);
              }));
    return str;
}

inline std::string &rtrim(std::string &str)
{
    str.erase(std::find_if(str.rbegin(), str.rend(), [](int ch) {
                  return !std::isspace(ch);
              })
                  .base(),
              str.end());
    return str;
}

inline void trim(std::string &str)
{
    ltrim(str);
    rtrim(str);
}

inline bool startswith(const std::string &str, const std::string &&prefix)
{
    return str.rfind(prefix) == 0;
}

inline bool startswith(const std::string &str, const std::string &prefix)
{
    return str.rfind(prefix) == 0;
}

inline bool is_empty(const char *str)
{
    return strcmp(str, "") == 0;
}

inline char* substr(const char *str, std::size_t start_pos, std::size_t length)
{
    char *substr = new char[length + 1];
    memcpy(substr, &str[start_pos], length);
    substr[length] = '\0';
    return substr;
}

inline std::vector<std::string> split(const std::string &base, const char delim)
{
    std::stringstream sstream(base);
    std::vector<std::string> parts;
    std::string next_part;
    while (std::getline(sstream, next_part, delim))
    {
        parts.push_back(next_part);
    }
    return parts;
}

} // namespace bashkir::util
