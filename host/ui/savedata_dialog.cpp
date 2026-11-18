#include "ui/savedata_dialog.hpp"

#include "../profile.hpp"
#include "audio/audio_sink.hpp"
#include "ui/layer.hpp"

#include "gpu/vulkan_renderer.hpp"

#include "imgui.h"
#include "imgui_internal.h"

// A private copy of the PNG decoder: texture_pack.cpp keeps its own static,
// and save icons are PNG only.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace portablekit::ui {
namespace {

// The dialog is laid out in the PSP's own 480x272 screen and scaled to the
// game's picture, so it sits where the PSP would draw it.
constexpr float kScreenWidth = 480.0f;
constexpr float kScreenHeight = 272.0f;

// ICON0 is 144x80 on the PSP; the atlas keeps one cell per entry, plus one
// for New Data.
constexpr int kIconWidth = 144;
constexpr int kIconHeight = 80;
constexpr int kAtlasColumns = 8;
constexpr int kAtlasRows = 9;
constexpr std::size_t kAtlasCells = static_cast<std::size_t>(kAtlasColumns) * kAtlasRows;
constexpr int kPictureWidth = 480;
constexpr int kPictureHeight = 272;

// The column of saves: the focused one full size, the others smaller.
constexpr float kFocusX = 27.0f;
constexpr float kFocusY = 97.0f;
constexpr float kSmallWidth = 81.0f;
constexpr float kSmallHeight = 45.0f;
constexpr float kSmallX = kFocusX + (kIconWidth - kSmallWidth) * 0.5f;
constexpr float kGap = 4.0f;
constexpr float kFocusGap = 12.0f;
constexpr float kAboveY = kFocusY - kFocusGap - kSmallHeight;
constexpr float kBelowY = kFocusY + kIconHeight + kFocusGap;
// The banner across the top, which the column scrolls under.
constexpr float kBannerHeight = 28.0f;

// How long "Saving..." stays up at least, in frames, so the player sees it.
constexpr int kWorkingFrames = 24;
// Seconds the dialog takes to fade in and out, and the text that changes
// with the cursor or the screen.
constexpr double kFade = 0.2;
constexpr double kTextFade = 0.15;

enum class Screen { List, Confirm, Working, Result, NoData };

// --- Text ---------------------------------------------------------------

struct Strings {
    const char *save, *load, *erase;
    const char *new_data;
    const char *overwrite, *erase_question;
    const char *saving, *loading, *erasing;
    const char *saved, *save_failed, *loaded, *load_failed, *erased, *erase_failed;
    const char *no_data;
    const char *yes, *no, *ok, *back;
};

constexpr Strings kEnglish{
    "Save", "Load", "Delete",
    "New Data",
    "Do you want to overwrite the data?", "Do you want to delete this data?",
    "Saving...\nPlease wait.", "Loading...\nPlease wait.", "Deleting...\nPlease wait.",
    "Save completed.", "Unable to save data.", "Load completed.", "Unable to load data.\nThe data is corrupted.",
    "Delete completed.", "Unable to delete data.",
    "There is no data.",
    "Yes", "No", "OK", "Back",
};

constexpr Strings kJapanese{
    "セーブ", "ロード", "削除",
    "新しいデータ",
    "このデータに上書きしますか？", "このデータを削除しますか？",
    "セーブしています。\nしばらくお待ちください。", "ロードしています。\nしばらくお待ちください。",
    "削除しています。\nしばらくお待ちください。",
    "セーブが完了しました。", "セーブできませんでした。", "ロードが完了しました。",
    "ロードできませんでした。\nデータが壊れています。", "削除が完了しました。", "削除できませんでした。",
    "データがありません。",
    "はい", "いいえ", "決定", "戻る",
};

constexpr Strings kSimplifiedChinese{
    "保存", "读取", "删除",
    "新建数据",
    "要覆盖此数据吗？", "要删除此数据吗？",
    "正在保存。\n请稍候。", "正在读取。\n请稍候。", "正在删除。\n请稍候。",
    "保存完成。", "无法保存数据。", "读取完成。", "无法读取数据。\n数据已损坏。",
    "删除完成。", "无法删除数据。",
    "没有数据。",
    "是", "否", "确定", "返回",
};

constexpr Strings kTraditionalChinese{
    "儲存", "讀取", "刪除",
    "新增資料",
    "要覆寫此資料嗎？", "要刪除此資料嗎？",
    "正在儲存。\n請稍候。", "正在讀取。\n請稍候。", "正在刪除。\n請稍候。",
    "儲存完成。", "無法儲存資料。", "讀取完成。", "無法讀取資料。\n資料已損毀。",
    "刪除完成。", "無法刪除資料。",
    "沒有資料。",
    "是", "否", "確定", "返回",
};

const Strings &strings_for(std::uint32_t language) {
    switch (language) {
    case 0u: return kJapanese;
    case 10u: return kTraditionalChinese;
    case 11u: return kSimplifiedChinese;
    default: return kEnglish;
    }
}

// --- Sounds -------------------------------------------------------------

enum class Sound { Move, Confirm, Back };

// Short tones in place of the PSP's own: a tick to move, rising to choose,
// falling to go back. 44100 Hz stereo, as the sink plays.
std::vector<std::int16_t> synthesise(std::initializer_list<std::pair<float, float>> notes) {
    constexpr float kRate = 44100.0f;
    constexpr float kAmplitude = 0.14f * 32767.0f;
    std::vector<std::int16_t> frames;
    for (const auto &[frequency, seconds] : notes) {
        const auto count = static_cast<std::size_t>(seconds * kRate);
        for (std::size_t i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / kRate;
            // A quick attack and an exponential tail, so nothing clicks.
            const float envelope = std::min(1.0f, t * 800.0f) * std::exp(-t * 6.0f / seconds);
            const auto value = static_cast<std::int16_t>(kAmplitude * envelope * std::sin(6.2831853f * frequency * t));
            frames.push_back(value);
            frames.push_back(value);
        }
    }
    return frames;
}

void play(Sound sound) {
    static const bool silent = portablekit::env("NO_DIALOG_SOUNDS") != nullptr;
    if (silent || !audio::AudioSink::instance().has_device()) return;
    static const std::vector<std::int16_t> move = synthesise({{1760.0f, 0.035f}});
    static const std::vector<std::int16_t> confirm = synthesise({{1320.0f, 0.03f}, {1980.0f, 0.06f}});
    static const std::vector<std::int16_t> back = synthesise({{1180.0f, 0.03f}, {880.0f, 0.06f}});
    const std::vector<std::int16_t> &frames = sound == Sound::Move ? move : sound == Sound::Confirm ? confirm : back;
    // Behind the device, so the sink starts it at its write target.
    static std::uint64_t cursor = 0u;
    audio::AudioSink::instance().mix(cursor, frames.data(), frames.size() / 2u, 0x8000u, 0x8000u);
}

// --- State --------------------------------------------------------------

struct Textures {
    ImTextureData *icons{};
    ImTextureData *picture{};
    // Which entry's PIC1 the picture texture holds: an entry index, the New
    // Data picture (kNewData), or nothing yet (kNone).
    std::size_t picture_of{};
    bool picture_valid{};
};

constexpr std::size_t kNone = static_cast<std::size_t>(-1);
constexpr std::size_t kNewData = static_cast<std::size_t>(-2);

struct Dialog {
    bool open{};
    SavedataDialogRequest request;
    const Strings *text{&kEnglish};
    SavedataPerform perform;
    SavedataDialogDone done;
    std::size_t cursor{};
    float scroll{};  // the cursor as the column shows it, catching up
    Screen screen{Screen::List};
    bool yes{};          // the Yes/No choice of a question
    bool succeeded{};    // what the last perform returned
    int working_frames{};
    double opened_at{};
    double cursor_moved_at{};
    double screen_since{};
    // Fading out: when it started and what the game is told after.
    std::optional<double> closing_at;
    SavedataDialogOutcome closing_outcome{SavedataDialogOutcome::Cancelled};
    // Which atlas cell each entry's icon is in, and whether it has one.
    std::vector<bool> has_icon;
    bool new_data_icon{};
};

Dialog &dialog() {
    static Dialog value;
    return value;
}

Textures &textures() {
    static Textures value;
    return value;
}

// The whole dialog's opacity this frame, from its fade.
float g_alpha = 1.0f;

ImU32 faded(ImU32 color, float alpha = 1.0f) {
    const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) * alpha * g_alpha;
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(a, 0.0f, 255.0f)) << IM_COL32_A_SHIFT);
}

