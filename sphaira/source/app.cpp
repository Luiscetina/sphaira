#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/popup_list.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/error_box.hpp"

#include "ui/menus/main_menu.hpp"

#include "app.hpp"
#include "log.hpp"
#include "ui/nvg_util.hpp"
#include "nro.hpp"
#include "evman.hpp"
#include "owo.hpp"
#include "image.hpp"
#include "nxlink.h"
#include "fs.hpp"
#include "defines.hpp"
#include "i18n.hpp"
#include "ftpsrv_helper.hpp"
#include "haze_helper.hpp"
#include "web.hpp"
#include "swkbd.hpp"
#include "fatfs.hpp"
#include "usbdvd.hpp"

#include "utils/profile.hpp"
#include "utils/thread.hpp"

#include <nanovg_dk.h>
#include <minIni.h>
#include <algorithm>
#include <ranges>
#include <cassert>
#include <cstring>
#include <ctime>
#include <span>
#include <dirent.h>
#include <usbhsfs.h>

extern "C" {
    u32 __nx_applet_exit_mode = 0;
} // extern "C"

namespace sphaira {
namespace {

constexpr const u8 DEFAULT_IMAGE_DATA[]{
    #embed <icons/default.png>
};

struct ThemeData {
    fs::FsPath music_path{};
    std::string elements[ThemeEntryID_MAX]{};
};

struct ThemeIdPair {
    const char* label;
    ThemeEntryID id;
    ElementType type{ElementType::None};
};

struct FrameBufferSize {
    Vec2 size;
    Vec2 scale;
};

struct NszOption {
    int value;
    const char* name;
};

constexpr NszOption NSZ_COMPRESS_LEVEL_OPTIONS[] = {
    { .value = 0, .name = "Level 0 (no compression)" },
    { .value = 1, .name = "Level 1" },
    { .value = 2, .name = "Level 2" },
    { .value = 3, .name = "Level 3 (default)" },
    { .value = 4, .name = "Level 4" },
    { .value = 5, .name = "Level 5" },
    { .value = 6, .name = "Level 6" },
    { .value = 7, .name = "Level 7" },
    { .value = 8, .name = "Level 8" },
};

constexpr NszOption NSZ_COMPRESS_THREAD_OPTIONS[] = {
    { .value = 0, .name = "0 (single threaded)" },
    { .value = 1, .name = "1" },
    { .value = 2, .name = "2" },
    { .value = 3, .name = "3 (default)" },
    { .value = 4, .name = "4" },
};

constexpr NszOption NSZ_COMPRESS_BLOCK_OPTIONS[] = {
    { .value = 14, .name = "16 KB" },
    { .value = 15, .name = "32 KB" },
    { .value = 16, .name = "64 KB" },
    { .value = 17, .name = "128 KB" },
    { .value = 18, .name = "256 KB" },
    { .value = 19, .name = "512 KB" },
    { .value = 20, .name = "1 MB (default)" },
    { .value = 21, .name = "2 MB" },
    { .value = 22, .name = "4 MB" },
    { .value = 23, .name = "8 MB" },
    { .value = 24, .name = "16 MB" },
};

constexpr ThemeIdPair THEME_ENTRIES[] = {
    { "background", ThemeEntryID_BACKGROUND },
    { "grid", ThemeEntryID_GRID },
    { "text", ThemeEntryID_TEXT, ElementType::Colour },
    { "text_info", ThemeEntryID_TEXT_INFO, ElementType::Colour },
    { "text_selected", ThemeEntryID_TEXT_SELECTED, ElementType::Colour },
    { "selected_background", ThemeEntryID_SELECTED_BACKGROUND, ElementType::Colour },
    { "error", ThemeEntryID_ERROR, ElementType::Colour },
    { "popup", ThemeEntryID_POPUP, ElementType::Colour },
    { "focus", ThemeEntryID_FOCUS, ElementType::Colour },
    { "line", ThemeEntryID_LINE, ElementType::Colour },
    { "line_separator", ThemeEntryID_LINE_SEPARATOR, ElementType::Colour },
    { "sidebar", ThemeEntryID_SIDEBAR, ElementType::Colour },
    { "scrollbar", ThemeEntryID_SCROLLBAR, ElementType::Colour },
    { "scrollbar_background", ThemeEntryID_SCROLLBAR_BACKGROUND, ElementType::Colour },
    { "progressbar", ThemeEntryID_PROGRESSBAR, ElementType::Colour },
    { "progressbar_background", ThemeEntryID_PROGRESSBAR_BACKGROUND, ElementType::Colour },
    { "highlight_1", ThemeEntryID_HIGHLIGHT_1, ElementType::Colour },
    { "highlight_2", ThemeEntryID_HIGHLIGHT_2, ElementType::Colour },
    { "icon_colour", ThemeEntryID_ICON_COLOUR, ElementType::Colour },
    { "icon_audio", ThemeEntryID_ICON_AUDIO, ElementType::Texture },
    { "icon_video", ThemeEntryID_ICON_VIDEO, ElementType::Texture },
    { "icon_image", ThemeEntryID_ICON_IMAGE, ElementType::Texture },
    { "icon_file", ThemeEntryID_ICON_FILE, ElementType::Texture },
    { "icon_folder", ThemeEntryID_ICON_FOLDER, ElementType::Texture },
    { "icon_zip", ThemeEntryID_ICON_ZIP, ElementType::Texture },
    { "icon_nro", ThemeEntryID_ICON_NRO, ElementType::Texture },
};

constinit App* g_app{};

void deko3d_error_cb(void* userData, const char* context, DkResult result, const char* message) {
    switch (result) {
        case DkResult_Success:
            break;

        case DkResult_Fail:
            log_write("[DkResult_Fail] %s\n", message);
            App::Notify("DkResult_Fail");
            break;

        case DkResult_Timeout:
            log_write("[DkResult_Timeout] %s\n", message);
            App::Notify("DkResult_Timeout");
            break;

        case DkResult_OutOfMemory:
            log_write("[DkResult_OutOfMemory] %s\n", message);
            App::Notify("DkResult_OutOfMemory");
            break;

        case DkResult_NotImplemented:
            log_write("[DkResult_NotImplemented] %s\n", message);
            App::Notify("DkResult_NotImplemented");
            break;

        case DkResult_MisalignedSize:
            log_write("[DkResult_MisalignedSize] %s\n", message);
            App::Notify("DkResult_MisalignedSize");
            break;

        case DkResult_MisalignedData:
            log_write("[DkResult_MisalignedData] %s\n", message);
            App::Notify("DkResult_MisalignedData");
            break;

        case DkResult_BadInput:
            log_write("[DkResult_BadInput] %s\n", message);
            App::Notify("DkResult_BadInput");
            break;

        case DkResult_BadFlags:
            log_write("[DkResult_BadFlags] %s\n", message);
            App::Notify("DkResult_BadFlags");
            break;

        case DkResult_BadState:
            log_write("[DkResult_BadState] %s\n", message);
            App::Notify("DkResult_BadState");
            break;
    }
}

void on_applet_focus_state(App* app) {
    switch (appletGetFocusState()) {
        case AppletFocusState_InFocus:
            log_write("[APPLET] AppletFocusState_InFocus\n");
            // App::Notify("AppletFocusState_InFocus");
            break;

        case AppletFocusState_OutOfFocus:
            log_write("[APPLET] AppletFocusState_OutOfFocus\n");
            // App::Notify("AppletFocusState_OutOfFocus");
            break;

        case AppletFocusState_Background:
            log_write("[APPLET] AppletFocusState_Background\n");
            // App::Notify("AppletFocusState_Background");
            break;
    }
}

void on_applet_operation_mode(App* app) {
    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            log_write("[APPLET] AppletOperationMode_Handheld\n");
            App::Notify("Switch-Handheld!"_i18n);
            break;

        case AppletOperationMode_Console:
            log_write("[APPLET] AppletOperationMode_Console\n");
            App::Notify("Switch-Docked!"_i18n);
            break;
    }
}

void applet_on_performance_mode(App* app) {
    switch (appletGetPerformanceMode()) {
        case ApmPerformanceMode_Invalid:
            log_write("[APPLET] ApmPerformanceMode_Invalid\n");
            App::Notify("ApmPerformanceMode_Invalid");
            break;

        case ApmPerformanceMode_Normal:
            log_write("[APPLET] ApmPerformanceMode_Normal\n");
            App::Notify("ApmPerformanceMode_Normal");
            break;

        case ApmPerformanceMode_Boost:
            log_write("[APPLET] ApmPerformanceMode_Boost\n");
            App::Notify("ApmPerformanceMode_Boost");
            break;
    }
}

void appplet_hook_calback(AppletHookType type, void *param) {
    auto app = static_cast<App*>(param);
    switch (type) {
        case AppletHookType_OnFocusState:
            log_write("[APPLET] AppletHookType_OnFocusState\n");
            // App::Notify("AppletHookType_OnFocusState");
            on_applet_focus_state(app);
            break;

        case AppletHookType_OnOperationMode:
            log_write("[APPLET] AppletHookType_OnOperationMode\n");
            // App::Notify("AppletHookType_OnOperationMode");
            on_applet_operation_mode(app);
            break;

        case AppletHookType_OnPerformanceMode:
            log_write("[APPLET] AppletHookType_OnPerformanceMode\n");
            // App::Notify("AppletHookType_OnPerformanceMode");
            applet_on_performance_mode(app);
            break;

        case AppletHookType_OnExitRequest:
            log_write("[APPLET] AppletHookType_OnExitRequest\n");
            // App::Notify("AppletHookType_OnExitRequest");
            break;

        case AppletHookType_OnResume:
            log_write("[APPLET] AppletHookType_OnResume\n");
            // App::Notify("AppletHookType_OnResume");
            break;

        case AppletHookType_OnCaptureButtonShortPressed:
            log_write("[APPLET] AppletHookType_OnCaptureButtonShortPressed\n");
            // App::Notify("AppletHookType_OnCaptureButtonShortPressed");
            break;

        case AppletHookType_OnAlbumScreenShotTaken:
            log_write("[APPLET] AppletHookType_OnAlbumScreenShotTaken\n");
            // App::Notify("AppletHookType_OnAlbumScreenShotTaken");
            break;

        case AppletHookType_RequestToDisplay:
            log_write("[APPLET] AppletHookType_RequestToDisplay\n");
            // App::Notify("AppletHookType_RequestToDisplay");
            break;

        case AppletHookType_Max:
            assert(!"AppletHookType_Max hit");
            break;
    }
}

auto GetFrameBufferSize() -> FrameBufferSize {
    FrameBufferSize fb{};

    switch (appletGetOperationMode()) {
        case AppletOperationMode_Handheld:
            fb.size.x = 1280;
            fb.size.y = 720;
            break;

        case AppletOperationMode_Console:
            fb.size.x = 1920;
            fb.size.y = 1080;
            break;
    }

    fb.scale.x = fb.size.x / SCREEN_WIDTH;
    fb.scale.y = fb.size.y / SCREEN_HEIGHT;
    return fb;
}

// this will try to decompress the icon and then re-convert it to jpg
// in order to strip exif data.
// this doesn't take long at all, but it's very overkill.
// todo: look into jpeg/exif spec to manually strip data
auto GetNroIcon(const std::vector<u8>& nro_icon) -> std::vector<u8> {
    auto image = ImageLoadFromMemory(nro_icon);
    if (!image.data.empty()) {
        if (image.w != 256 || image.h != 256) {
            image = ImageResize(image.data, image.w, image.h, 256, 256);
        }
        if (!image.data.empty()) {
            image = ImageConvertToJpg(image.data, image.w, image.h);
            if (!image.data.empty()) {
                return image.data;
            }
        }
    }
    return nro_icon;
}

auto LoadThemeMeta(const fs::FsPath& path, ThemeMeta& meta) -> bool {
    meta = {};

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto meta = static_cast<ThemeMeta*>(UserData);

        if (!std::strcmp(Section, "meta")) {
            if (!std::strcmp(Key, "name")) {
                meta->name = Value;
            } else if (!std::strcmp(Key, "author")) {
                meta->author = Value;
            } else if (!std::strcmp(Key, "version")) {
                meta->version = Value;
            } else if (!std::strcmp(Key, "inherit")) {
                meta->inherit = Value;
            }

            return 1;
        }

        return 0;
    };

