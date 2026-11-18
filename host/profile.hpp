#pragma once

// What the framework has to ask a game rather than assume about it.
//
// Everything under host/ is meant to work for any PSP title. Wherever it used
// to name Monster Hunter Portable 3rd, its disc, its memory layout or its save
// folders, it now reads one of these fields instead. A port supplies exactly
// one definition of portablekit::game() and the framework links against it.
//
// The rule for what belongs here: a value the framework needs but cannot
// derive from the executable it was given. Anything the ELF already says (its
// entry point, its segments, its imports) is read from the ELF, not declared.

#include "input/bindings.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace psprecomp {
class Elf32Image;
class Runtime;
} // namespace psprecomp

namespace portablekit {

class HleRegistrar;
class IsoImage;
namespace gpu::interpolation {
struct CutThresholds;
}

// One save folder on the memory stick, as the game names it.
struct SaveFolder {
    const char *name;        // SAVEDATA_DIRECTORY, e.g. "ULJM05800QST"
    const char *description; // shown in the save screen, e.g. "Downloaded quests"
    bool exported;           // included when the player exports or backs up
};

// A choice of what LT/RT (L2/R2) press past the trigger point, beside the
// framework's own: L and R, like the shoulders.
struct TriggerProfile {
    const char *key;     // settings.ini and <prefix>_PAD_TRIGGERS, e.g. "bows"
    const char *label;   // the menu, e.g. "Bows (R / △)"
    std::uint32_t left;  // the PSP buttons L2 presses, as SceCtrlButtons bits
    std::uint32_t right; // and R2
};

// A game played by tilting its world (LocoRoco tilts with L and R): what the
// device's roll presses when the player turns tilt controls on
// (input/tilt.hpp, input/motion.hpp).
struct TiltControls {
    std::uint32_t left;  // the PSP buttons held to tilt left, as SceCtrlButtons bits
    std::uint32_t right; // and right
    // True when the game eases its tilt towards the held side, so pressing
    // a button part of the time tilts it part of the way: the roll between
    // the dead zone and a full tilt then presses for that share of frames.
    // False: past the dead zone the button is simply held.
    bool proportional;
    // What the menu adds about the game's own controls, a whole sentence.
    const char *note;
};

// Another release of the game than the one a port supports, and what the
// installer tells a player who hands it that one.
struct OtherRelease {
    const char *disc_id; // DISC_ID in its PARAM.SFO, e.g. "ULJM05800"
    const char *note;    // a whole sentence, e.g. "This is the original PSP release of the game, ..."
};

// A translation of one of the profile's own interface texts (an add-on
// disc's name and description, its build's stages), which the framework's
// catalogues cannot know.
struct ProfileText {
    const char *language;   // an interface language code: "zh-Hans"
    const char *english;    // the text as the profile gives it
    const char *translated;
};

// What an add-on disc's build is given (AddOnDisc::build).
struct AddOnBuild {
    IsoImage &add_on;    // the image the player chose
    IsoImage &game_disc; // the game's own, as installed
    // Where the build writes: ms0's folder (files the game finds on the memory
    // stick) and the disc overlay's (files read in place of the disc's;
    // kernel/disc_patch.hpp for patches). Both start empty; the framework
    // moves what they hold into place once the build has returned.
    std::filesystem::path memory_stick;
    std::filesystem::path disc_overlay;
    // Progress for the player: a stage name, done and total. Throws
    // install::InstallCancelled when the player cancels.
    std::function<void(const std::string &stage, std::uint64_t done, std::uint64_t total)> progress;
};

// Another disc the game can take content from: an expansion, or an earlier
// release whose episodes the game plays. Setup offers each one as an optional
// step after the game's own disc (and `--add-disc image` adds one later); the
// profile's build turns the player's image into files the game reads, so
// neither disc is ever changed or shipped.
struct AddOnDisc {
    const char *key;         // "season1": lower case, digits and '-'
    const char *name;        // "Trick x Logic Season 1", for the player
    const char *description; // what adding it does, a whole sentence
    std::span<const char *const> disc_ids; // DISC_ID values in its PARAM.SFO it accepts
    // Throws install::InstallError for a problem the player can act on.
    void (*build)(const AddOnBuild &);
};

// Where a game keeps the atlas of glyphs it has drawn, so that changing the
// font while it runs can make it draw them again. Read off the game's own
// text code; every number is the game's.
struct GlyphCacheLayout {
    // Return address of the game's call to sceFontGetCharGlyphImage, and the
    // register that holds the atlas object there (17 is s1).
    std::uint32_t caller;
    std::uint32_t object_register;
    // Offsets in that object: the cell width and height (u8 each), the
    // number of cells in the whole atlas (u16), and the table from character
    // code to cell (u16 each, 0xFFFF meaning not drawn yet).
    std::uint32_t cell_width_offset;
    std::uint32_t cell_height_offset;
    std::uint32_t cell_count_offset;
    std::uint32_t code_to_cell_offset;
    std::uint32_t code_to_cell_entries;
    // Pages of 256x256 the atlas spans, and the extra rows between cell rows;
    // with the cell size, what the cell count must be for the object to be
    // the one expected.
    std::uint32_t atlas_pages;
    std::uint32_t row_gap;
};

// A driver for the game's own camera. Every member may be null; the framework
// reads camera/camera_driver.hpp's defaults for those.
struct CameraDriver {
    // Once per game frame, at the flip (never per interpolated present).
    void (*frame)(psprecomp::Runtime &) = nullptr;
    // The port is driving the camera right now, so the game must not also act
    // on the second stick: its own turn would fight the driver's.
    bool (*driving)() = nullptr;
    // The game is aiming under the driver: the second stick goes to the game
    // stretched to full length, so the game's aim code steps and the driver
    // sizes each step.
    bool (*aim_boost)() = nullptr;
    // While aiming with the mouse, the direction (-1..1 each axis) the second
    // stick should show the game this sample. False: the mouse has not moved.
    bool (*mouse_aim)(float &x, float &y) = nullptr;
    // Where the port does not drive the camera, the mouse can only switch the
    // game's own turn: -1 or +1 while it moves left or right, 0 otherwise.
    int (*mouse_stock_turn)() = nullptr;
    // Full-deflection speed of the current camera, in degrees a second. Null:
    // the Camera speed setting.
    float (*degrees_per_second)() = nullptr;
};

// ProfileVariant::dialog_language: the profile's own GameProfile::dialog_language.
inline constexpr std::uint32_t kProfileDialogLanguage = 0xFFFFFFFEu;

// One edition of the game a profile covers beside the one its own fields
// describe: another executable of the same disc id, such as the release with
// a fan patch applied (FUComplete on Monster Hunter Portable 2nd G) or a
// translation. Each is known by its executable's hash, runs its own corpus
// (a port builds it from generated-variants/<key>/), and shares the saves.
struct ProfileVariant {
    const char *key;  // "fuc-1.4": its corpus's name; lower case, digits, '-' and '_'
    const char *name; // "FUComplete 1.4", for the player
    // SHA-256 of EBOOT.BIN on the disc when it is encrypted (the installer
    // decrypts it with the profile's key), or null when this edition's
    // EBOOT.BIN is the plain executable already.
    const char *encrypted_executable_sha256 = nullptr;
    const char *executable_sha256 = nullptr;
    // What this edition does differently; null or empty: the profile's own.
    void (*register_extra_hle)(HleRegistrar &) = nullptr;
    void (*patch_loaded_image)(psprecomp::Runtime &, const psprecomp::Elf32Image &) = nullptr;
    std::span<const std::uint32_t> overlay_slots = {};
    // Its code is the profile's own executable's, byte for byte, and only its
    // data differs (a translation that changes text alone): the corpus linked
    // into the port runs it, and it needs no corpus of its own. Set it only
    // after checking: a corpus generated from this executable must differ
    // from the profile's in nothing but the hash it records.
    bool same_code_as_profile = false;
    // The language of its system dialogs (GameProfile::dialog_language), for
    // an edition in another language than the profile's own.
    std::uint32_t dialog_language = kProfileDialogLanguage;
};

// GameProfile::dialog_language: draw the system's dialogs in the language the
// game is told the console uses.
inline constexpr std::uint32_t kFollowSystemLanguage = 0xFFFFFFFFu;

struct GameProfile {
    // --- What the port is called -------------------------------------------
    const char *app_name;          // the executable: "MHP3rdNative"
    const char *project_name;      // what the player sees: "Yakumo"
    // Environment variables are read as "<env_prefix>_NAME", so every port has
    // its own set and two of them can run side by side without sharing state.
    const char *env_prefix;        // "MHP3RD"
    const char *data_organization; // SDL_GetPrefPath organization
    const char *data_application;  // SDL_GetPrefPath application