float since(double start, double length) {
    return static_cast<float>(std::clamp((ImGui::GetTime() - start) / length, 0.0, 1.0));
}

void set_screen(Screen screen) {
    Dialog &d = dialog();
    d.screen = screen;
    d.screen_since = ImGui::GetTime();
    if (screen == Screen::Working) d.working_frames = 0;
}

// --- Images -------------------------------------------------------------

struct Rgba {
    int width{};
    int height{};
    std::vector<std::uint32_t> pixels;  // R, G, B, A bytes in memory order
};

std::optional<Rgba> decode_png(const std::vector<std::uint8_t> &bytes) {
    if (bytes.empty()) return std::nullopt;
    int width = 0, height = 0, channels = 0;
    stbi_uc *data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (data == nullptr || width <= 0 || height <= 0) {
        if (data != nullptr) stbi_image_free(data);
        return std::nullopt;
    }
    Rgba image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height);
    std::memcpy(image.pixels.data(), data, image.pixels.size() * 4u);
    stbi_image_free(data);
    return image;
}

std::uint32_t pack(float r, float g, float b, float a) {
    const auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); };
    return byte(r) | (byte(g) << 8u) | (byte(b) << 16u) | (byte(a) << 24u);
}

// Bilinear sample of `src` at a texel-centre coordinate.
void sample(const Rgba &src, float u, float v, float out[4]) {
    const float x = std::clamp(u - 0.5f, 0.0f, static_cast<float>(src.width - 1));
    const float y = std::clamp(v - 0.5f, 0.0f, static_cast<float>(src.height - 1));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, src.width - 1), y1 = std::min(y0 + 1, src.height - 1);
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    const auto at = [&](int px, int py, int c) {
        return static_cast<float>((src.pixels[static_cast<std::size_t>(py) * src.width + px] >> (8u * c)) & 0xFFu);
    };
    for (int c = 0; c < 4; ++c) {
        const float top = at(x0, y0, c) * (1.0f - fx) + at(x1, y0, c) * fx;
        const float bottom = at(x0, y1, c) * (1.0f - fx) + at(x1, y1, c) * fx;
        out[c] = top * (1.0f - fy) + bottom * fy;
    }
}

