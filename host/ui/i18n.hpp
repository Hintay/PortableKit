#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

// The interface's language. Its text is written in English in the code and
// translated as it is drawn: the widgets look every label, value,
// description, note, title and hint up in the chosen language's catalogue,
// so a screen passes its English text as before. Text the catalogue lacks
// stays English, and so does anything that is not the interface's own words
// (file names, save titles, addresses), because no catalogue has it.
//
// A label's "##" part is an id and is kept as it is; only the part before
// it is translated, so ids do not change with the language.
//
// Text put together from parts goes through format(): the whole sentence is
// the catalogue's key, with {} where the parts go, so a translation can
// order them as its language needs.
//
// The language is the setting ui.language (<prefix>_UI_LANGUAGE): "auto",
// the default, takes the release's language (GameProfile::dialog_language)
// when there is a catalogue for it, else the system's; "en" keeps English.
// <prefix>_TRACE_I18N=1 logs each text the chosen catalogue lacks, once.
namespace portablekit::ui::i18n {

struct LanguageInfo {
    const char *code;  // what settings.ini stores: "en", "zh-Hans"
    const char *name;  // in its own language, as the menu offers it
};

// The languages there are catalogues for, English first.
[[nodiscard]] const LanguageInfo *languages(std::size_t &count);

// Chooses the language from a setting's value: "auto", "" or a code.
void apply(const std::string &setting);
// The code of the language in use.
[[nodiscard]] const char *current_code();
// What "auto" chooses now.
[[nodiscard]] const char *automatic_code();

// `english` in the current language. The pointer stays valid until the
// program ends.
[[nodiscard]] const char *tr(const char *english);
[[nodiscard]] std::string tr(const std::string &english);
// Translates `english`, then puts the parts in for its {} in order.
[[nodiscard]] std::string format(const char *english, std::initializer_list<std::string_view> parts);

// One entry of a catalogue.
struct Entry {
    const char *english;
    const char *translated;
};

} // namespace portablekit::ui::i18n
