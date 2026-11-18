#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

// The PSP's save-data list dialog, drawn over the running game for the list
// modes of sceUtilitySavedata (LISTSAVE, LISTLOAD, LISTDELETE).
//
// It looks and behaves as the PSP's own: the saves stand in a column on the
// left, the focused one larger, with the focused save's PIC1 behind and its
// title, date, size and detail on the right. Saving lists every name the
// game offers, the ones with no save as New Data; loading and deleting list
// the saves that exist. Writing over a save and deleting one ask first, with
// No focused. After the work it says how it went and closes on a button.
//
// Its text is in the language the request names (Japanese, English and
// both Chinese scripts; English for the rest). It fades in and out, the
// column scrolls, the hints at the bottom show the PSP's circle and cross,
// and moving, choosing and going back make short sounds. The sounds are
// synthesised here, and the text is in the interface's own font: the PSP's
// own sounds and font are the console's firmware, which no port may carry.
// <prefix>_NO_DIALOG_SOUNDS silences them.
//
// Gamepad: up and down move, confirm chooses, back cancels; left and right
// pick Yes or No. Confirm is the face button the menu's "Confirm button"
// setting names. Keyboard: the arrow keys, Enter, Space or Z to choose,
// Esc, Backspace or X to go back.
//
// The dialog does no file work itself: it calls `perform` with the chosen
// entry when the player has confirmed, on the thread that draws it (the
// game's, during its flip), and shows what that returned.
namespace portablekit::ui {

enum class SavedataDialogKind { Save, Load, Delete };

struct SavedataDialogEntry {
    std::string save_name;       // "SAVE00": what the game named this slot
    bool exists{};               // false: New Data
    std::string title;           // PARAM.SFO TITLE: the game
    std::string savedata_title;  // SAVEDATA_TITLE: this save
    std::string detail;          // SAVEDATA_DETAIL
    std::time_t modified{};      // when the save was last written
    std::uint64_t size_bytes{};  // the folder's files
    std::vector<std::uint8_t> icon0;  // ICON0.PNG, empty for none
    std::vector<std::uint8_t> pic1;   // PIC1.PNG, empty for none
};

struct SavedataDialogRequest {
    SavedataDialogKind kind{SavedataDialogKind::Save};
    std::vector<SavedataDialogEntry> entries;
    std::size_t focus{};  // the entry the cursor starts on
    // What a new save would hold: the game's icon, picture, titles and the
    // size it asks for. Shown for New Data entries.
    SavedataDialogEntry new_data;
    // The PSP's language numbers: 0 Japanese, 1 English, 10 Traditional
    // Chinese, 11 Simplified Chinese.
    std::uint32_t language{1};
    // The console's confirm button is cross, not circle: which of the two
    // the hints name for OK.
    bool confirm_cross{};
};

// Does the work for the chosen entry; true when it succeeded.
using SavedataPerform = std::function<bool(std::size_t index)>;

enum class SavedataDialogOutcome { Done, Cancelled, NoData };

// Called once when the dialog closes, with the entry the work was done on
// (meaningless for Cancelled and NoData).
using SavedataDialogDone = std::function<void(SavedataDialogOutcome outcome, std::size_t index)>;

// Opens the dialog over the running game. The game keeps running, as it
// does behind the PSP's dialog, but reads a neutral pad, and the window keeps
// the frame that was on screen. False when there is no interface to draw it
// in; the caller then answers the request itself.
bool open_game_savedata_dialog(SavedataDialogRequest request, SavedataPerform perform, SavedataDialogDone done);

[[nodiscard]] bool savedata_dialog_open();

// Builds this frame of the dialog and handles its input. Call inside an
// interface frame over the game (draw_over_game does).
void savedata_dialog_frame();

} // namespace portablekit::ui