// Draws `src` into a w x h region of `dst` (row length `stride`), scaled to
// fit with its shape kept and centred, over black: the PSP draws save icons
// over black, so their transparent parts come out black.
void blit_fitted(const Rgba &src, std::uint32_t *dst, int stride, int w, int h) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) dst[static_cast<std::size_t>(y) * stride + x] = pack(0, 0, 0, 255);
    const float scale = std::min(static_cast<float>(w) / src.width, static_cast<float>(h) / src.height);
    const int fitted_w = std::max(1, static_cast<int>(std::lround(src.width * scale)));
    const int fitted_h = std::max(1, static_cast<int>(std::lround(src.height * scale)));
    const int left = (w - fitted_w) / 2, top = (h - fitted_h) / 2;
    for (int y = 0; y < fitted_h; ++y)
        for (int x = 0; x < fitted_w; ++x) {
            float c[4];
            sample(src, (static_cast<float>(x) + 0.5f) / scale, (static_cast<float>(y) + 0.5f) / scale, c);
            const float a = c[3] / 255.0f;
            dst[static_cast<std::size_t>(top + y) * stride + left + x] = pack(c[0] * a, c[1] * a, c[2] * a, 255);
        }
}

ImTextureData *make_texture(int width, int height) {
    auto *texture = new ImTextureData();
    texture->Create(ImTextureFormat_RGBA32, width, height);
    ImGui::RegisterUserTexture(texture);
    return texture;
}

void upload(ImTextureData *texture) {
    // A texture the backend has not created yet takes its pixels when it is.
    if (texture->Status == ImTextureStatus_OK || texture->Status == ImTextureStatus_WantUpdates)
        ImTextureDataQueueUpload(texture, 0, 0, texture->Width, texture->Height);
}

ImVec2 cell_uv(std::size_t cell) {
    const int column = static_cast<int>(cell % kAtlasColumns);
    const int row = static_cast<int>(cell / kAtlasColumns);
    return {static_cast<float>(column * kIconWidth) / (kAtlasColumns * kIconWidth),
            static_cast<float>(row * kIconHeight) / (kAtlasRows * kIconHeight)};
}

// One icon into its atlas cell; false when it is not a PNG.
bool put_icon(std::size_t cell, const std::vector<std::uint8_t> &png) {
    ImTextureData *icons = textures().icons;
    if (icons == nullptr || cell >= kAtlasCells) return false;
    const auto image = decode_png(png);
    if (!image) return false;
    auto *pixels = static_cast<std::uint32_t *>(icons->GetPixels());
    const int stride = kAtlasColumns * kIconWidth;
    const int column = static_cast<int>(cell % kAtlasColumns), row = static_cast<int>(cell / kAtlasColumns);
    blit_fitted(*image, pixels + static_cast<std::size_t>(row * kIconHeight) * stride + column * kIconWidth, stride,
                kIconWidth, kIconHeight);
    return true;
}

std::size_t new_data_cell() {
    return std::min(dialog().request.entries.size(), kAtlasCells - 1u);
}

// Every entry's icon into the atlas, once per opening; the New Data icon in
// the cell after the last entry.
void build_icons() {
    Dialog &d = dialog();
    Textures &t = textures();
    if (t.icons == nullptr) t.icons = make_texture(kAtlasColumns * kIconWidth, kAtlasRows * kIconHeight);
    const std::size_t count = new_data_cell();
    d.has_icon.assign(d.request.entries.size(), false);
    for (std::size_t i = 0; i < count; ++i)
        if (d.request.entries[i].exists) d.has_icon[i] = put_icon(i, d.request.entries[i].icon0);
    d.new_data_icon = put_icon(count, d.request.new_data.icon0);
    upload(t.icons);
}