    if (!ini_browse(cb, &meta, path)) {
        return false;
    }

    if (meta.name.empty() || meta.author.empty() || meta.version.empty()) {
        return false;
    }

    log_write("loaded meta from: %s\n", path.s);
    meta.ini_path = path;
    return true;
}

void LoadThemeInternal(ThemeMeta meta, ThemeData& theme_data, int inherit_level = 0) {
    constexpr auto inherit_level_max = 5;

    // all themes will inherit from black theme by default.
    if (meta.inherit.empty() && !inherit_level) {
        meta.inherit = "romfs:/themes/base_black_theme.ini";
    }

    // check if the theme inherits from another, if so, load it.
    // block inheriting from itself.
    if (inherit_level < inherit_level_max && !meta.inherit.empty() && strcasecmp(meta.inherit, "none") && meta.inherit != meta.ini_path) {
        log_write("inherit is not empty: %s\n", meta.inherit.s);
        if (R_SUCCEEDED(romfsInit())) {
            ThemeMeta inherit_meta;
            const auto has_meta = LoadThemeMeta(meta.inherit, inherit_meta);
            romfsExit();

            // base themes do not have a meta
            if (!has_meta) {
                inherit_meta.ini_path = meta.inherit;
            }

            LoadThemeInternal(inherit_meta, theme_data, inherit_level + 1);
        }
    }

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto theme_data = static_cast<ThemeData*>(UserData);

        if (!std::strcmp(Section, "theme")) {
            if (!std::strcmp(Key, "music")) {
                theme_data->music_path = Value;
            } else {
                for (auto& e : THEME_ENTRIES) {
                    if (!std::strcmp(Key, e.label)) {
                        theme_data->elements[e.id] = Value;
                        break;
                    }
                }
            }
        }

        return 1;
    };

    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());

        if (!ini_browse(cb, &theme_data, meta.ini_path)) {
            log_write("failed to open ini: %s\n", meta.ini_path.s);
        } else {
            log_write("opened ini: %s\n", meta.ini_path.s);
        }
    }
}

void nxlink_callback(const NxlinkCallbackData *data) {
    App::NotifyFlashLed();
    evman::push(*data, false);
}

void on_i18n_change() {
    i18n::exit();
    i18n::init(App::GetLanguage());
}

} // namespace

void App::Loop() {
    // adjust these if FPSlocker ever supports different min/max fps.
    constexpr double min_delta    = 1000.0 / 120.0; // 120 fps
    constexpr double max_delta    = 1000.0 / 15.0;  // 15  fps
    constexpr double target_delta = 1000.0 / 60.0;  // 60  fps

    u64 start = armTicksToNs(armGetSystemTick());
    m_delta_time = 1.0;

    while (!m_quit && appletMainLoop()) {
        if (m_widgets.empty()) {
            m_quit = true;
            break;
        }

        ui::gfx::updateHighlightAnimation();

        // fire all events in in a 3ms timeslice
        TimeStamp ts_event;
        const u64 event_timeout = 3;

        // limit events to a max per frame in order to not block for too long.
        while (true) {
            if (ts_event.GetMs() >= event_timeout) {
                log_write("event loop timed-out\n");
                break;
            }

            auto event = evman::pop();
            if (!event.has_value()) {
                break;
            }

            std::visit([this](auto&& arg){
                using T = std::decay_t<decltype(arg)>;
                if constexpr(std::is_same_v<T, evman::LaunchNroEventData>) {
                    log_write("[LaunchNroEventData] got event\n");
                    u64 timestamp = 0;
                    timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);
                    const auto nro_path = nro_normalise_path(arg.path);

                    // update timestamp
                    ini_putl(nro_path.c_str(), "timestamp", timestamp, App::PLAYLOG_PATH);
                    log_write("updating timestamp for: %s %lu\n", nro_path.c_str(), timestamp);

                    // force disable pop-back to main menu.
                    __nx_applet_exit_mode = 0;
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, evman::ExitEventData>) {
                    log_write("[ExitEventData] got event\n");
                    m_quit = true;
                } else if constexpr(std::is_same_v<T, NxlinkCallbackData>) {
                    switch (arg.type) {
                        case NxlinkCallbackType_Connected:
                            log_write("[NxlinkCallbackType_Connected]\n");
                            App::Notify("Nxlink Connected"_i18n);
                            break;
                        case NxlinkCallbackType_WriteBegin:
                            log_write("[NxlinkCallbackType_WriteBegin] %s\n", arg.file.filename);
                            App::Notify("Nxlink Upload"_i18n);
                            break;
                        case NxlinkCallbackType_WriteProgress:
                            // log_write("[NxlinkCallbackType_WriteProgress]\n");
                            break;
                        case NxlinkCallbackType_WriteEnd:
                            log_write("[NxlinkCallbackType_WriteEnd] %s\n", arg.file.filename);
                            App::Notify("Nxlink Finished"_i18n);
                            break;
                    }
                } else if constexpr(std::is_same_v<T, curl::DownloadEventData>) {
                    log_write("[DownloadEventData] got event\n");
                    if (arg.callback && !arg.stoken.stop_requested()) {
                        arg.callback(arg.result);
                    }
                } else {
                    static_assert(false, "non-exhaustive visitor!");
                }
            }, event.value());
        }

        const auto fb = GetFrameBufferSize();
        if (fb.size.x != s_width || fb.size.y != s_height) {
            s_width = fb.size.x;
            s_height = fb.size.y;
            m_scale = fb.scale;
            this->destroyFramebufferResources();
            this->createFramebufferResources();
            renderer->UpdateViewSize(s_width, s_height);
        }

        this->Poll();
        this->Update();
        this->Draw();

        // check how long this frame took.
        const u64 now = armTicksToNs(armGetSystemTick());
        // convert to ns.
        const double delta = (double)(now - start) / 1e+6;
        // clamp and normalise to 1.0 as the target, higher values if we took too long.
        m_delta_time = std::clamp(delta, min_delta, max_delta) / target_delta;
        // save timestamp for next frame.
        start = now;
    }
}

auto App::Push(std::unique_ptr<ui::Widget>&& widget) -> void {
    log_write("[Mui] pushing widget\n");

    // check if the widget wants to pop before adding.
    // this can happen if something failed in the constructor and the widget wants to exit.
    if (widget->ShouldPop()) {
        return;
    }

    if (!g_app->m_widgets.empty()) {
        g_app->m_widgets.back()->OnFocusLost();
    }

    log_write("doing focus gained\n");
    g_app->m_widgets.emplace_back(std::forward<decltype(widget)>(widget))->OnFocusGained();
    log_write("did it\n");
}

auto App::PopToMenu() -> void {
    for (auto& p : std::ranges::views::reverse(g_app->m_widgets)) {
        if (p->IsMenu()) {
            break;
        }

        p->SetPop();
    }
}

void App::Notify(const std::string& text, ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Push({text, side});
}

void App::Notify(ui::NotifEntry entry) {
    g_app->m_notif_manager.Push(entry);
}

void App::NotifyPop(ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Pop(side);
}

void App::NotifyClear(ui::NotifEntry::Side side) {
    g_app->m_notif_manager.Clear(side);
}

void App::NotifyFlashLed() {
    static constexpr HidsysNotificationLedPattern pattern = {
        .baseMiniCycleDuration = 0x1,             // 12.5ms.
        .totalMiniCycles = 0x1,                   // 1 mini cycle(s).
        .totalFullCycles = 0x1,                   // 1 full run(s).
        .startIntensity = 0xF,                    // 100%.
        .miniCycles = {{
            .ledIntensity = 0xF,                  // 100%.
            .transitionSteps = 0xF,               // 1 step(s). Total 12.5ms.
            .finalStepDuration = 0xF,             // Forced 12.5ms.
        }}
    };

    Result rc;
    s32 total;
    HidsysUniquePadId unique_pad_id;

    rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_Handheld, &unique_pad_id, 1, &total);
    if (R_SUCCEEDED(rc) && total) {
        rc = hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
    }

    if (R_FAILED(rc) || !total) {
        rc = hidsysGetUniquePadsFromNpad(HidNpadIdType_No1, &unique_pad_id, 1, &total);
        if (R_SUCCEEDED(rc) && total) {
            hidsysSetNotificationLedPattern(&pattern, unique_pad_id);
        }
    }
}

Result App::PushErrorBox(Result rc, const std::string& message) {
    if (R_FAILED(rc)) {
        App::Push<ui::ErrorBox>(rc, message);
    }
    return rc;
}

auto App::GetThemeMetaList() -> std::span<ThemeMeta> {
    return g_app->m_theme_meta_entries;
}

void App::SetTheme(s64 theme_index) {
    g_app->LoadTheme(g_app->m_theme_meta_entries[theme_index]);
    g_app->m_theme_index = theme_index;
}

auto App::GetThemeIndex() -> s64 {
    return g_app->m_theme_index;
}

auto App::GetDefaultImage() -> int {
    return g_app->m_default_image;
}

auto App::GetDefaultImageData() -> std::span<const u8> {
    return DEFAULT_IMAGE_DATA;
}

