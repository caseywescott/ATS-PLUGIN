#pragma once

// Internal-only helpers for hand-rolled JSON serialization.
// Include only from .cpp translation units — not part of the public API.

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace ats
{
namespace json_internal
{

inline std::string number(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

inline void appendQuoted(std::ostringstream& out, const std::string& value)
{
    out << '"';
    for (const char c : value)
    {
        switch (c)
        {
            case '\\': out << "\\\\"; break;
            case '"':  out << "\\\""; break;
            case '\b': out << "\\b";  break;
            case '\f': out << "\\f";  break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c)) << std::dec
                        << std::setfill(' ');
                }
                else
                {
                    out << c;
                }
                break;
        }
    }
    out << '"';
}

} // namespace json_internal
} // namespace ats