// The focused entry's PIC1, or the game's for New Data, when it changes.
void update_picture() {
    Dialog &d = dialog();
    Textures &t = textures();
    const SavedataDialogEntry *entry = d.request.entries.empty() ? nullptr : &d.request.entries[d.cursor];
    const bool new_data = entry == nullptr || !entry->exists;
    const std::size_t wanted = new_data ? kNewData : d.cursor;
    if (t.picture != nullptr && t.picture_of == wanted) return;
    if (t.picture == nullptr) t.picture = make_texture(kPictureWidth, kPictureHeight);
    t.picture_of = wanted;
    const auto image = decode_png(new_data ? d.request.new_data.pic1 : entry->pic1);
    t.picture_valid = image.has_value();
    if (!image) return;
    blit_fitted(*image, static_cast<std::uint32_t *>(t.picture->GetPixels()), kPictureWidth, kPictureWidth,
                kPictureHeight);
    upload(t.picture);
}

// --- Layout -------------------------------------------------------------

struct Frame {
    ImVec2 origin;
    float scale{};
    ImVec2 at(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }
    float size(float v) const { return v * scale; }
};

// The PSP's screen, fitted into the window with its shape kept.
Frame screen_frame() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = std::min(display.x / kScreenWidth, display.y / kScreenHeight);
    return {{(display.x - kScreenWidth * scale) * 0.5f, (display.y - kScreenHeight * scale) * 0.5f}, scale};
}

void text(ImDrawList *draw, const Frame &f, float x, float y, float size, ImU32 color, const std::string &value,
          float wrap = 0.0f) {
    if (value.empty()) return;
    // A shadow under every line, as the PSP's dialogs have.
    const float font_size = f.size(size);
    const ImVec2 at = f.at(x, y);
    const float wrap_px = wrap > 0.0f ? f.size(wrap) : 0.0f;
    const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) / 255.0f;
    draw->AddText(ImGui::GetFont(), font_size, {at.x + f.size(0.8f), at.y + f.size(0.8f)},
                  faded(IM_COL32(0, 0, 0, 160), alpha), value.c_str(), nullptr, wrap_px);
    draw->AddText(ImGui::GetFont(), font_size, at, faded(color), value.c_str(), nullptr, wrap_px);
}

ImVec2 measure(const Frame &f, float size, const std::string &value, float wrap = 0.0f) {
    const ImVec2 px_size = ImGui::GetFont()->CalcTextSizeA(f.size(size), FLT_MAX, wrap > 0.0f ? f.size(wrap) : 0.0f,
                                                           value.c_str());
    return {px_size.x / f.scale, px_size.y / f.scale};
}

std::string format_time(std::time_t time) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char text[48];
    std::snprintf(text, sizeof(text), "%04d/%02d/%02d   %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min);
    return text;
}

std::string format_size(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%llu KB", static_cast<unsigned long long>((bytes + 1023u) / 1024u));
    return text;
}

const char *banner_text() {
    const Dialog &d = dialog();
    switch (d.request.kind) {
    case SavedataDialogKind::Save: return d.text->save;
    case SavedataDialogKind::Load: return d.text->load;
    case SavedataDialogKind::Delete: return d.text->erase;
    }
    return "";
}

struct Box {
    float x, y, w, h;
};

Box lerp(const Box &a, const Box &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
}

const Box kFocusBox{kFocusX, kFocusY, static_cast<float>(kIconWidth), static_cast<float>(kIconHeight)};

// Where the column puts the entry `offset` places from the one in focus; a
// fractional offset is on its way between two places as the column scrolls.
Box column_place(float offset) {
    const float distance = std::fabs(offset);
    const float step = kSmallHeight + kGap;
    if (distance < 1.0f) {
        const Box neighbour{kSmallX, offset < 0.0f ? kAboveY : kBelowY, kSmallWidth, kSmallHeight};
        return lerp(kFocusBox, neighbour, distance);
    }
    const float y = offset < 0.0f ? kAboveY - (distance - 1.0f) * step : kBelowY + (distance - 1.0f) * step;
    return {kSmallX, y, kSmallWidth, kSmallHeight};
}

void draw_icon(ImDrawList *draw, const Frame &f, std::size_t index, const Box &box, bool dim) {
    Dialog &d = dialog();
    const bool new_data = index == kNewData;
    const bool has = new_data ? d.new_data_icon : (index < d.has_icon.size() && d.has_icon[index]);
    const ImVec2 min = f.at(box.x, box.y), max = f.at(box.x + box.w, box.y + box.h);
    if (has && textures().icons != nullptr) {
        const ImVec2 uv0 = cell_uv(new_data ? new_data_cell() : index);
        const ImVec2 uv1{uv0.x + 1.0f / kAtlasColumns, uv0.y + 1.0f / kAtlasRows};
        draw->AddImage(textures().icons->GetTexRef(), min, max, uv0, uv1,
                       faded(dim ? IM_COL32(120, 120, 120, 255) : IM_COL32_WHITE));
    } else {
        draw->AddRectFilled(min, max, faded(IM_COL32(102, 102, 102, 136)));
    }
}