    // --- The one release the installer accepts -----------------------------
    const char *disc_id;                 // DISC_ID in PSP_GAME/PARAM.SFO
    const char *disc_id_display;         // the same, as it is printed
    const char *game_title;
    const char *executable_path_on_disc; // "PSP_GAME/SYSDIR/EBOOT.BIN"
    const char *param_sfo_path_on_disc;  // "PSP_GAME/PARAM.SFO"
    // Which release that is, as the installer names it when it is handed
    // another one: "the Japanese release". Null: it is named by disc id alone.
    const char *release_name = nullptr;
    // Releases a player may mistake for it. The installer says the note of
    // the one it was handed, instead of "Other releases and regions are not
    // supported."
    std::span<const OtherRelease> other_releases = {};
    // SHA-256 of the encrypted executable on the disc, and of the executable
    // the recompiled code was generated from. The installer checks both.
    const char *encrypted_executable_sha256;
    const char *executable_sha256;
    // The "~PSP" header's tag at offset 0xD0 and the key it selects. The
    // framework knows the header layout; only the key is per-release.
    std::uint32_t decryption_tag;
    std::array<std::uint8_t, 16> decryption_key;
    // For a tag that selects the older header layout, the 0x90-byte table it
    // selects instead of a 16-byte key, as the published tables give it
    // (already scrambled). Empty: the tag selects decryption_key.
    std::span<const std::uint8_t> decryption_key_table = {};
    // Other editions of the same disc (see ProfileVariant). Empty: only the
    // executable above is known.
    std::span<const ProfileVariant> variants = {};
    // An unencrypted executable that is none of the known ones (a patch in a
    // version nobody listed yet): true installs and runs it under the
    // interpreter, with a warning; false refuses it at setup.
    bool run_unknown_executables = false;

