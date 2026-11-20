#pragma once
// The display language shared by the launcher and the HUD (user, 2026-11-17): Japanese or English.
// Both programs keep it in %LOCALAPPDATA%\MyClient\language.txt ("ja" or "en"), so a switch in one shows in the other.
// The launcher picks it on its first start from where it was downloaded (the site page's language, see LangFromZone),
// then from Windows' display language. Text that exists in both languages is written as Lang::T(ja, en).
#include <optional>
#include <string>

namespace Lang {

enum class Id { Ja, En };

inline Id& Current()
{
    static Id current = Id::Ja;
    return current;
}
inline bool English() { return Current() == Id::En; }

inline const wchar_t* T(const wchar_t* ja, const wchar_t* en) { return English() ? en : ja; }
inline const char* T(const char* ja, const char* en) { return English() ? en : ja; }

inline const char* Key(Id id) { return id == Id::En ? "en" : "ja"; }

// "ja" / "en" (the file may end with a newline or start with a BOM).
inline std::optional<Id> Parse(std::string text)
{
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    if (text == "ja") return Id::Ja;
    if (text == "en") return Id::En;
    return std::nullopt;
}

// The Zone.Identifier stream Windows keeps on a downloaded file holds the download link (HostUrl=). The Worker adds
// lang=en to the launcher's link when the site's English page started the download; an /en/ page as the referrer also counts.
inline std::optional<Id> FromZone(const std::string& zone)
{
    std::optional<Id> found;
    size_t start = 0;
    while (start < zone.size())
    {
        size_t end = zone.find('\n', start);
        if (end == std::string::npos) end = zone.size();
        std::string line = zone.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        const bool host = line.rfind("HostUrl=", 0) == 0;
        const bool referrer = line.rfind("ReferrerUrl=", 0) == 0;
        if (!host && !referrer) continue;
        const std::string url = line.substr(line.find('=') + 1);
        if (host)
        {
            const size_t query = url.find('?');
            if (query == std::string::npos) continue;
            const std::string params = "&" + url.substr(query + 1) + "&";
            if (params.find("&lang=en&") != std::string::npos) return Id::En;
            if (params.find("&lang=ja&") != std::string::npos) return Id::Ja;
        }
        else if (!found)
        {
            const size_t scheme = url.find("://");
            const size_t path = scheme == std::string::npos ? std::string::npos : url.find('/', scheme + 3);
            if (path != std::string::npos) found = url.compare(path, 4, "/en/") == 0 || url.substr(path) == "/en" ? Id::En : Id::Ja;
        }
    }
    return found;
}

// Windows' display language (a LANGID): Japanese stays Japanese, anything else is English.
inline Id FromLangId(unsigned short langId) { return (langId & 0x3FF) == 0x11 ? Id::Ja : Id::En; }

}  // namespace Lang