// An entry's icon as the column shows it: a save's own, or for New Data the
// game's icon dimmed.
void draw_entry_icon(ImDrawList *draw, const Frame &f, std::size_t index, const Box &box) {
    const auto &entry = dialog().request.entries[index];
    if (entry.exists) draw_icon(draw, f, index, box, false);
    else draw_icon(draw, f, kNewData, box, true);
}

void draw_focus_border(ImDrawList *draw, const Frame &f, const Box &box) {
    const float b = 1.2f;
    draw->AddRect(f.at(box.x - b, box.y - b), f.at(box.x + box.w + b, box.y + box.h + b),
                  faded(IM_COL32(255, 255, 255, 208)), 0.0f, 0, f.size(b));
}

void draw_list(ImDrawList *draw, const Frame &f) {
    Dialog &d = dialog();
    const std::size_t count = d.request.entries.size();
    // Under the banner, the column is cut off.
    draw->PushClipRect(f.at(0.0f, kBannerHeight), f.at(kScreenWidth, kScreenHeight), true);
    for (std::size_t i = 0; i < count; ++i) {
        if (i == d.cursor) continue;
        const Box box = column_place(static_cast<float>(i) - d.scroll);
        if (box.y > kScreenHeight || box.y + box.h < kBannerHeight) continue;
        draw_entry_icon(draw, f, i, box);
    }
    // The focused one over its neighbours, with its frame.
    const Box focused = column_place(static_cast<float>(d.cursor) - d.scroll);
    draw_entry_icon(draw, f, d.cursor, focused);
    draw_focus_border(draw, f, focused);
    draw->PopClipRect();
}

// Right of the column: the focused save's game title over a rule, its date
// and size under it, then its own title and detail. Fades in as the cursor
// lands.
void draw_list_info(ImDrawList *draw, const Frame &f) {
    Dialog &d = dialog();
    if (d.request.entries.empty()) return;
    const SavedataDialogEntry &entry = d.request.entries[d.cursor];
    const float alpha = since(d.cursor_moved_at, kTextFade);
    const auto white = [&](int level) { return IM_COL32(level, level, level, static_cast<int>(255 * alpha)); };
    constexpr float kLeft = 180.0f;
    constexpr float kRule = 136.0f;
    if (!entry.exists) {
        const std::string label = d.text->new_data;
        const ImVec2 size = measure(f, 16.0f, label);
        text(draw, f, kLeft, kRule - size.y * 0.5f, 16.0f, white(255), label);
        return;
    }
    const ImVec2 title = measure(f, 12.0f, entry.title);
    text(draw, f, kLeft, kRule - title.y - 2.0f, 12.0f, white(192), entry.title);
    draw->AddRectFilled(f.at(kLeft, kRule), f.at(kScreenWidth, kRule + 1.0f), faded(IM_COL32_WHITE, alpha));
    text(draw, f, kLeft, kRule + 3.0f, 10.0f, white(255),
         format_time(entry.modified) + "   " + format_size(entry.size_bytes));
    text(draw, f, kLeft - 5.0f, 159.0f, 12.0f, white(255), entry.savedata_title);
    text(draw, f, kLeft - 5.0f, 181.0f, 10.0f, white(255), entry.detail, kScreenWidth - kLeft - 8.0f);
}

// Under the icon on the question and message screens: what is being saved
// or loaded, when and how large.
void draw_summary(ImDrawList *draw, const Frame &f) {
    Dialog &d = dialog();
    const bool fresh = d.request.entries.empty() || !d.request.entries[d.cursor].exists ||
                       (d.request.kind == SavedataDialogKind::Save && d.screen != Screen::Confirm);
    const SavedataDialogEntry &entry = fresh ? d.request.new_data : d.request.entries[d.cursor];
    const std::time_t when = fresh ? std::time(nullptr) : entry.modified;
    const std::string lines = entry.savedata_title + "\n" + format_time(when) + "\n" + format_size(entry.size_bytes);
    text(draw, f, 8.0f, 200.0f, 10.0f, IM_COL32_WHITE, lines);
}