auto App::GetExePath() -> fs::FsPath {
    return g_app->m_app_path;
}

auto App::IsHbmenu() -> bool {
    return !strcasecmp(GetExePath().s, "/hbmenu.nro");
}

auto App::GetNxlinkEnable() -> bool {
    return g_app->m_nxlink_enabled.Get();
}

auto App::GetHddEnable() -> bool {
    return g_app->m_hdd_enabled.Get();
}

auto App::GetWriteProtect() -> bool {
    return g_app->m_hdd_write_protect.Get();
}

auto App::GetLogEnable() -> bool {
    return g_app->m_log_enabled.Get();
}

auto App::GetReplaceHbmenuEnable() -> bool {
    return g_app->m_replace_hbmenu.Get();
}

auto App::GetInstallEnable() -> bool {
    if (IsEmummc()) {
        return GetInstallEmummcEnable();
    } else {
        return GetInstallSysmmcEnable();
    }
}

auto App::GetInstallSysmmcEnable() -> bool {
    return g_app->m_install_sysmmc.GetOr("install");
}

auto App::GetInstallEmummcEnable() -> bool {
    return g_app->m_install_emummc.GetOr("install");
}

auto App::GetInstallSdEnable() -> bool {
    return g_app->m_install_sd.Get();
}

auto App::GetThemeMusicEnable() -> bool {
    return g_app->m_theme_music.Get();
}

auto App::GetMtpEnable() -> bool {
    return g_app->m_mtp_enabled.Get();
}

auto App::GetFtpEnable() -> bool {
    return g_app->m_ftp_enabled.Get();
}

auto App::GetLanguage() -> long {
    return g_app->m_language.Get();
}

auto App::GetTextScrollSpeed() -> long {
    return g_app->m_text_scroll_speed.Get();
}

auto App::Get12HourTimeEnable() -> bool {
    return g_app->m_12hour_time.Get();
}

auto App::GetNszCompressLevel() -> u8 {
    return NSZ_COMPRESS_LEVEL_OPTIONS[App::GetApp()->m_nsz_compress_level.Get()].value;
}

auto App::GetNszThreadCount() -> u8 {
    return NSZ_COMPRESS_THREAD_OPTIONS[App::GetApp()->m_nsz_compress_threads.Get()].value;
}

auto App::GetNszBlockExponent() -> u8 {
    return NSZ_COMPRESS_BLOCK_OPTIONS[App::GetApp()->m_nsz_compress_block_exponent.Get()].value;
}

void App::SetNxlinkEnable(bool enable) {
    if (App::GetNxlinkEnable() != enable) {
        g_app->m_nxlink_enabled.Set(enable);
        if (enable) {
            nxlinkInitialize(nxlink_callback);
        } else {
            nxlinkExit();
        }
    }
}

void App::SetHddEnable(bool enable) {
    if (App::GetHddEnable() != enable) {
        g_app->m_hdd_enabled.Set(enable);
        if (enable) {
            if (App::GetWriteProtect()) {
                usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
            }
            usbHsFsInitialize(1);
        } else {
            usbHsFsExit();
        }
    }
}

void App::SetWriteProtect(bool enable) {
    if (App::GetWriteProtect() != enable) {
        g_app->m_hdd_write_protect.Set(enable);

        if (enable) {
            usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
        } else {
            usbHsFsSetFileSystemMountFlags(0);
        }
    }
}

void App::SetLogEnable(bool enable) {
    if (App::GetLogEnable() != enable) {
        g_app->m_log_enabled.Set(enable);
        if (enable) {
            log_file_init();
        } else {
            log_file_exit();
        }
    }
}

void App::SetReplaceHbmenuEnable(bool enable) {
    if (App::GetReplaceHbmenuEnable() != enable) {
        g_app->m_replace_hbmenu.Set(enable);
        if (!enable) {
            // check we have already replaced hbmenu with sphaira
            NacpStruct hbmenu_nacp{};
            if (R_SUCCEEDED(nro_get_nacp("/hbmenu.nro", hbmenu_nacp))) {
                if (std::strcmp(hbmenu_nacp.lang[0].name, "sphaira")) {
                    return;
                }
            }

            // ask user if they want to restore hbmenu
            App::Push<ui::OptionBox>(
                "Restore hbmenu?"_i18n,
                "Back"_i18n, "Restore"_i18n, 1, [hbmenu_nacp](auto op_index){
                    if (!op_index || *op_index == 0) {
                        return;
                    }

                    NacpStruct actual_hbmenu_nacp;
                    if (R_FAILED(nro_get_nacp("/switch/hbmenu.nro", actual_hbmenu_nacp))) {
                        App::Push<ui::OptionBox>(
                            "Failed to find /switch/hbmenu.nro\n"
                            "Use the Appstore to re-install hbmenu"_i18n,
                            "OK"_i18n
                        );
                        return;
                    }

                    // NOTE: do NOT use rename anywhere here as it's possible
                    // to have a race condition with another app that opens hbmenu as a file
                    // in between the delete + rename.
                    // this would require a sys-module to open hbmenu.nro, such as an ftp server.
                    // a copy means that it opens the file handle, if successfull, then
                    // the full read/write will succeed.
                    NacpStruct sphaira_nacp;
                    fs::FsPath sphaira_path = "/switch/sphaira/sphaira.nro";
                    Result rc;

                    // first, try and backup sphaira, its not super important if this fails.
                    rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    if (R_FAILED(rc) || std::strcmp(sphaira_nacp.lang[0].name, "sphaira")) {
                        sphaira_path = "/switch/sphaira.nro";
                        rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    }

                    if (R_SUCCEEDED(rc) && !std::strcmp(sphaira_nacp.lang[0].name, "sphaira")) {
                        if (IsVersionNewer(sphaira_nacp.display_version, hbmenu_nacp.display_version)) {
                            if (R_FAILED(rc = g_app->m_fs->copy_entire_file(sphaira_path, "/hbmenu.nro"))) {
                                log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", sphaira_path.s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
                            } else {
                                log_write("success with updating hbmenu!\n");
                            }
                        }
                    } else {
                        // sphaira doesn't yet exist, create a new file.
                        sphaira_path = "/switch/sphaira/sphaira.nro";
                        g_app->m_fs->CreateDirectoryRecursively("/switch/sphaira/");
                        g_app->m_fs->copy_entire_file(sphaira_path, "/hbmenu.nro");
                    }

                    // this should never fail, if it does, well then the sd card is fucked.
                    if (R_FAILED(rc = g_app->m_fs->copy_entire_file("/hbmenu.nro", "/switch/hbmenu.nro")))  {
                        // try and restore sphaira in a last ditch effort.
                        if (R_FAILED(rc = g_app->m_fs->copy_entire_file("/hbmenu.nro", sphaira_path))) {
                            App::PushErrorBox(rc, "Failed to, TODO: add message here"_i18n);
                            App::PushErrorBox(rc,
                                "Failed to restore hbmenu, please re-download hbmenu"_i18n
                            );
                        } else {
                            App::Push<ui::OptionBox>(
                                "Failed to restore hbmenu, using sphaira instead"_i18n,
                                "OK"_i18n
                            );
                        }
                        return;
                    }

                    // don't need this any more.
                    g_app->m_fs->DeleteFile("/switch/hbmenu.nro");

                    // if we were hbmenu, exit now (as romfs is gone).
                    if (IsHbmenu()) {
                        App::Push<ui::OptionBox>(
                            "Restored hbmenu, closing sphaira"_i18n,
                            "OK"_i18n, [](auto) {
                                App::Exit();
                            }
                        );
                    } else {
                        App::Notify("Restored hbmenu"_i18n);
                    }
                }
            );
        }
    }
}

void App::SetInstallSysmmcEnable(bool enable) {
    g_app->m_install_sysmmc.Set(enable);
}

void App::SetInstallEmummcEnable(bool enable) {
    g_app->m_install_emummc.Set(enable);
}

void App::SetInstallSdEnable(bool enable) {
    g_app->m_install_sd.Set(enable);
}

void App::SetThemeMusicEnable(bool enable) {
    if (App::GetThemeMusicEnable() != enable) {
        g_app->m_theme_music.Set(enable);
        if (enable) {
            g_app->LoadAndPlayThemeMusic();
        } else {
            g_app->CloseThemeBackgroundMusic();
        }
    }
}

void App::Set12HourTimeEnable(bool enable) {
    g_app->m_12hour_time.Set(enable);
}

void App::SetMtpEnable(bool enable) {
    if (App::GetMtpEnable() != enable) {
        g_app->m_mtp_enabled.Set(enable);
        if (enable) {
            haze::Init();
        } else {
            haze::Exit();
        }
    }
}

void App::SetFtpEnable(bool enable) {
    if (App::GetFtpEnable() != enable) {
        g_app->m_ftp_enabled.Set(enable);
        if (enable) {
            ftpsrv::Init();
        } else {
            ftpsrv::Exit();
        }
    }
}

void App::SetLanguage(long index) {
    if (App::GetLanguage() != index) {
        g_app->m_language.Set(index);
        on_i18n_change();

        App::Push<ui::OptionBox>(
            "Restart Sphaira?"_i18n,
            "Back"_i18n, "Restart"_i18n, 1, [](auto op_index){
                if (op_index && *op_index) {
                    App::ExitRestart();
                }
            }
        );
    }
}

void App::SetTextScrollSpeed(long index) {
    g_app->m_text_scroll_speed.Set(index);
}

auto App::Install(OwoConfig& config) -> Result {
    App::Push<ui::ProgressBox>(0, "Installing Forwarder"_i18n, config.name, [config](auto pbox) mutable -> Result {
        return Install(pbox, config);
    }, [](Result rc){
        App::PushErrorBox(rc, "Failed to install forwarder"_i18n);

        if (R_SUCCEEDED(rc)) {
            App::PlaySoundEffect(SoundEffect::Install);
            App::Notify("Installed!"_i18n);
        }
    });

    R_SUCCEED();
}

auto App::Install(ui::ProgressBox* pbox, OwoConfig& config) -> Result {
    config.nro_path = nro_add_arg_file(config.nro_path);
    if (!config.icon.empty()) {
        config.icon = GetNroIcon(config.icon);
    }

    if (config.logo.empty()) {
        g_app->m_fs->read_entire_file("/config/sphaira/logo/NintendoLogo.png", config.logo);
    }

    if (config.gif.empty()) {
        g_app->m_fs->read_entire_file("/config/sphaira/logo/StartupMovie.gif", config.gif);
    }

    return install_forwarder(pbox, config, GetInstallSdEnable() ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser);
}

auto App::IsEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') || (paths.nintendo[0] != '\0');
}

auto App::IsParitionBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] == '\0') && (paths.nintendo[0] != '\0');
}