    // --- How the game sits in guest memory ---------------------------------
    std::uint32_t load_base;
    std::uint32_t guest_ram_bytes;
    // argv[0] the game is started with, e.g. "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN".
    const char *boot_path;
    // Addresses of the code overlay slots inside the load image's BSS, from
    // the executable's section table. The end of each slot is the start of the
    // next, so the last entry is the end of the load image. Empty for a game
    // that loads no overlays.
    std::span<const std::uint32_t> overlay_slots;
    // Overlay libraries export <prefix>_overlay_info and
    // <prefix>_register_overlay. The framework's own are "portablekit_"; a
    // port that built its overlay libraries before it moved onto the framework
    // names the prefix they were built with, so they load without being
    // rebuilt. Null: only the framework's own names are looked for.
    // Temporary: meant to go at a port's next planned rebuild of its overlays
    // (for Yakumo, the next release build), and then from here as well.
    const char *legacy_overlay_symbol_prefix = nullptr;

    // Other discs the game can take content from (see AddOnDisc).
    std::span<const AddOnDisc> add_on_discs = {};
    // Translations of the profile's own interface texts (see ProfileText).
    std::span<const ProfileText> text = {};

    // --- Save data ---------------------------------------------------------
    const char *save_game_name;             // the main folder, e.g. "ULJM05800"
    std::span<const SaveFolder> save_folders;

    // --- What the player is called -----------------------------------------
    // How the interface names the name the game asks for: "Hunter name".
    const char *player_name_label = "Player name";
    // The name given when the game asks and the on-screen keyboard is off,
    // until the player sets one.
    const char *default_player_name = "Player";

    // --- Ad hoc multiplayer ------------------------------------------------
    // The product code the game announces to other players. Two players must
    // agree on it, so it is the original release's code even when the port's
    // disc id is a different one.
    const char *adhoc_product_code;

    // --- Controls ----------------------------------------------------------
    // What the triggers may press instead of L and R, for the way this game is
    // played. Empty: they press L and R, and the menu offers nothing else.
    std::span<const TriggerProfile> trigger_profiles = {};
    // What the menu says about them, after "Standard: L and R, like the
    // shoulders."
    const char *trigger_profiles_note = nullptr;
    // Tilt controls from motion sensors. Null: the game does not tilt, and
    // the menu offers none.
    const TiltControls *tilt = nullptr;
    // The keyboard and mouse this game starts with, for a game played
    // otherwise than the framework's defaults suppose (WASD and the mouse
    // for a camera): gets the framework's bindings and mouse setting and
    // changes what it needs. Only what settings.ini does not hold is taken
    // from here, so a player's own bindings stay. Null: the framework's.
    void (*keyboard_defaults)(input::Bindings &bindings, bool &mouse) = nullptr;

    // --- Hooks -------------------------------------------------------------
    // Called after the framework has registered its own HLE, so a game can add
    // calls no other game makes or replace one the framework gets wrong for it.
    // Null means there are none.
    void (*register_extra_hle)(HleRegistrar &) = nullptr;
    // Called once the executable is in memory and before it runs, for the
    // per-game patches that have no better home. Null means there are none.
    void (*patch_loaded_image)(psprecomp::Runtime &, const psprecomp::Elf32Image &) = nullptr;

    // --- The game's camera and view ----------------------------------------
    // Drives the game's own camera from what the player asks of it
    // (camera/camera_input.hpp). Only a game knows where its camera lives and
    // when it may be moved. Null: the framework never touches the camera, and
    // the second stick and the mouse reach the game as they are.
    const CameraDriver *camera = nullptr;
    // Once per game frame, at the flip: gives the game's 3D view `aspect`
    // (width over height of the picture it is drawn into). Only a game knows
    // where it keeps its projection. Null: the game draws only the PSP's
    // 480:272, and the renderer does not offer to widen it.
    void (*view_aspect_frame)(psprecomp::Runtime &, float aspect) = nullptr;