// The message on the right between two rules, with Yes and No under it for
// a question. Fades in when the screen changes.
void draw_message(ImDrawList *draw, const Frame &f, const std::string &message, bool question) {
    Dialog &d = dialog();
    const float alpha = since(d.screen_since, kTextFade);
    const ImU32 white = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));
    constexpr float kCentre = 334.0f;
    constexpr float kWrap = 254.0f;
    constexpr float kSize = 13.0f;
    const ImVec2 size = measure(f, kSize, message, kWrap);
    const float choices = question ? measure(f, kSize, d.text->yes).y + 6.0f : 0.0f;
    const float half = (size.y + choices) * 0.5f;
    const float top = 136.0f - half;
    // Each line centred on its own.
    float y = top;
    std::size_t start = 0;
    while (start <= message.size()) {
        const std::size_t end = message.find('\n', start);
        const std::string line = message.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const ImVec2 line_size = measure(f, kSize, line.empty() ? " " : line, kWrap);
        text(draw, f, kCentre - line_size.x * 0.5f, y, kSize, white, line, kWrap);
        y += line_size.y;
        if (end == std::string::npos) break;
        start = end + 1u;
    }
    if (question) {
        const float row = y + 6.0f;
        const auto choice = [&](const char *label, float centre, bool chosen) {
            const ImVec2 label_size = measure(f, kSize, label);
            if (chosen)
                draw->AddRectFilled(f.at(centre - label_size.x * 0.5f - 6.0f, row - 1.0f),
                                    f.at(centre + label_size.x * 0.5f + 6.0f, row + label_size.y + 1.0f),
                                    faded(IM_COL32(192, 192, 192, 64), alpha));
            text(draw, f, centre - label_size.x * 0.5f, row, kSize, white, label);
        };
        choice(d.text->yes, 302.0f, d.yes);
        choice(d.text->no, 366.0f, !d.yes);
    }
    draw->AddRectFilled(f.at(202.0f, top - 12.0f), f.at(466.0f, top - 11.0f), faded(IM_COL32_WHITE, alpha));
    draw->AddRectFilled(f.at(202.0f, top + 2.0f * half + 12.0f), f.at(466.0f, top + 2.0f * half + 13.0f),
                        faded(IM_COL32_WHITE, alpha));
}

// The PSP's circle and cross, drawn rather than taken from a font.
void draw_button(ImDrawList *draw, const Frame &f, float x, float centre_y, bool circle) {
    constexpr float kRadius = 4.6f;
    const ImVec2 centre = f.at(x + kRadius, centre_y);
    draw->AddCircleFilled(centre, f.size(kRadius + 0.8f), faded(IM_COL32(0, 0, 0, 120)));
    if (circle) {
        draw->AddCircle(centre, f.size(kRadius - 0.6f), faded(IM_COL32_WHITE), 0, f.size(1.3f));
    } else {
        const float r = f.size(kRadius - 1.3f);
        draw->AddLine({centre.x - r, centre.y - r}, {centre.x + r, centre.y + r}, faded(IM_COL32_WHITE), f.size(1.3f));
        draw->AddLine({centre.x - r, centre.y + r}, {centre.x + r, centre.y - r}, faded(IM_COL32_WHITE), f.size(1.3f));
    }
}

// Bottom right, as the PSP puts them: the button that confirms and the one
// that goes back, each with what it does.
void draw_hints(ImDrawList *draw, const Frame &f, bool with_back) {
    const Dialog &d = dialog();
    constexpr float kSize = 11.0f;
    constexpr float kButton = 9.2f;
    constexpr float kCentreY = 258.0f;
    struct Hint {
        bool circle;
        const char *label;
    };
    std::vector<Hint> hints{{!d.request.confirm_cross, d.text->ok}};
    if (with_back) hints.push_back({d.request.confirm_cross, d.text->back});
    float x = kScreenWidth - 10.0f;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        const ImVec2 size = measure(f, kSize, it->label);
        x -= size.x;
        text(draw, f, x, kCentreY - size.y * 0.5f, kSize, IM_COL32_WHITE, it->label);
        x -= kButton + 3.0f;
        draw_button(draw, f, x, kCentreY, it->circle);
        x -= 14.0f;
    }
}

// --- Flow ---------------------------------------------------------------

void finish(SavedataDialogOutcome outcome) {
    Dialog &d = dialog();
    if (!d.open) return;
    d.open = false;
    Layer &layer = Layer::get();
    layer.set_interactive(false);
    layer.renderer().set_game_input(true);
    layer.renderer().hold_frame(false);
    const std::size_t index = d.cursor;
    SavedataDialogDone done = std::move(d.done);
    d.done = nullptr;
    d.perform = nullptr;
    d.request = {};
    if (done) done(outcome, index);
}

// Fades out, then closes with `outcome`.
void close(SavedataDialogOutcome outcome) {
    Dialog &d = dialog();
    if (d.closing_at) return;
    d.closing_at = ImGui::GetTime();
    d.closing_outcome = outcome;
}

std::string working_text() {
    const Dialog &d = dialog();
    switch (d.request.kind) {
    case SavedataDialogKind::Save: return d.text->saving;
    case SavedataDialogKind::Load: return d.text->loading;
    case SavedataDialogKind::Delete: return d.text->erasing;
    }
    return "";
}

std::string result_text() {
    const Dialog &d = dialog();
    switch (d.request.kind) {
    case SavedataDialogKind::Save: return d.succeeded ? d.text->saved : d.text->save_failed;
    case SavedataDialogKind::Load: return d.succeeded ? d.text->loaded : d.text->load_failed;
    case SavedataDialogKind::Delete: return d.succeeded ? d.text->erased : d.text->erase_failed;
    }
    return "";
}