auto App::IsFileBaseEmummc() -> bool {
    const auto& paths = g_app->m_emummc_paths;
    return (paths.file_based_path[0] != '\0') && (paths.nintendo[0] != '\0');
}

void App::Exit() {
    g_app->m_quit = true;
}

void App::ExitRestart() {
    nro_launch(GetExePath());
    Exit();
}

void App::Poll() {
    m_controller.Reset();

    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
    m_touch_info.is_clicked = false;

// todo: replace old touch code with gestures from below
#if 0
    static HidGestureState prev_gestures[17]{};
    HidGestureState gestures[17]{};
    const auto gesture_count = hidGetGestureStates(gestures, std::size(gestures));
    for (int i = (int)gesture_count - 1; i >= 0; i--) {
        bool found = false;
        for (int j = 0; j < gesture_count; j++) {
            if (gestures[i].type == prev_gestures[j].type && gestures[i].sampling_number == prev_gestures[j].sampling_number) {
                found = true;
                break;
            }
        }

        if (found) {
            continue;
        }

        auto gesture = gestures[i];
        if (gesture_count && gesture.type == HidGestureType_Touch) {
            log_write("[TOUCH] got gesture attr: %u direction: %u sampling_number: %zu context_number: %zu\n", gesture.attributes, gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Swipe) {
            log_write("[SWIPE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Tap) {
            log_write("[TAP] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Press) {
            log_write("[PRESS] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Cancel) {
            log_write("[CANCEL] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Complete) {
            log_write("[COMPLETE] got gesture direction: %u sampling_number: %zu context_number: %zu\n", gesture.direction, gesture.sampling_number, gesture.context_number);
        }
        else if (gesture_count && gesture.type == HidGestureType_Pan) {
            log_write("[PAN] got gesture direction: %u sampling_number: %zu context_number: %zu x: %d y: %d dx: %d dy: %d vx: %.2f vy: %.2f count: %d\n", gesture.direction, gesture.sampling_number, gesture.context_number, gesture.x, gesture.y, gesture.delta_x, gesture.delta_y, gesture.velocity_x, gesture.velocity_y, gesture.point_count);
        }
    }

    memcpy(prev_gestures, gestures, sizeof(gestures));
#endif

    if (state.count == 1 && !m_touch_info.is_touching) {
        m_touch_info.initial = m_touch_info.cur = state.touches[0];
        m_touch_info.is_touching = true;
        m_touch_info.is_tap = true;
    } else if (state.count >= 1 && m_touch_info.is_touching) {
        m_touch_info.cur = state.touches[0];

        if (m_touch_info.is_tap &&
            (std::abs((s32)m_touch_info.initial.x - (s32)m_touch_info.cur.x) > 20 ||
            std::abs((s32)m_touch_info.initial.y - (s32)m_touch_info.cur.y) > 20)) {
            m_touch_info.is_tap = false;
            m_touch_info.is_scroll = true;
        }
    } else if (m_touch_info.is_touching) {
        m_touch_info.is_touching = false;
        m_touch_info.is_scroll = false;
        if (m_touch_info.is_tap) {
            m_touch_info.is_clicked = true;
        } else {
            m_touch_info.is_end = true;
        }
    }

    // todo: better implement this to match hos
    if (!m_touch_info.is_touching && !m_touch_info.is_clicked) {
        padUpdate(&m_pad);
        m_controller.m_kdown = padGetButtonsDown(&m_pad);
        m_controller.m_kheld = padGetButtons(&m_pad);
        m_controller.m_kup = padGetButtonsUp(&m_pad);
        m_controller.UpdateButtonHeld(static_cast<u64>(Button::ANY_DIRECTION), m_delta_time);
    }
}

void App::Update() {
    // loop background music if it has finished.
    audio::State song_state;
    if (R_SUCCEEDED(audio::GetProgress(m_background_music, nullptr, &song_state))) {
        if (song_state == audio::State::Finished) {
            audio::SeekSong(m_background_music, 0);
        }
    }

    m_widgets.back()->Update(&m_controller, &m_touch_info);

    bool popped_at_least1 = false;
    while (true) {
        if (m_widgets.empty()) {
            log_write("[Mui] no widgets left, so we exit...");
            App::Exit();
            return;
        }

        if (m_widgets.back()->ShouldPop()) {
            log_write("popping widget\n");
            m_widgets.pop_back();
            popped_at_least1 = true;
        } else {
            break;
        }
    }

    if (!m_widgets.empty() && popped_at_least1) {
        m_widgets.back()->OnFocusGained();
    }
}

void App::Draw() {
    const auto slot = this->queue.acquireImage(this->swapchain);
    this->queue.submitCommands(this->framebuffer_cmdlists[slot]);
    this->queue.submitCommands(this->render_cmdlist);
    nvgBeginFrame(this->vg, s_width, s_height, 1.f);
    nvgScale(vg, m_scale.x, m_scale.y);

    // find the last menu in the list, start drawing from there
    auto menu_it = m_widgets.rend();
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); it++) {
        const auto& p = *it;
        if (!p->IsHidden() && p->IsMenu()) {
            menu_it = it;
            break;
        }
    }

    // reverse itr so loop backwards to go forwarders.
    if (menu_it != m_widgets.rend()) {
        for (auto it = menu_it; ; it--) {
            const auto& p = *it;

            // draw everything not hidden on top of the menu.
            if (!p->IsHidden()) {
                p->Draw(vg, &m_theme);
            }

            if (it == m_widgets.rbegin()) {
                break;
            }
        }
    }

    m_notif_manager.Draw(vg, &m_theme);

    nvgResetTransform(vg);
    nvgEndFrame(this->vg);
    this->queue.presentImage(this->swapchain, slot);
}

auto App::GetApp() -> App* {
    return g_app;
}

auto App::GetVg() -> NVGcontext* {
    return g_app->vg;
}

void DrawElement(float x, float y, float w, float h, ThemeEntryID id) {
    DrawElement({x, y, w, h}, id);
}

void DrawElement(const Vec4& v, ThemeEntryID id) {
    const auto& e = g_app->m_theme.elements[id];

    switch (e.type) {
        case ElementType::None: {
        } break;
        case ElementType::Texture: {
            auto paint = nvgImagePattern(g_app->vg, v.x, v.y, v.w, v.h, 0, e.texture, 1.f);
            // override the icon colours if set
            if (id > ThemeEntryID_ICON_COLOUR && id < ThemeEntryID_MAX) {
                if (g_app->m_theme.elements[ThemeEntryID_ICON_COLOUR].type != ElementType::None) {
                    paint.innerColor = g_app->m_theme.GetColour(ThemeEntryID_ICON_COLOUR);
                }
            }
            ui::gfx::drawRect(g_app->vg, v, paint);
        } break;
        case ElementType::Colour: {
            ui::gfx::drawRect(g_app->vg, v, e.colour);
        } break;
    }
}

auto App::LoadElementImage(std::string_view value) -> ElementEntry {
    ElementEntry entry{};

    entry.texture = nvgCreateImage(vg, value.data(), 0);
    if (entry.texture) {
        entry.type = ElementType::Texture;
    }

    return entry;
}

auto App::LoadElementColour(std::string_view value) -> ElementEntry {
    ElementEntry entry{};

    if (value.starts_with("0x")) {
        value = value.substr(2);
    } else {
        return {};
    }

    char* end;
    u32 c = std::strtoul(value.data(), &end, 16);
    if (!c && value.data() == end) {
        return {};
    }

    // force alpha bit if not already set.
    if (value.length() <= 6) {
        c <<= 8;
        c |= 0xFF;
    }

    entry.colour = nvgRGBA((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    entry.type = ElementType::Colour;
    return entry;
}

auto App::LoadElement(std::string_view value, ElementType type) -> ElementEntry {
    if (value.size() <= 1) {
        return {};
    }

    if (type == ElementType::None || type == ElementType::Colour) {
        // most assets are colours, so prioritise this first
        if (auto e = LoadElementColour(value); e.type != ElementType::None) {
            return e;
        }
    }

    if (type == ElementType::None || type == ElementType::Texture) {
        if (auto e = LoadElementImage(value); e.type != ElementType::None) {
            return e;
        }
    }

    return {};
}

void App::CloseThemeBackgroundMusic() {
    audio::CloseSong(&m_background_music);
}

void App::CloseTheme() {
    CloseThemeBackgroundMusic();

    for (auto& e : m_theme.elements) {
        if (e.type == ElementType::Texture) {
            nvgDeleteImage(vg, e.texture);
        }
    }

    m_theme = {};
}

void App::LoadTheme(const ThemeMeta& meta) {
    // reset theme
    CloseTheme();

    ThemeData theme_data{};
    theme_data.music_path = m_default_music.Get();
    LoadThemeInternal(meta, theme_data);
    m_theme.meta = meta;

    if (R_SUCCEEDED(romfsInit())) {
        ON_SCOPE_EXIT(romfsExit());

        // load all assets / colours.
        for (auto& e : THEME_ENTRIES) {
            m_theme.elements[e.id] = LoadElement(theme_data.elements[e.id], e.type);
        }

        // load music
        m_theme.music_path = theme_data.music_path;
        LoadAndPlayThemeMusic();
    }
}

// todo: only use opendir on if romfs, otherwise use native fs
void App::ScanThemes(const std::string& path) {
    auto dir = opendir(path.c_str());
    if (!dir) {
        return;
    }
    ON_SCOPE_EXIT(closedir(dir));


    while (auto d = readdir(dir)) {
        if (d->d_name[0] == '.') {
            continue;
        }

        if (d->d_type != DT_REG) {
            continue;
        }

        const std::string name = d->d_name;
        if (!name.ends_with(".ini")) {
            continue;
        }

        const auto full_path = path + name;

        ThemeMeta meta{};
        if (LoadThemeMeta(full_path, meta)) {
            m_theme_meta_entries.emplace_back(meta);
        }
    }
}

void App::ScanThemeEntries() {
    // load from romfs first
    if (R_SUCCEEDED(romfsInit())) {
        ScanThemes("romfs:/themes/");
        romfsExit();
    }

    // then load custom entries
    ScanThemes("/config/sphaira/themes/");
}

void App::LoadAndPlayThemeMusic() {
    if (App::GetThemeMusicEnable() && !m_theme.music_path.empty()) {
        audio::CloseSong(&m_background_music);
        audio::OpenSong(m_fs.get(), m_theme.music_path, audio::Flag_Loop, &m_background_music);
        audio::PlaySong(m_background_music);
    }
}

Result App::SetDefaultBackgroundMusic(fs::Fs* fs, const fs::FsPath& path) {
    constexpr const char* base_path = "/config/sphaira/themes/default_music.";

    const auto ext = std::strrchr(path, '.');
    R_UNLESS(ext, 0x1);

    std::vector<u8> buf;
    R_TRY(fs->read_entire_file(path, buf));

    // remove old file if we made a copy of it.
    audio::CloseSong(&g_app->m_background_music);
    if (g_app->m_default_music.Get().starts_with(base_path)) {
        log_write("[APP] removing previously copied background music file\n");
        g_app->m_fs->DeleteFile(g_app->m_default_music.Get());
    }

    // check if we can link to the file, or if we have to copy it over.
    // sd card fs can link to the file, but music contained in zips or hdd needs copying.
    fs::FsPath new_path{};
    if (path[0] == '/' && fs->IsSd() && g_app->m_fs->FileExists(path)) {
        new_path = path;
        log_write("[APP] linking background music\n");
    } else {
        std::snprintf(new_path, sizeof(new_path), "%s%s", base_path, ext + 1);
        R_TRY(g_app->m_fs->write_entire_file(new_path, buf));
        log_write("[APP] copying background music to sd card\n");
    }

    // save path to the music file.
    g_app->m_default_music.Set(new_path.toString());

    // load and play music file.
    audio::OpenSong(g_app->m_fs.get(), new_path, audio::Flag_Loop, &g_app->m_background_music);
    audio::PlaySong(g_app->m_background_music);

    R_SUCCEED();
}

void App::SetBackgroundMusicPause(bool pause) {
    if (pause) {
        audio::PauseSong(g_app->m_background_music);
    } else {
        audio::PlaySong(g_app->m_background_music);
    }
}

App::App(const char* argv0) {
    // boost mode is enabled in userAppInit().
    ON_SCOPE_EXIT(App::SetBoostMode(false));
    SCOPED_TIMESTAMP("App Constructor");

    g_app = this;
    m_start_timestamp = armGetSystemTick();
    if (!std::strncmp(argv0, "sdmc:/", 6)) {
        // memmove(path, path + 5, strlen(path)-5);
        std::strncpy(m_app_path, argv0 + 5, std::strlen(argv0)-5);
    } else {
        m_app_path = argv0;
    }

    // set if we are hbmenu
    if (IsHbmenu()) {
        __nx_applet_exit_mode = 1;
    }

    // init fs for app use.
    m_fs = std::make_shared<fs::FsNativeSd>(true);

    auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
        auto app = static_cast<App*>(UserData);

        if (!std::strcmp(Section, INI_SECTION)) {
            if (app->m_nxlink_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_mtp_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_ftp_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_hdd_write_protect.LoadFrom(Key, Value)) {}
            else if (app->m_log_enabled.LoadFrom(Key, Value)) {}
            else if (app->m_replace_hbmenu.LoadFrom(Key, Value)) {}
            else if (app->m_default_music.LoadFrom(Key, Value)) {}
            else if (app->m_theme_path.LoadFrom(Key, Value)) {}
            else if (app->m_theme_music.LoadFrom(Key, Value)) {}
            else if (app->m_12hour_time.LoadFrom(Key, Value)) {}
            else if (app->m_language.LoadFrom(Key, Value)) {}
            else if (app->m_left_menu.LoadFrom(Key, Value)) {}
            else if (app->m_right_menu.LoadFrom(Key, Value)) {}
            else if (app->m_install_sysmmc.LoadFrom(Key, Value)) {}
            else if (app->m_install_emummc.LoadFrom(Key, Value)) {}
            else if (app->m_install_sd.LoadFrom(Key, Value)) {}
            else if (app->m_progress_boost_mode.LoadFrom(Key, Value)) {}
            else if (app->m_allow_downgrade.LoadFrom(Key, Value)) {}
            else if (app->m_skip_if_already_installed.LoadFrom(Key, Value)) {}
            else if (app->m_ticket_only.LoadFrom(Key, Value)) {}
            else if (app->m_skip_base.LoadFrom(Key, Value)) {}
            else if (app->m_skip_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_addon.LoadFrom(Key, Value)) {}
            else if (app->m_skip_data_patch.LoadFrom(Key, Value)) {}
            else if (app->m_skip_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_skip_nca_hash_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_header_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_skip_rsa_npdm_fixed_key_verify.LoadFrom(Key, Value)) {}
            else if (app->m_ignore_distribution_bit.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_common_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_convert_to_standard_crypto.LoadFrom(Key, Value)) {}
            else if (app->m_lower_master_key.LoadFrom(Key, Value)) {}
            else if (app->m_lower_system_version.LoadFrom(Key, Value)) {}
        } else if (!std::strcmp(Section, "accessibility")) {
            if (app->m_text_scroll_speed.LoadFrom(Key, Value)) {}
        } else if (!std::strcmp(Section, "dump")) {
            if (app->m_dump_app_folder.LoadFrom(Key, Value)) {}
            else if (app->m_dump_append_folder_with_xci.LoadFrom(Key, Value)) {}
            else if (app->m_dump_trim_xci.LoadFrom(Key, Value)) {}
            else if (app->m_dump_label_trim_xci.LoadFrom(Key, Value)) {}
            else if (app->m_dump_convert_to_common_ticket.LoadFrom(Key, Value)) {}
            else if (app->m_nsz_compress_level.LoadFrom(Key, Value)) {}
            else if (app->m_nsz_compress_threads.LoadFrom(Key, Value)) {}
            else if (app->m_nsz_compress_ldm.LoadFrom(Key, Value)) {}
            else if (app->m_nsz_compress_block.LoadFrom(Key, Value)) {}
            else if (app->m_nsz_compress_block_exponent.LoadFrom(Key, Value)) {}
        }

        return 1;
    };

    // load all configs ahead of time, as this is actually faster than
    // loading each config one by one as it avoids re-opening the file multiple times.
    {
        SCOPED_TIMESTAMP("config init");
        ini_browse(cb, this, CONFIG_PATH);
    }

    if (App::GetLogEnable()) {
        log_file_init();
        log_write("hello world v%s\n", APP_VERSION_HASH);
    }

    // anything that can be async loaded should be placed in here in order
    // to halve load times.
    // rules:
    // - 1: cannot use romfs as its not thread-safe.
    // - 2: cannot use nvg code as its not thread-safe.
    // - 3: cannot be too slow that async takes longer than the main thread (ie, balance the load).
    // currrent load time is 60ms without logs, 90 with (down from 230ms).
    utils::Async async_init([this](){
        SCOPED_TIMESTAMP("App async load");

        {
            SCOPED_TIMESTAMP("config directory init");
            m_fs->CreateDirectoryRecursively("/config/sphaira");
            m_fs->CreateDirectory("/config/sphaira/assoc");
            m_fs->CreateDirectory("/config/sphaira/themes");
            m_fs->CreateDirectory("/config/sphaira/github");
            m_fs->CreateDirectory("/config/sphaira/i18n");
        }

        {
            // delete old themezer cache as themezer does not exit anymore.
            SCOPED_TIMESTAMP("themezer cache delete");
            m_fs->DeleteDirectoryRecursively("/switch/sphaira/cache/themezer");
        }

        if (log_is_init()) {
            SCOPED_TIMESTAMP("fw log init");
            SetSysFirmwareVersion fw_version{};
            setsysInitialize();
            ON_SCOPE_EXIT(setsysExit());
            setsysGetFirmwareVersion(&fw_version);

            log_write("[version] platform: %s\n", fw_version.platform);
            log_write("[version] version_hash: %s\n", fw_version.version_hash);
            log_write("[version] display_version: %s\n", fw_version.display_version);
            log_write("[version] display_title: %s\n", fw_version.display_title);

            splInitialize();
            ON_SCOPE_EXIT(splExit());

            u64 out{};
            splGetConfig((SplConfigItem)65000, &out);
            log_write("[ams] version: %lu.%lu.%lu\n", (out >> 56) & 0xFF, (out >> 48) & 0xFF, (out >> 40) & 0xFF);
            log_write("[ams] target version: %lu.%lu.%lu\n", (out >> 24) & 0xFF, (out >> 16) & 0xFF, (out >> 8) & 0xFF);
            log_write("[ams] key gen: %lu\n", (out >> 32) & 0xFF);

            splGetConfig((SplConfigItem)65003, &out);
            log_write("[ams] hash: %lx\n", out);

            splGetConfig((SplConfigItem)65010, &out);
            log_write("[ams] usb 3.0 enabled: %lu\n", out);
        }

        // get emummc config.
        {
            SCOPED_TIMESTAMP("emummc detect init");
            alignas(0x1000) AmsEmummcPaths paths{};
            SecmonArgs args{};
            args.X[0] = 0xF0000404; /* smcAmsGetEmunandConfig */
            args.X[1] = 0; /* EXO_EMUMMC_MMC_NAND*/
            args.X[2] = (u64)&paths; /* out path */
            svcCallSecureMonitor(&args);
            m_emummc_paths = paths;

            log_write("[emummc] enabled: %u\n", App::IsEmummc());
            if (App::IsEmummc()) {
                log_write("[emummc] file based path: %s\n", m_emummc_paths.file_based_path);
                log_write("[emummc] nintendo path: %s\n", m_emummc_paths.nintendo);
            }
        }

        if (App::GetMtpEnable()) {
            SCOPED_TIMESTAMP("mtp init");
            haze::Init();
        }

        if (App::GetFtpEnable()) {
            SCOPED_TIMESTAMP("ftp init");
            ftpsrv::Init();
        }

        if (App::GetNxlinkEnable()) {
            SCOPED_TIMESTAMP("nxlink init");
            nxlinkInitialize(nxlink_callback);
        }

        if (App::GetHddEnable()) {
            SCOPED_TIMESTAMP("hdd init");
            if (App::GetWriteProtect()) {
                usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReadOnly);
            }

            usbHsFsInitialize(1);
        }

        {
            SCOPED_TIMESTAMP("fat init");
            if (R_FAILED(fatfs::MountAll())) {
                log_write("[FAT] failed to mount bis\n");
            }
        }

        {
            SCOPED_TIMESTAMP("usbdvd init");
            if (R_FAILED(usbdvd::MountAll())) {
                log_write("[USBDVD] failed to mount\n");
            }
        }

        {
            SCOPED_TIMESTAMP("curl init");
            curl::Init();
        }

        {
            SCOPED_TIMESTAMP("timestamp init");
            // ini_putl(GetExePath(), "timestamp", m_start_timestamp, App::PLAYLOG_PATH);
        }

        {
            SCOPED_TIMESTAMP("HID init");
            hidInitializeTouchScreen();
            hidInitializeGesture();
            padConfigureInput(8, HidNpadStyleSet_NpadStandard);
            // padInitializeDefault(&m_pad);
            padInitializeAny(&m_pad);
        }

        {
            SCOPED_TIMESTAMP("loader init");
            const auto loader_info_size = envGetLoaderInfoSize();
            if (loader_info_size) {
                if (loader_info_size >= 8 && !std::memcmp(envGetLoaderInfo(), "sphaira", 7)) {
                    log_write("launching from sphaira created forwarder\n");
                    m_is_launched_via_sphaira_forwader = true;
                } else {
                    log_write("launching from unknown forwader: %.*s size: %zu\n", (int)loader_info_size, envGetLoaderInfo(), loader_info_size);
                }
            } else {
                log_write("not launching from forwarder\n");
            }
        }
    });

    {
        SCOPED_TIMESTAMP("i18n init");
        i18n::init(GetLanguage());
    }

    if (App::GetLogEnable()) {
        App::Notify("Warning! Logs are enabled, Sphaira will run slowly!"_i18n);
    }

#ifdef USE_NVJPG
    {
        SCOPED_TIMESTAMP("nvjpg init");
        // this has to be init before deko3d.
        nj::initialize();
        m_decoder.initialize();
    }
#endif

    // get current size of the framebuffer
    {
        SCOPED_TIMESTAMP("nvg init");
        const auto fb = GetFrameBufferSize();
        s_width = fb.size.x;
        s_height = fb.size.y;
        m_scale = fb.scale;

        // Create the deko3d device
        this->device = dk::DeviceMaker{}
            .setCbDebug(deko3d_error_cb)
            .create();

        // Create the main queue
        this->queue = dk::QueueMaker{this->device}
            .setFlags(DkQueueFlags_Graphics)
            .create();

        // Create the memory pools
        this->pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16*1024*1024);
        this->pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
        this->pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1*1024*1024);

        // Create the static command buffer and feed it freshly allocated memory
        this->cmdbuf = dk::CmdBufMaker{this->device}.create();
        const CMemPool::Handle cmdmem = this->pool_data->allocate(this->StaticCmdSize);
        this->cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

        // Create the framebuffer resources
        this->createFramebufferResources();

        this->renderer.emplace(s_width, s_height, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
        this->vg = nvgCreateDk(&*this->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    }

    // not sure if these are meant to be deleted or not...
    {
        SCOPED_TIMESTAMP("font init");
        PlFontData font_standard, font_extended, font_lang;
        plGetSharedFontByType(&font_standard, PlSharedFontType_Standard);
        plGetSharedFontByType(&font_extended, PlSharedFontType_NintendoExt);

        auto standard_font = nvgCreateFontMem(this->vg, "Standard", (unsigned char*)font_standard.address, font_standard.size, 0);
        auto extended_font = nvgCreateFontMem(this->vg, "Extended", (unsigned char*)font_extended.address, font_extended.size, 0);
        nvgAddFallbackFontId(this->vg, standard_font, extended_font);

        constexpr PlSharedFontType lang_font[] = {
            PlSharedFontType_ChineseSimplified,
            PlSharedFontType_ExtChineseSimplified,
            PlSharedFontType_ChineseTraditional,
            PlSharedFontType_KO,
        };

        for (auto type : lang_font) {
            if (R_SUCCEEDED(plGetSharedFontByType(&font_lang, type))) {
                char name[32];
                std::snprintf(name, sizeof(name), "Lang_%u", font_lang.type);
                auto lang_font = nvgCreateFontMem(this->vg, name, (unsigned char*)font_lang.address, font_lang.size, 0);
                nvgAddFallbackFontId(this->vg, standard_font, lang_font);
            } else {
                log_write("failed plGetSharedFontByType(%d)\n", type);
            }
        }
    }

    {
        SCOPED_TIMESTAMP("hook init");
        appletHook(&m_appletHookCookie, appplet_hook_calback, this);
    }

    // load default image
    {
        SCOPED_TIMESTAMP("load default image");
        m_default_image = nvgCreateImageMem(vg, 0, DEFAULT_IMAGE_DATA, std::size(DEFAULT_IMAGE_DATA));
    }

    // disable audio in applet mode with a suspended application due to audren fatal.
    // see: https://github.com/ITotalJustice/sphaira/issues/92
    if (IsAppletWithSuspendedApp()) {
        App::Notify("Audio disabled due to suspended game"_i18n);
    } else {
        SCOPED_TIMESTAMP("audio init");
        if (R_FAILED(audio::Init())) {
            log_write("[AUDIO] failed to init\n");
        }
    }

    {
        SCOPED_TIMESTAMP("theme init");
        ScanThemeEntries();

        // try and load previous theme, default to previous version otherwise.
        fs::FsPath theme_path = m_theme_path.Get();
        ThemeMeta theme_meta;
        if (R_SUCCEEDED(romfsInit())) {
            ON_SCOPE_EXIT(romfsExit());
            if (!LoadThemeMeta(theme_path, theme_meta)) {
                log_write("failed to load meta using default\n");
                theme_path = DEFAULT_THEME_PATH;
                LoadThemeMeta(theme_path, theme_meta);
            }
        }
        log_write("loading theme from: %s\n", theme_meta.ini_path.s);
        LoadTheme(theme_meta);

        // find theme index using the path of the theme.ini
        for (u64 i = 0; i < m_theme_meta_entries.size(); i++) {
            if (m_theme.meta.ini_path == m_theme_meta_entries[i].ini_path) {
                m_theme_index = i;
                break;
            }
        }
    }
}

void App::PlaySoundEffect(SoundEffect effect) {
    audio::PlaySoundEffect(effect);
}

void App::DisplayThemeOptions(bool left_side) {
    ui::SidebarEntryArray::Items theme_items{};
    const auto theme_meta = App::GetThemeMetaList();
    for (auto& p : theme_meta) {
        theme_items.emplace_back(p.name);
    }

    auto options = std::make_unique<ui::Sidebar>("Theme Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    options->Add<ui::SidebarEntryArray>("Select Theme"_i18n, theme_items, [](s64& index_out){
        App::SetTheme(index_out);
    }, App::GetThemeIndex(), "Customise the look of Sphaira by changing the theme"_i18n);

    options->Add<ui::SidebarEntryBool>("Music"_i18n, App::GetThemeMusicEnable(), [](bool& enable){
        App::SetThemeMusicEnable(enable);
    },  "Enable background music.\n"
        "Each theme can have it's own music file. "
        "If a theme does not set a music file, the default music is loaded instead (if it exists)."_i18n);

    options->Add<ui::SidebarEntryBool>("12 Hour Time"_i18n, App::Get12HourTimeEnable(), [](bool& enable){
        App::Set12HourTimeEnable(enable);
    }, "Changes the clock to 12 hour"_i18n);

    // todo: add file picker for music here.
    // todo: add array to audio which has the list of supported extensions.
    auto remove_music = options->Add<ui::SidebarEntryCallback>("Remove Background Music", [](){
        g_app->m_default_music.Set("");
        audio::CloseSong(&g_app->m_background_music);
    },  "Removes the background music file"_i18n);

    remove_music->Depends([](){
        return !g_app->m_default_music.Get().empty();
    }, "No background music file is set"_i18n);
}

void App::DisplayNetworkOptions(bool left_side) {

}

void App::DisplayMiscOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Misc Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    for (auto& e : ui::menu::main::GetMiscMenuEntries()) {
        if (e.name == g_app->m_left_menu.Get()) {
            continue;
        } else if (e.name == g_app->m_right_menu.Get()) {
            continue;
        }

        auto entry = options->Add<ui::SidebarEntryCallback>(i18n::get(e.title), [e](){
            App::Push(e.func(ui::menu::MenuFlag_None));
        }, i18n::get(e.info));

        if (e.IsInstall()) {
            entry->Depends(App::GetInstallEnable, i18n::get(App::INSTALL_DEPENDS_STR), App::ShowEnableInstallPrompt);
        }
    }

    if (App::IsApplication()) {
        options->Add<ui::SidebarEntryCallback>("Web"_i18n, [](){
            // add some default entries, will use a config file soon so users can set their own.
            ui::PopupList::Items items;
            items.emplace_back("https://lite.duckduckgo.com/lite");
            items.emplace_back("https://dns.switchbru.com");
            items.emplace_back("https://gbatemp.net");
            items.emplace_back("https://github.com/ITotalJustice/sphaira/wiki");
            items.emplace_back("Enter custom URL"_i18n);

            App::Push<ui::PopupList>(
                "Select URL"_i18n, items, [items](auto op_index){
                    if (op_index) {
                        const auto index = *op_index;
                        if (index == items.size() - 1) {
                            std::string out;
                            if (R_SUCCEEDED(swkbd::ShowText(out, "Enter URL"_i18n.c_str(), "https://")) && !out.empty()) {
                                WebShow(out);
                            }
                        } else {
                            WebShow(items[index]);
                        }
                    }
                }
            );
        },
        "Launch the built-in web browser.\n\n",
        "NOTE: The browser is very limted, some websites will fail to load and there's a 30 minute timeout which closes the browser"_i18n);
    }
}

void App::DisplayAdvancedOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Advanced Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items text_scroll_speed_items;
    text_scroll_speed_items.push_back("Slow"_i18n);
    text_scroll_speed_items.push_back("Normal"_i18n);
    text_scroll_speed_items.push_back("Fast"_i18n);

    std::vector<std::string> menu_names;
    ui::SidebarEntryArray::Items menu_items;
    for (auto& e : ui::menu::main::GetMiscMenuEntries()) {
        if (!e.IsShortcut()) {
            continue;
        }

        menu_names.emplace_back(e.name);
        menu_items.push_back(i18n::get(e.name));
    }

    options->Add<ui::SidebarEntryBool>("Logging"_i18n, App::GetLogEnable(), [](bool& enable){
        App::SetLogEnable(enable);
    }, "Logs to /config/sphaira/log.txt"_i18n);

    options->Add<ui::SidebarEntryBool>("Replace hbmenu on exit"_i18n, App::GetReplaceHbmenuEnable(), [](bool& enable){
        App::SetReplaceHbmenuEnable(enable);
    }, "When enabled, it replaces /hbmenu.nro with Sphaira, creating a backup of hbmenu to /switch/hbmenu.nro\n\n" \
       "Disabling will give you the option to restore hbmenu."_i18n);

    options->Add<ui::SidebarEntryBool>("Boost CPU during transfer"_i18n, App::GetApp()->m_progress_boost_mode,
        "Enables boost mode during transfers which can improve transfer speed. "
        "This sets the CPU to 1785mhz and lowers the GPU 76mhz"_i18n);

    options->Add<ui::SidebarEntryArray>("Text scroll speed"_i18n, text_scroll_speed_items, [](s64& index_out){
        App::SetTextScrollSpeed(index_out);
    }, App::GetTextScrollSpeed(), "Change how fast the scrolling text updates"_i18n);

    options->Add<ui::SidebarEntryArray>("Set left-side menu"_i18n, menu_items, [menu_names](s64& index_out){
        const auto e = menu_names[index_out];
        if (g_app->m_left_menu.Get() != e) {
            // swap menus around.
            if (g_app->m_right_menu.Get() == e) {
                g_app->m_right_menu.Set(g_app->m_left_menu.Get());
            }
            g_app->m_left_menu.Set(e);

            App::Push<ui::OptionBox>(
                "Press OK to restart Sphaira"_i18n, "OK"_i18n, [](auto){
                    App::ExitRestart();
                }
            );
        }
    }, i18n::get(g_app->m_left_menu.Get()), "Set the menu that appears on the left tab."_i18n);

    options->Add<ui::SidebarEntryArray>("Set right-side menu"_i18n, menu_items, [menu_names](s64& index_out){
        const auto e = menu_names[index_out];
        if (g_app->m_right_menu.Get() != e) {
            // swap menus around.
            if (g_app->m_left_menu.Get() == e) {
                g_app->m_left_menu.Set(g_app->m_right_menu.Get());
            }
            g_app->m_right_menu.Set(e);

            App::Push<ui::OptionBox>(
                "Press OK to restart Sphaira"_i18n, "OK"_i18n, [](auto){
                    App::ExitRestart();
                }
            );
        }
    }, i18n::get(g_app->m_right_menu.Get()), "Set the menu that appears on the right tab."_i18n);

    options->Add<ui::SidebarEntryCallback>("Install options"_i18n, [left_side](){
        App::DisplayInstallOptions(left_side);
    },  "Change the install options.\n"
        "You can enable installing from here."_i18n);

    options->Add<ui::SidebarEntryCallback>("Export options"_i18n, [left_side](){
        App::DisplayDumpOptions(left_side);
    },  "Change the export options."_i18n);

    static const char* erpt_path = "/atmosphere/erpt_reports";
    options->Add<ui::SidebarEntryBool>("Disable erpt_reports"_i18n, g_app->m_fs->FileExists(erpt_path), [](bool& enable){
        if (enable) {
            Result rc;
            // it's possible for erpt to generate a report in between deleting the folder and creating the file.
            for (int i = 0; i < 10; i++) {
                g_app->m_fs->DeleteDirectoryRecursively(erpt_path);
                if (R_SUCCEEDED(rc = g_app->m_fs->CreateFile(erpt_path))) {
                    break;
                }
            }
            enable = R_SUCCEEDED(rc);
        } else {
            g_app->m_fs->DeleteFile(erpt_path);
            g_app->m_fs->CreateDirectory(erpt_path);
        }
    }, "Disables error reports generated in /atmosphere/erpt_reports."_i18n);
}