    // --- Frame interpolation -----------------------------------------------
    // What tells a camera cut from motion when frames are blended
    // (gpu/frame_interpolation.hpp). The angles and fractions there hold for
    // any game; the distances are in the game's own world units, and the
    // defaults were measured on Monster Hunter Portable 3rd. A game whose world
    // is on another scale gives its own. Null: the defaults.
    const gpu::interpolation::CutThresholds *interpolation_thresholds = nullptr;
    // The game's own frame, in vblanks of the PSP's display: 2 for a game
    // that makes 30 frames a second (Yakumo), 1 for one that makes 60
    // (Purun). Frame interpolation presents between frames of this length,
    // and the Frame rate setting offers only rates above the game's own.
    std::uint32_t frame_vblanks = 2;

    // --- Loading -------------------------------------------------------------
    // Fast loading (kernel/fast_loading.hpp) lets emulated time run ahead
    // while the game reads the disc in silence, with no button held, no
    // movie, no menu and no ad hoc play. For what only a game can tell apart
    // from a load, such as a scene that reads the disc silently while the
    // player watches: false keeps real time now. Called at every vblank on
    // the emulation thread. Null: the framework's own guards decide alone.
    bool (*fast_loading_allowed)() = nullptr;

    // --- The console's settings the game reads ----------------------------
    // What sceUtilityGetSystemParamInt and sceImposeGetLanguageMode report: the
    // system language (0 Japanese, 1 English, 2 French, ...) and the confirm
    // button (0 circle, 1 cross). A console of the release's region is what
    // the game expects; the defaults are a Japanese one.
    std::uint32_t system_language = 0u;
    std::uint32_t confirm_button = 0u;
    // The language the release's text is in, by the same numbers;
    // kFollowSystemLanguage takes system_language. Apart from it so a
    // translated release can be shown as its translation while the game
    // still sees the console its code expects. The system's own dialogs
    // (the save-data list) are drawn in it: Japanese 0, English 1,
    // Traditional Chinese 10 and Simplified Chinese 11 have their own text,
    // the rest English. A Chinese or Korean one (9) also draws the game's
    // text, and the interface's CJK characters, with a face of that script
    // where the system has one: a Japanese face shapes some Chinese
    // characters differently, and may have no Hangul.
    std::uint32_t dialog_language = kFollowSystemLanguage;

    // --- Text ----------------------------------------------------------------
    // Where the game caches the glyphs it has drawn. Null: a font changed while
    // the game runs applies to text the game has not drawn yet.
    const GlyphCacheLayout *glyph_cache = nullptr;
};

// Defined by the port, exactly once. Everything under host/ reads the game
// through this and nothing else.
[[nodiscard]] const GameProfile &game();

// GameProfile::dialog_language, or the running edition's, with
// kFollowSystemLanguage resolved.
[[nodiscard]] std::uint32_t text_language(const GameProfile &profile);

// --- Editions (ProfileVariant) ----------------------------------------------
// The edition whose executable, or encrypted EBOOT.BIN, has this SHA-256; null
// for the profile's own and for an unknown one.
[[nodiscard]] const ProfileVariant *find_variant(std::string_view executable_sha256);
[[nodiscard]] const ProfileVariant *find_variant_by_encrypted(std::string_view eboot_sha256);
// The profile's own executable or one of its editions'.
[[nodiscard]] bool is_known_executable(std::string_view executable_sha256);
// The edition running now, chosen by host/main.cpp from the executable's hash;
// null for the profile's own (and before it is known).
void set_active_variant(const ProfileVariant *variant);
[[nodiscard]] const ProfileVariant *active_variant();
// The profile's hooks and overlay slots as the running edition sets them.
using ExtraHleHook = void (*)(HleRegistrar &);
using PatchImageHook = void (*)(psprecomp::Runtime &, const psprecomp::Elf32Image &);
[[nodiscard]] ExtraHleHook active_register_extra_hle();
[[nodiscard]] PatchImageHook active_patch_loaded_image();
[[nodiscard]] std::span<const std::uint32_t> active_overlay_slots();

// getenv("<env_prefix>_" + name), so host code names the variable without the
// game's prefix: env("TRACE_GE") reads <prefix>_TRACE_GE in Yakumo.
[[nodiscard]] const char *env(const char *name);
// True when the variable is set to anything at all, the convention every
// switch in the port already used.
[[nodiscard]] bool env_set(const char *name);
// The full name of that variable, for messages and for the menu's list of
// settings the environment has overridden.
[[nodiscard]] std::string env_name(const char *name);

} // namespace portablekit