struct Input {
    bool up{}, down{}, left{}, right{}, confirm{}, back{};
};

Input read_input() {
    Layer &layer = Layer::get();
    Input in;
    const auto any = [](std::initializer_list<ImGuiKey> keys, bool repeat) {
        for (const ImGuiKey key : keys)
            if (ImGui::IsKeyPressed(key, repeat)) return true;
        return false;
    };
    in.up = any({ImGuiKey_UpArrow, ImGuiKey_W}, true);
    in.down = any({ImGuiKey_DownArrow, ImGuiKey_S}, true);
    in.left = any({ImGuiKey_LeftArrow, ImGuiKey_A}, true);
    in.right = any({ImGuiKey_RightArrow, ImGuiKey_D}, true);
    in.confirm = any({ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Space, ImGuiKey_Z}, false);
    in.back = layer.take_back() || any({ImGuiKey_Backspace, ImGuiKey_X}, false);
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel > 0.0f) in.up = true;
    if (wheel < 0.0f) in.down = true;
    // A button held when the dialog opened, such as the one that asked for
    // it, does nothing until it is released.
    if (layer.gamepad_armed()) {
        const bool south = layer.confirm_south();
        const ImGuiKey confirm = south ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight;
        const ImGuiKey back = south ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
        in.up = in.up || any({ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp}, true);
        in.down = in.down || any({ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown}, true);
        in.left = in.left || any({ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft}, true);
        in.right = in.right || any({ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight}, true);
        in.confirm = in.confirm || ImGui::IsKeyPressed(confirm, false);
        in.back = in.back || ImGui::IsKeyPressed(back, false);
    }
    return in;
}

void move_cursor(std::size_t to) {
    Dialog &d = dialog();
    if (to == d.cursor) return;
    d.cursor = to;
    d.cursor_moved_at = ImGui::GetTime();
    update_picture();
    play(Sound::Move);
}

// The player chose the focused entry on the list.
void choose() {
    Dialog &d = dialog();
    const SavedataDialogEntry &entry = d.request.entries[d.cursor];
    const bool ask = (d.request.kind == SavedataDialogKind::Save && entry.exists) ||
                     d.request.kind == SavedataDialogKind::Delete;
    d.yes = false;
    set_screen(ask ? Screen::Confirm : Screen::Working);
}

void handle_input() {
    Dialog &d = dialog();
    const Input in = read_input();
    if (d.closing_at) return;
    switch (d.screen) {
    case Screen::List:
        if (in.up && d.cursor > 0u) move_cursor(d.cursor - 1u);
        if (in.down && d.cursor + 1u < d.request.entries.size()) move_cursor(d.cursor + 1u);
        if (in.back) {
            play(Sound::Back);
            close(SavedataDialogOutcome::Cancelled);
        } else if (in.confirm) {
            play(Sound::Confirm);
            choose();
        }
        break;
    case Screen::Confirm:
        if (in.left && !d.yes) {
            d.yes = true;
            play(Sound::Move);
        }
        if (in.right && d.yes) {
            d.yes = false;
            play(Sound::Move);
        }
        if (in.back || (in.confirm && !d.yes)) {
            play(Sound::Back);
            set_screen(Screen::List);
        } else if (in.confirm) {
            play(Sound::Confirm);
            set_screen(Screen::Working);
        }
        break;
    case Screen::Working: break;
    case Screen::Result:
        if (in.confirm || in.back) {
            play(Sound::Confirm);
            if (d.succeeded) close(SavedataDialogOutcome::Done);
            else set_screen(Screen::List);  // so the player can try another
        }
        break;
    case Screen::NoData:
        if (in.confirm || in.back) {
            play(Sound::Confirm);
            close(SavedataDialogOutcome::NoData);
        }
        break;
    }
}

// The work, once "Saving..." has been on screen for a frame; the result
// after it has been up long enough to read.
void advance_work() {
    Dialog &d = dialog();
    if (d.screen != Screen::Working) return;
    ++d.working_frames;
    if (d.working_frames == 2) {
        d.succeeded = d.perform && d.perform(d.cursor);
        if (d.succeeded && d.request.kind != SavedataDialogKind::Load) {
            // What was written or removed is what the list shows now.
            SavedataDialogEntry &entry = d.request.entries[d.cursor];
            if (d.request.kind == SavedataDialogKind::Save) {
                const std::string name = entry.save_name;
                entry = d.request.new_data;
                entry.save_name = name;
                entry.exists = true;
                entry.modified = std::time(nullptr);
                if (d.cursor < d.has_icon.size() && d.cursor < kAtlasCells - 1u) {
                    d.has_icon[d.cursor] = put_icon(d.cursor, entry.icon0);
                    upload(textures().icons);
                }
                textures().picture_of = kNone;  // the entry's picture is the game's now
            } else {
                entry.exists = false;
                if (d.cursor < d.has_icon.size()) d.has_icon[d.cursor] = false;
            }
        }
    }
    if (d.working_frames >= kWorkingFrames) set_screen(Screen::Result);
}