void App::DisplayInstallOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Install Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items install_items;
    install_items.push_back("System memory"_i18n);
    install_items.push_back("microSD card"_i18n);

    options->Add<ui::SidebarEntryBool>("Enable sysmmc"_i18n, App::GetInstallSysmmcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_sysmmc, enable);
    }, "Enables installing whilst in sysMMC mode."_i18n);

    options->Add<ui::SidebarEntryBool>("Enable emummc"_i18n, App::GetInstallEmummcEnable(), [](bool& enable){
        ShowEnableInstallPromptOption(g_app->m_install_emummc, enable);
    }, "Enables installing whilst in emuMMC mode."_i18n);

    options->Add<ui::SidebarEntryArray>("Install location"_i18n, install_items, [](s64& index_out){
        App::SetInstallSdEnable(index_out);
    }, (s64)App::GetInstallSdEnable());

    options->Add<ui::SidebarEntryBool>("Allow downgrade"_i18n, App::GetApp()->m_allow_downgrade,
        "Allows for installing title updates that are lower than the currently installed update."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip if already installed"_i18n, App::GetApp()->m_skip_if_already_installed,
        "Skips installing titles / ncas if they're already installed."_i18n);

    options->Add<ui::SidebarEntryBool>("Ticket only"_i18n, App::GetApp()->m_ticket_only,
        "Installs tickets only, useful if the title was already installed however the tickets were missing or corrupted."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip base"_i18n, App::GetApp()->m_skip_base,
        "Skips installing the base application."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip patch"_i18n, App::GetApp()->m_skip_patch,
        "Skips installing updates."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip dlc"_i18n, App::GetApp()->m_skip_addon,
        "Skips installing DLC."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip data patch"_i18n, App::GetApp()->m_skip_data_patch,
        "Skips installing DLC update (data patch)."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip ticket"_i18n, App::GetApp()->m_skip_ticket,
        "Skips installing tickets, not recommended."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip NCA hash verify"_i18n, App::GetApp()->m_skip_nca_hash_verify,
        "Enables the option to skip sha256 verification. This is a hash over the entire NCA. "
        "It is used to verify that the NCA is valid / not corrupted. "
        "You may have seen the option for \"checking for corrupted data\" when a corrupted game is installed. "
        "That check performs various hash checks, including the hash over the NCA.\n\n"
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip RSA header verify"_i18n, App::GetApp()->m_skip_rsa_header_fixed_key_verify,
        "Enables the option to skip RSA NCA fixed key verification. "
        "This is a hash over the NCA header. It is used to verify that the header has not been modified. "
        "The header is signed by nintendo, thus it cannot be forged, and is reliable to detect modified NCA headers (such as NSP/XCI converts).\n\n"
        "It is recommended to keep this disabled, unless you need to install nsp/xci converts."_i18n);

    options->Add<ui::SidebarEntryBool>("Skip RSA NPDM verify"_i18n, App::GetApp()->m_skip_rsa_npdm_fixed_key_verify,
        "Enables the option to skip RSA NPDM fixed key verification.\n\n"
        "Currently, this option is stubbed (not implemented)."_i18n);

    options->Add<ui::SidebarEntryBool>("Ignore distribution bit"_i18n, App::GetApp()->m_ignore_distribution_bit,
        "If set, it will ignore the distribution bit in the NCA header. "
        "The distribution bit is used to signify whether a NCA is Eshop or GameCard. "
        "You cannot (normally) launch install games that have the distruction bit set to GameCard.\n\n"
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Convert to common ticket"_i18n, App::GetApp()->m_convert_to_common_ticket,
        "[Requires keys] Converts personalised tickets to common (fake) tickets.\n\n"
        "It is recommended to keep this enabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Convert to standard crypto"_i18n, App::GetApp()->m_convert_to_standard_crypto,
        "[Requires keys] Converts titlekey to standard crypto, also known as \"ticketless\".\n\n"
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Lower master key"_i18n, App::GetApp()->m_lower_master_key,
        "[Requires keys] Encrypts the keak (key area key) with master key 0, which allows the game to be launched on every fw. "
        "Implicitly performs standard crypto.\n\n"
        "Do note that just because the game can be launched on any fw (as it can be decrypted), doesn't mean it will work. It is strongly recommened to update your firmware and Atmosphere version in order to play the game, rather than enabling this option.\n\n"
        "It is recommended to keep this disabled."_i18n);

    options->Add<ui::SidebarEntryBool>("Lower system version"_i18n, App::GetApp()->m_lower_system_version,
        "Sets the system_firmware field in the cnmt extended header to 0. "
        "Note: if the master key is higher than fw version, the game still won't launch as the fw won't have the key to decrypt keak (see above).\n\n"
        "It is recommended to keep this disabled."_i18n);
}

