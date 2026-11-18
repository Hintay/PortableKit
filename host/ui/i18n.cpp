#include "ui/i18n.hpp"

#include "../profile.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace portablekit::ui::i18n {

// The catalogues, one file each under ui/i18n/.
extern const Entry kSimplifiedChinese[];
extern const std::size_t kSimplifiedChineseCount;

namespace {

struct Catalogue {
    LanguageInfo info;
    const Entry *entries;
    const std::size_t *count;
    // The PSP's language number for it, to follow a release in it.
    std::uint32_t psp_language;
};

const Catalogue kCatalogues[] = {
    {{"en", "English"}, nullptr, nullptr, 1u},
    {{"zh-Hans", "简体中文"}, kSimplifiedChinese, &kSimplifiedChineseCount, 11u},
};
constexpr std::size_t kCount = sizeof(kCatalogues) / sizeof(kCatalogues[0]);

const LanguageInfo *language_list() {
    static const auto list = [] {
        static LanguageInfo out[kCount];
        for (std::size_t i = 0; i < kCount; ++i) out[i] = kCatalogues[i].info;
        return out;
    }();
    return list;
}

struct State {
    std::mutex mutex;
    const Catalogue *catalogue{&kCatalogues[0]};
    std::unordered_map<std::string_view, const char *> lookup;
    // Labels with an id, translated and joined again; node-based, so the
    // strings stay where they are.
    std::unordered_map<std::string, std::string> joined;
    // <prefix>_TRACE_I18N: each text the catalogue lacks, logged once.
    bool trace{portablekit::env("TRACE_I18N") != nullptr};
    std::unordered_set<std::string> missing;
};

State &state() {
    static State value;
    return value;
}

const Catalogue *find(std::string_view code) {
    for (const Catalogue &c : kCatalogues)
        if (code == c.info.code) return &c;
    return nullptr;
}

// The system's own language, if there is a catalogue for it.
const Catalogue *system_catalogue() {
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    const Catalogue *found = nullptr;
    for (int i = 0; locales != nullptr && i < count && found == nullptr; ++i) {
        const SDL_Locale *locale = locales[i];
        if (locale == nullptr || locale->language == nullptr) continue;
        const std::string language = locale->language;
        const std::string country = locale->country != nullptr ? locale->country : "";
        if (language == "en") break;  // English preferred over a later Chinese
        // Chinese as written in mainland China and Singapore.
        if (language == "zh" && (country.empty() || country == "CN" || country == "SG")) found = find("zh-Hans");
    }
    SDL_free(locales);
    return found;
}

const Catalogue *automatic() {
    const std::uint32_t release = text_language(game());
    for (const Catalogue &c : kCatalogues)
        if (c.entries != nullptr && c.psp_language == release) return &c;
    if (const Catalogue *system = system_catalogue()) return system;
    return &kCatalogues[0];
}

} // namespace

const LanguageInfo *languages(std::size_t &count) {
    count = kCount;
    return language_list();
}

void apply(const std::string &setting) {
    const Catalogue *chosen = setting.empty() || setting == "auto" ? automatic() : find(setting);
    if (chosen == nullptr) {
        std::cout << "[ui] no interface language \"" << setting << "\"; using English\n";
        chosen = &kCatalogues[0];
    }
    State &s = state();
    std::lock_guard lock(s.mutex);
    if (s.catalogue == chosen && (chosen->entries == nullptr || !s.lookup.empty())) return;
    s.catalogue = chosen;
    s.lookup.clear();
    s.joined.clear();
    if (chosen->entries != nullptr)
        for (std::size_t i = 0; i < *chosen->count; ++i) s.lookup.emplace(chosen->entries[i].english, chosen->entries[i].translated);
}

const char *current_code() {
    State &s = state();
    std::lock_guard lock(s.mutex);
    return s.catalogue->info.code;
}

const char *automatic_code() { return automatic()->info.code; }

const char *tr(const char *english) {
    if (english == nullptr || *english == '\0') return english;
    State &s = state();
    std::lock_guard lock(s.mutex);
    if (s.lookup.empty()) return english;
    const char *id = std::strstr(english, "##");
    const std::string_view shown = id != nullptr ? std::string_view(english, static_cast<std::size_t>(id - english))
                                                 : std::string_view(english);
    const auto it = s.lookup.find(shown);
    if (it == s.lookup.end()) {
        // Text with non-ASCII bytes is already translated (format() output
        // drawn by a widget) or is data; only English is reported.
        const bool english_text = std::all_of(shown.begin(), shown.end(), [](char c) { return (c & 0x80) == 0; });
        if (s.trace && english_text && !shown.empty() && s.missing.emplace(shown).second)
            std::cout << "[i18n] no " << s.catalogue->info.code << " text for \"" << shown << "\"\n";
        return english;
    }
    if (id == nullptr) return it->second;
    const auto [joined, added] = s.joined.try_emplace(english);
    if (added) joined->second = std::string(it->second) + id;
    return joined->second.c_str();
}

std::string tr(const std::string &english) { return tr(english.c_str()); }

std::string format(const char *english, std::initializer_list<std::string_view> parts) {
    const std::string pattern = tr(english);
    std::string out;
    out.reserve(pattern.size() + 32u);
    auto part = parts.begin();
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}' && part != parts.end()) {
            out += *part++;
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

} // namespace portablekit::ui::i18n