// The column catches up with the cursor over a few frames.
void advance_scroll() {
    Dialog &d = dialog();
    const float target = static_cast<float>(d.cursor);
    const float step = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * 16.0f);
    d.scroll += (target - d.scroll) * step;
    if (std::fabs(target - d.scroll) < 0.002f) d.scroll = target;
}

} // namespace

bool open_game_savedata_dialog(SavedataDialogRequest request, SavedataPerform perform, SavedataDialogDone done) {
    Layer &layer = Layer::get();
    if (!layer.attached()) return false;
    Dialog &d = dialog();
    if (d.open) finish(SavedataDialogOutcome::Cancelled);
    layer.renderer().hold_frame(true);
    layer.renderer().set_game_input(false);
    layer.set_interactive(true);
    d = Dialog{};
    d.open = true;
    d.request = std::move(request);
    d.text = &strings_for(d.request.language);
    d.perform = std::move(perform);
    d.done = std::move(done);
    d.cursor = d.request.entries.empty() ? 0u : std::min(d.request.focus, d.request.entries.size() - 1u);
    d.scroll = static_cast<float>(d.cursor);
    d.opened_at = d.cursor_moved_at = d.screen_since = ImGui::GetTime();
    d.screen = d.request.entries.empty() ? Screen::NoData : Screen::List;
    textures().picture_of = kNone;
    return true;
}

bool savedata_dialog_open() { return dialog().open; }

void savedata_dialog_frame() {
    Dialog &d = dialog();
    if (!d.open) return;
    Layer &layer = Layer::get();
    // The menu does not open over the dialog.
    (void)layer.take_menu_toggle();
    if (d.has_icon.size() != d.request.entries.size()) {
        build_icons();
        update_picture();
    }
    handle_input();
    if (d.closing_at && ImGui::GetTime() - *d.closing_at >= kFade) {
        finish(d.closing_outcome);
        return;
    }
    advance_work();
    advance_scroll();

    g_alpha = d.closing_at ? 1.0f - since(*d.closing_at, kFade) : since(d.opened_at, kFade);
    const Frame f = screen_frame();
    ImDrawList *draw = ImGui::GetBackgroundDrawList();
    // The save's picture over the game's last frame, as the PSP shows it; a
    // game that gives none keeps its own screen behind, dimmed only by an
    // eighth so it stays readable.
    if (textures().picture != nullptr && textures().picture_valid)
        draw->AddImage(textures().picture->GetTexRef(), f.at(0.0f, 0.0f), f.at(kScreenWidth, kScreenHeight), {0, 0},
                       {1, 1}, faded(IM_COL32_WHITE));
    else
        draw->AddRectFilled({0.0f, 0.0f}, ImGui::GetIO().DisplaySize, faded(IM_COL32(0, 0, 0, 32)));

    switch (d.screen) {
    case Screen::List:
        draw_list(draw, f);
        draw_list_info(draw, f);
        draw_hints(draw, f, true);
        break;
    case Screen::Confirm:
        draw_entry_icon(draw, f, d.cursor, kFocusBox);
        draw_summary(draw, f);
        draw_message(draw, f,
                     d.request.kind == SavedataDialogKind::Delete ? d.text->erase_question : d.text->overwrite, true);
        draw_hints(draw, f, true);
        break;
    case Screen::Working:
        draw_entry_icon(draw, f, d.cursor, kFocusBox);
        draw_summary(draw, f);
        draw_message(draw, f, working_text(), false);
        break;
    case Screen::Result:
        draw_entry_icon(draw, f, d.cursor, kFocusBox);
        draw_summary(draw, f);
        draw_message(draw, f, result_text(), false);
        draw_hints(draw, f, false);
        break;
    case Screen::NoData:
        draw_message(draw, f, d.text->no_data, false);
        draw_hints(draw, f, false);
        break;
    }
    // The banner, over the column as it scrolls under it: what the dialog
    // is for.
    draw->AddRectFilledMultiColor(f.at(0.0f, 0.0f), f.at(kScreenWidth, kBannerHeight + 6.0f),
                                  faded(IM_COL32(0, 0, 0, 170)), faded(IM_COL32(0, 0, 0, 170)),
                                  faded(IM_COL32(0, 0, 0, 0)), faded(IM_COL32(0, 0, 0, 0)));
    text(draw, f, 12.0f, 6.0f, 13.0f, IM_COL32_WHITE, banner_text());
    draw->AddRectFilled(f.at(8.0f, 24.0f), f.at(160.0f, 25.0f), faded(IM_COL32(255, 255, 255, 160)));
}

} // namespace portablekit::ui