void App::DisplayDumpOptions(bool left_side) {
    auto options = std::make_unique<ui::Sidebar>("Export Options"_i18n, left_side ? ui::Sidebar::Side::LEFT : ui::Sidebar::Side::RIGHT);
    ON_SCOPE_EXIT(App::Push(std::move(options)));

    ui::SidebarEntryArray::Items nsz_level_items;
    for (auto& e : NSZ_COMPRESS_LEVEL_OPTIONS) {
        nsz_level_items.emplace_back(e.name);
    }

    ui::SidebarEntryArray::Items nsz_thread_items;
    for (auto& e : NSZ_COMPRESS_THREAD_OPTIONS) {
        nsz_thread_items.emplace_back(e.name);
    }

    ui::SidebarEntryArray::Items nsz_block_items;
    for (auto& e : NSZ_COMPRESS_BLOCK_OPTIONS) {
        nsz_block_items.emplace_back(e.name);
    }

    options->Add<ui::SidebarEntryBool>(
        "Created nested folder"_i18n, App::GetApp()->m_dump_app_folder,
        "Creates a folder using the name of the game.\n"
        "For example, /name/name.xci\n"
        "Disabling this would use /name.xci"_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Append folder with .xci"_i18n, App::GetApp()->m_dump_append_folder_with_xci,
        "XCI dumps will name the folder with the .xci extension.\n"
        "For example, /name.xci/name.xci\n\n"
        "Some devices only function is the xci folder is named exactly the same as the xci."_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Trim XCI"_i18n, App::GetApp()->m_dump_trim_xci,
        "Removes the unused data at the end of the XCI, making the output smaller."_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Label trimmed XCI"_i18n, App::GetApp()->m_dump_label_trim_xci,
        "Names the trimmed xci.\n"
        "For example, /name/name (trimmed).xci"_i18n
    );
    options->Add<ui::SidebarEntryBool>(
        "Convert to common ticket"_i18n, App::GetApp()->m_dump_convert_to_common_ticket,
        "Converts personalised ticket to a fake common ticket."_i18n
    );

    options->Add<ui::SidebarEntryArray>("NSZ level"_i18n, nsz_level_items, [](s64& index_out){
        App::GetApp()->m_nsz_compress_level.Set(index_out);
    }, App::GetApp()->m_nsz_compress_level.Get(),
        "Sets the compression level used when exporting to NSZ.\n\n"
        "NOTE: The switch CPU is not very fast, and setting the value too high can "
        "result in exporting taking a very long time for very little gain in size.\n\n"
        "It is recommended to set this value to 3."_i18n
    );

    options->Add<ui::SidebarEntryArray>("NSZ threads"_i18n, nsz_thread_items, [](s64& index_out){
        App::GetApp()->m_nsz_compress_threads.Set(index_out);
    }, App::GetApp()->m_nsz_compress_threads.Get(),
        "Sets the number of threads used when compression the NCA.\n\n"
        "A value less than 3 allows for another thread to run freely, such as read/write threads. "
        "However in my testing, a value of 3 was usually the most performant.\n"
        "A value of 0 will use no threads and should only be used for testing as it is always slower.\n\n"
        "It is recommended to set this value between 1-3."_i18n
    );

    options->Add<ui::SidebarEntryBool>(
        "NSZ long distance mode"_i18n, App::GetApp()->m_nsz_compress_ldm,
        "Enables \"Long Distance Mode\" which can reduce the output size at the cost of speed."_i18n
    );

    options->Add<ui::SidebarEntryBool>(
        "NSZ block compression"_i18n, App::GetApp()->m_nsz_compress_block,
        "Enables block compression, which compresses the NCA into blocks (at the cost of compression ratio) "
        "which allows for random access, allowing the NCZ to be mounted as a file system.\n\n"
        "NOTE: Sphaira does not yet support mounting NCZ as a file system, but it will be added evntually."_i18n
    );

    auto block_size_option = options->Add<ui::SidebarEntryArray>("NSZ block size"_i18n, nsz_block_items, [](s64& index_out){
        App::GetApp()->m_nsz_compress_block_exponent.Set(index_out);
    }, App::GetApp()->m_nsz_compress_block_exponent.Get(),
        "Sets the size of each block. The smaller the size, the faster the random access is at the cost of compression ratio."_i18n
    );
    block_size_option->Depends(App::GetApp()->m_nsz_compress_block, "NSZ block compression is disabled."_i18n);
}

