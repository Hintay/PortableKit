// Simplified Chinese: the interface's English text and its translation, one
// file of entries per part of the interface.

#include "ui/i18n.hpp"

#include <iterator>

namespace portablekit::ui::i18n {

extern const Entry kSimplifiedChinese[] = {
#include "ui/i18n/zh_hans_common.inc"
#include "ui/i18n/zh_hans_menu.inc"
#include "ui/i18n/zh_hans_screens.inc"
#include "ui/i18n/zh_hans_setup.inc"
};
extern const std::size_t kSimplifiedChineseCount = std::size(kSimplifiedChinese);

} // namespace portablekit::ui::i18n