void App::ShowEnableInstallPrompt() {
    // warn the user the dangers of installing.
    App::Push<ui::OptionBox>(
        "Installing is disabled, enable now?"_i18n,
        "Back"_i18n, "Enable"_i18n, 0, [](auto op_index){
            if (op_index && *op_index) {
                // get the install option based on sysmmc/emummc.
                auto& option = IsEmummc() ? g_app->m_install_emummc : g_app->m_install_sysmmc;

                // dummy ref.
                static bool enable{};
                enable = true;

                return ShowEnableInstallPromptOption(option, enable);
            }
        }
    );
}

void App::ShowEnableInstallPromptOption(option::OptionBool& option, bool& enable) {
    if (enable) {
        // warn the user the dangers of installing.
        App::Push<ui::OptionBox>(
            "WARNING: Installing apps will lead to a ban!"_i18n,
            "Back"_i18n, "Enable"_i18n, 0, [&option, &enable](auto op_index){
                if (op_index && *op_index) {
                    option.Set(true);
                    App::Notify("Installing enabled!"_i18n);
                } else {
                    enable = false;
                }
            }
        );
    } else {
        option.Set(false);
    }
}

App::~App() {
    // boost mode is disabled in userAppExit().
    App::SetBoostMode(true);

    log_write("starting to exit\n");
    {
        SCOPED_TIMESTAMP("TOTAL EXIT");
        appletUnhook(&m_appletHookCookie);

        // async exit as these threads sleep every 100ms.
        {
            SCOPED_TIMESTAMP("async signal");
            nxlinkSignalExit();
            ftpsrv::ExitSignal();
            audio::ExitSignal();
            curl::ExitSignal();
        }

        utils::Async async_exit([this](){
            {
                SCOPED_TIMESTAMP("usbdvd_exit");
                usbdvd::UnmountAll();
            }

            {
                SCOPED_TIMESTAMP("i18n_exit");
                i18n::exit();
            }

            {
                SCOPED_TIMESTAMP("mtp exit");
                haze::Exit();
            }

            {
                SCOPED_TIMESTAMP("hdd exit");
                usbHsFsExit();
            }

            {
                SCOPED_TIMESTAMP("fatfs exit");
                fatfs::UnmountAll();
            }

            // do these last as they were signalled to exit.
            {
                SCOPED_TIMESTAMP("audio_exit");
                audio::CloseSong(&m_background_music);
                audio::Exit();
            }

            {
                SCOPED_TIMESTAMP("ftp exit");
                ftpsrv::Exit();
            }

            {
                SCOPED_TIMESTAMP("nxlink exit");
                nxlinkExit();
            }

            {
                SCOPED_TIMESTAMP("curl_exit");
                curl::Exit();
            }
        });

        // destroy this first as it seems to prevent a crash when exiting the appstore
        // when an image that was being drawn is displayed
        // replicate: saves -> homebrew -> misc -> appstore -> sphaira -> changelog -> exit
        // it will crash when deleting image 43.
        {
            SCOPED_TIMESTAMP("destroy frame buffer resources");
            this->destroyFramebufferResources();
        }

        // this has to be called before any cleanup to ensure the lifetime of
        // nvg is still active as some widgets may need to free images.
        // clear in reverse order as the widgets are a stack (todo: just use a stack?)
        {
            SCOPED_TIMESTAMP("widget exit");
            while (!m_widgets.empty()) {
                m_widgets.pop_back();
            }
        }

        // do not async close theme as it frees textures.
        {
            SCOPED_TIMESTAMP("theme exit");
            ini_puts("config", "theme", m_theme.meta.ini_path, CONFIG_PATH);
            CloseTheme();
        }

        {
            SCOPED_TIMESTAMP("nvg exit");
            nvgDeleteImage(vg, m_default_image);
            nvgDeleteDk(this->vg);
            this->renderer.reset();

    #ifdef USE_NVJPG
            m_decoder.finalize();
            nj::finalize();
    #endif
        }

        // backup hbmenu if it is not sphaira
        {
            SCOPED_TIMESTAMP("nro copy main");
            if (App::GetReplaceHbmenuEnable() && !IsHbmenu()) {
                NacpStruct hbmenu_nacp;
                Result rc;

                // todo: don't read whole nacp, only the name.
                // todo: keep file open and use that as part of the file copy.
                if (R_SUCCEEDED(rc = nro_get_nacp("/hbmenu.nro", hbmenu_nacp)) && std::strcmp(hbmenu_nacp.lang[0].name, "sphaira")) {
                    log_write("backing up hbmenu.nro\n");
                    if (R_FAILED(rc = m_fs->copy_entire_file("/switch/hbmenu.nro", "/hbmenu.nro"))) {
                        log_write("failed to backup  hbmenu.nro\n");
                    }
                } else {
                    log_write("not backing up\n");
                }

                if (R_FAILED(rc = m_fs->copy_entire_file("/hbmenu.nro", GetExePath()))) {
                    log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", GetExePath().s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
                } else {
                    log_write("success with copying over root file!\n");
                }
            } else if (IsHbmenu()) {
                // check we have a version that's newer than current.
                NacpStruct hbmenu_nacp;
                Result rc;

                // ensure that are still sphaira
                if (R_SUCCEEDED(rc = nro_get_nacp("/hbmenu.nro", hbmenu_nacp)) && !std::strcmp(hbmenu_nacp.lang[0].name, "sphaira")) {
                    NacpStruct sphaira_nacp;
                    fs::FsPath sphaira_path = "/switch/sphaira/sphaira.nro";

                    rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    if (R_FAILED(rc) || std::strcmp(sphaira_nacp.lang[0].name, "sphaira")) {
                        sphaira_path = "/switch/sphaira.nro";
                        rc = nro_get_nacp(sphaira_path, sphaira_nacp);
                    }

                    // found sphaira, now lets get compare version
                    if (R_SUCCEEDED(rc) && !std::strcmp(sphaira_nacp.lang[0].name, "sphaira")) {
                        if (IsVersionNewer(hbmenu_nacp.display_version, sphaira_nacp.display_version)) {
                            if (R_FAILED(rc = m_fs->copy_entire_file(GetExePath(), sphaira_path))) {
                                log_write("failed to copy entire file: %s 0x%X module: %u desc: %u\n", sphaira_path.s, rc, R_MODULE(rc), R_DESCRIPTION(rc));
                            } else {
                                log_write("success with updating hbmenu!\n");
                            }
                        }
                    }
                } else {
                    log_write("no longer hbmenu!\n");
                }
            }
        }
    }

    if (App::GetLogEnable()) {
        log_write("closing log\n");
        log_file_exit();
    }
}

auto App::GetVersionFromString(const char* str) -> u32 {
    u32 major{}, minor{}, macro{};
    std::sscanf(str, "%u.%u.%u", &major, &minor, &macro);
    return MAKEHOSVERSION(major, minor, macro);
}

auto App::IsVersionNewer(const char* current, const char* new_version) -> u32 {
    return GetVersionFromString(current) < GetVersionFromString(new_version);
}

void App::createFramebufferResources() {
    this->swapchain = nullptr;

    // Create layout for the depth buffer
    dk::ImageLayout layout_depthbuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(s_width, s_height)
        .initialize(layout_depthbuffer);

    // Create the depth buffer
    this->depthBuffer_mem = this->pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
    this->depthBuffer.initialize(layout_depthbuffer, this->depthBuffer_mem.getMemBlock(), this->depthBuffer_mem.getOffset());

    // Create layout for the framebuffers
    dk::ImageLayout layout_framebuffer;
    dk::ImageLayoutMaker{device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(s_width, s_height)
        .initialize(layout_framebuffer);

    // Create the framebuffers
    std::array<DkImage const*, NumFramebuffers> fb_array;
    const u64 fb_size  = layout_framebuffer.getSize();
    const uint32_t fb_align = layout_framebuffer.getAlignment();
    for (unsigned i = 0; i < fb_array.size(); i++) {
        // Allocate a framebuffer
        this->framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
        this->framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

        // Generate a command list that binds it
        dk::ImageView colorTarget{ framebuffers[i] }, depthTarget{ depthBuffer };
        this->cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
        this->framebuffer_cmdlists[i] = cmdbuf.finishList();

        // Fill in the array for use later by the swapchain creation code
        fb_array[i] = &framebuffers[i];
    }

    // Create the swapchain using the framebuffers
    this->swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), fb_array}.create();

    // Generate the main rendering cmdlist
    this->recordStaticCommands();
}

void App::destroyFramebufferResources() {
    // Return early if we have nothing to destroy
    if (!this->swapchain) {
        return;
    }

    this->queue.waitIdle();
    this->cmdbuf.clear();
    swapchain.destroy();

    // Destroy the framebuffers
    for (unsigned i = 0; i < NumFramebuffers; i++) {
        framebuffers_mem[i].destroy();
    }

    // Destroy the depth buffer
    this->depthBuffer_mem.destroy();
}

void App::recordStaticCommands() {
    // Initialize state structs with deko3d defaults
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState;
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;

    // Configure the viewport and scissor
    this->cmdbuf.setViewports(0, { { 0.0f, 0.0f, (float)s_width, (float)s_height, 0.0f, 1.0f } });
    this->cmdbuf.setScissors(0, { { 0, 0, (u32)s_width, (u32)s_height } });

    // Clear the color and depth buffers
    this->cmdbuf.clearColor(0, DkColorMask_RGBA, 0.2f, 0.3f, 0.3f, 1.0f);
    this->cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

    // Bind required state
    this->cmdbuf.bindRasterizerState(rasterizerState);
    this->cmdbuf.bindColorState(colorState);
    this->cmdbuf.bindColorWriteState(colorWriteState);

    this->render_cmdlist = this->cmdbuf.finishList();
}

} // namespace sphaira
