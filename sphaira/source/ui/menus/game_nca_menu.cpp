#include "ui/menus/game_nca_menu.hpp"
#include "ui/menus/filebrowser.hpp"

#include "ui/nvg_util.hpp"
#include "ui/sidebar.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/crypto.hpp"

#include "title_info.hpp"
#include "app.hpp"
#include "dumper.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "hasher.hpp"

#include <cstring>
#include <algorithm>

namespace sphaira::ui::menu::game::meta_nca {
namespace {

struct HashStr {
    char str[0x21];
};

HashStr hexIdToStr(auto id) {
    HashStr str{};
    const auto id_lower = std::byteswap(*(u64*)id.c);
    const auto id_upper = std::byteswap(*(u64*)(id.c + 0x8));
    std::snprintf(str.str, 0x21, "%016lx%016lx", id_lower, id_upper);
    return str;
}

struct NcaHashSource final : hash::BaseSource {
    NcaHashSource(NcmContentStorage* cs, const NcaEntry& entry) : m_cs{cs}, m_entry{entry} {
    }

    Result Size(s64* out) override {
        *out = m_entry.size;
        R_SUCCEED();
    }

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        const auto rc = ncmContentStorageReadContentIdFile(m_cs, buf, size, &m_entry.content_id, off);
        if (R_SUCCEEDED(rc)) {
            *bytes_read = size;
        }
        return rc;
    }

private:
    NcmContentStorage* const m_cs;
    const NcaEntry& m_entry{};
};

struct NcaSource final : dump::BaseSource {
    NcaSource(NcmContentStorage* cs, int icon, const std::vector<NcaEntry>& entries) : m_cs{cs}, m_icon{icon}, m_entries{entries} {
        m_is_file_based_emummc = App::IsFileBaseEmummc();
    }

    Result Read(const std::string& path, void* buf, s64 off, s64 size, u64* bytes_read) override {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(hexIdToStr(e.content_id).str) != path.npos;
        });
        R_UNLESS(it != m_entries.end(), Result_GameBadReadForDump);

        const auto rc = ncmContentStorageReadContentIdFile(m_cs, buf, size, &it->content_id, off);
        if (R_SUCCEEDED(rc)) {
            *bytes_read = size;
        }

        if (m_is_file_based_emummc) {
            svcSleepThread(2e+6); // 2ms
        }

        return rc;
    }

    auto GetName(const std::string& path) const -> std::string {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(hexIdToStr(e.content_id).str) != path.npos;
        });

        if (it != m_entries.end()) {
            return hexIdToStr(it->content_id).str;
        }

        return {};
    }

    auto GetSize(const std::string& path) const -> s64 {
        const auto it = std::ranges::find_if(m_entries, [&path](auto& e){
            return path.find(hexIdToStr(e.content_id).str) != path.npos;
        });

        if (it != m_entries.end()) {
            return it->size;
        }

        return 0;
    }

    auto GetIcon(const std::string& path) const -> int override {
        return m_icon ? m_icon : App::GetDefaultImage();
    }

private:
    NcmContentStorage* const m_cs;
    const int m_icon;
    std::vector<NcaEntry> m_entries{};
    bool m_is_file_based_emummc{};
};

Result GetFsFileSystemType(u8 content_type, FsFileSystemType& out) {
    switch (content_type) {
        case nca::ContentType_Meta:
            out = FsFileSystemType_ContentMeta;
            R_SUCCEED();
        case nca::ContentType_Control:
            out = FsFileSystemType_ContentControl;
            R_SUCCEED();
        case nca::ContentType_Manual:
            out = FsFileSystemType_ContentManual;
            R_SUCCEED();
        case nca::ContentType_Data:
            out = FsFileSystemType_ContentData;
            R_SUCCEED();
    }

    R_THROW(0x1);
}

} // namespace

Menu::Menu(Entry& entry, const meta::MetaEntry& meta_entry)
: MenuBase{entry.GetName(), MenuFlag_None}
, m_entry{entry}
, m_meta_entry{meta_entry} {
    this->SetActions(
        std::make_pair(Button::L2, Action{"Select"_i18n, [this](){
            // if both set, select all.
            if (App::GetApp()->m_controller.GotHeld(Button::R2)) {
                const auto set = m_selected_count != m_entries.size();

                for (u32 i = 0; i < m_entries.size(); i++) {
                    auto& e = GetEntry(i);
                    if (e.selected != set) {
                        e.selected = set;
                        if (set) {
                            m_selected_count++;
                        } else {
                            m_selected_count--;
                        }
                    }
                }
            } else {
                GetEntry().selected ^= 1;
                if (GetEntry().selected) {
                    m_selected_count++;
                } else {
                    m_selected_count--;
                }
            }
        }}),
        std::make_pair(Button::A, Action{"Mount Fs"_i18n, [this](){
            // todo: handle error here.
            MountNcaFs();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Options"_i18n, [this](){
            auto options = std::make_unique<Sidebar>("NCA Options"_i18n, Sidebar::Side::RIGHT);
            ON_SCOPE_EXIT(App::Push(std::move(options)));

            if (!m_entries.empty()) {
                options->Add<SidebarEntryCallback>("Export NCA"_i18n, [this](){
                    DumpNcas();
                });

                options->Add<SidebarEntryCallback>("Verify NCA 256 hash"_i18n, [this](){
                    static std::string hash_out;
                    hash_out.clear();

                    App::Push<ProgressBox>(m_entry.image, "Hashing"_i18n, hexIdToStr(GetEntry().content_id).str, [this](auto pbox) -> Result{
                        auto source = std::make_unique<NcaHashSource>(m_meta.cs, GetEntry());
                        return hash::Hash(pbox, hash::Type::Sha256, source.get(), hash_out);
                    }, [this](Result rc){
                        App::PushErrorBox(rc, "Failed to hash file..."_i18n);
                        const auto str = hexIdToStr(GetEntry().content_id);

                        if (R_SUCCEEDED(rc)) {
                            if (std::strncmp(hash_out.c_str(), str.str, std::strlen(str.str))) {
                                App::Push<OptionBox>("NCA hash missmatch!"_i18n, "OK"_i18n);
                            } else {
                                App::Push<OptionBox>("NCA hash valid."_i18n, "OK"_i18n);
                            }
                        }
                    });
                },  "Performs sha256 hash over the NCA to check if it's valid.\n\n"
                    "NOTE: This only detects if the hash is missmatched, it does not validate if \
                    the content has been modified at all."_i18n);

                options->Add<SidebarEntryCallback>("Verify NCA fixed key"_i18n, [this](){
                    if (R_FAILED(nca::VerifyFixedKey(GetEntry().header))) {
                        App::Push<OptionBox>("NCA fixed key is invalid!"_i18n, "OK"_i18n);
                    } else {
                        App::Push<OptionBox>("NCA fixed key is valid."_i18n, "OK"_i18n);
                    }
                },  "Performs RSA NCA fixed key verification. "\
                    "This is a hash over the NCA header. It is used to verify that the header has not been modified. "\
                    "The header is signed by nintendo, thus it cannot be forged, and is reliable to detect modified NCA headers (such as NSP/XCI converts)."_i18n);
            }
        }})
    );

    keys::Keys keys;
    parse_keys(keys, false);

    if (R_FAILED(GetNcmMetaFromMetaStatus(m_meta_entry.status, m_meta))) {
        SetPop();
        return;
    }

    // get the content meta header.
    ncm::ContentMeta content_meta;
    if (R_FAILED(ncm::GetContentMeta(m_meta.db, &m_meta.key, content_meta))) {
        SetPop();
        return;
    }

    // fetch all the content infos.
    std::vector<NcmContentInfo> infos;
    if (R_FAILED(ncm::GetContentInfos(m_meta.db, &m_meta.key, content_meta.header, infos))) {
        SetPop();
        return;
    }

    for (const auto& info : infos) {
        NcaEntry entry{};
        entry.content_id = info.content_id;
        entry.content_type = info.content_type;
        ncmContentInfoSizeToU64(&info, &entry.size);

        bool has = false;
        ncmContentMetaDatabaseHasContent(m_meta.db, &has, &m_meta.key, &info.content_id);
        entry.missing = !has;

        if (has && R_SUCCEEDED(ncmContentStorageReadContentIdFile(m_meta.cs, &entry.header, sizeof(entry.header), &info.content_id, 0))) {
            // decrypt header.
            crypto::cryptoAes128Xts(&entry.header, &entry.header, keys.header_key, 0, 0x200, sizeof(entry.header), false);
        }

        m_entries.emplace_back(entry);
    }

    // todo: maybe width is broken here?
    const Vec4 v{485, GetY() + 1.f + 42.f, 720, 60};
    m_list = std::make_unique<List>(1, 8, m_pos, v);

    char subtitle[128];
    std::snprintf(subtitle, sizeof(subtitle), "by %s", entry.GetAuthor());
    SetTitleSubHeading(subtitle);

    SetIndex(0);
}

Menu::~Menu() {
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (touch && m_index == i) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect_Focus);
            SetIndex(i);
        }
    });
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    // draw left-side grid background.
    gfx::drawRect(vg, 30, 90, 375, 555, theme->GetColour(ThemeEntryID_GRID));

    // draw the game icon (maybe remove this or reduce it's size).
    const auto& e = m_entries[m_index];
    gfx::drawImage(vg, 90, 130, 256, 256, m_entry.image ? m_entry.image : App::GetDefaultImage());

    if (e.header.magic != NCA3_MAGIC) {
        gfx::drawTextArgs(vg, 50, 415, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Failed to decrypt NCA");
    } else {
        nvgSave(vg);
        nvgIntersectScissor(vg, 50, 90, 325, 555);
        gfx::drawTextArgs(vg, 50, 415, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Application Type: %s", ncm::GetReadableMetaTypeStr(m_meta_entry.status.meta_type));
        gfx::drawTextArgs(vg, 50, 455, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Content Type: %s", nca::GetContentTypeStr(e.header.content_type));
        gfx::drawTextArgs(vg, 50, 495, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Distribution Type: %s", nca::GetDistributionTypeStr(e.header.distribution_type));
        gfx::drawTextArgs(vg, 50, 535, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Program ID: %016lX", e.header.program_id);
        gfx::drawTextArgs(vg, 50, 575, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Key Generation: %u (%s)", e.header.GetKeyGeneration(), nca::GetKeyGenStr(e.header.GetKeyGeneration()));
        gfx::drawTextArgs(vg, 50, 615, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "SDK Version: %u.%u.%u.%u", e.header.sdk_major, e.header.sdk_minor, e.header.sdk_micro, e.header.sdk_revision);
        nvgRestore(vg);
    }

    // exit early if we have no entries (maybe?)
    if (m_entries.empty()) {
        // todo: center this.
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    constexpr float text_xoffset{15.f};

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;
        auto& e = m_entries[i];

        auto text_id = ThemeEntryID_TEXT;
        if (m_index == i) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else {
            if (i != m_entries.size() - 1) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }
        }

        gfx::drawTextArgs(vg, x + text_xoffset, y + (h / 2.f), 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", ncm::GetContentTypeStr(e.content_type));
        gfx::drawTextArgs(vg, x + text_xoffset + 185, y + (h / 2.f), 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%s", hexIdToStr(e.content_id).str);

        if ((double)e.size / 1024.0 / 1024.0 <= 0.009) {
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f), 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%.2f KiB", (double)e.size / 1024.0);
        } else if ((double)e.size / 1024.0 / 1024.0 / 1024.0 <= 0.009) {
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f), 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%.2f MiB", (double)e.size / 1024.0 / 1024.0);
        } else {
            gfx::drawTextArgs(vg, x + w - text_xoffset, y + (h / 2.f), 16.f, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), "%.2f GiB", (double)e.size / 1024.0 / 1024.0 / 1024.0);
        }

        if (e.missing) {
            gfx::drawText(vg, x + text_xoffset - 80 / 2, y + (h / 2.f) - (24.f / 2), 24.f, "\uE140", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR));
        } else if (e.selected) {
            gfx::drawText(vg, x + text_xoffset - 80 / 2, y + (h / 2.f) - (24.f / 2), 24.f, "\uE14B", nullptr, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_SELECTED));
        }
    });
}

void Menu::SetIndex(s64 index) {
    m_index = index;
    if (!m_index) {
        m_list->SetYoff(0);
    }

    UpdateSubheading();
}

void Menu::UpdateSubheading() {
    const auto index = m_entries.empty() ? 0 : m_index + 1;
    this->SetSubHeading(std::to_string(index) + " / " + std::to_string(m_entries.size()));
}

void Menu::DumpNcas() {
    const auto entries = GetSelectedEntries();
    App::PopToMenu();

    fs::FsPath name_buf = m_entry.GetName();
    title::utilsReplaceIllegalCharacters(name_buf, true);

    char version[sizeof(NacpStruct::display_version) + 1]{};
    if (m_meta_entry.status.meta_type == NcmContentMetaType_Patch) {
        std::snprintf(version, sizeof(version), "%s ", m_meta_entry.nacp.display_version);
    }

    std::vector<fs::FsPath> paths;
    for (auto& e : entries) {
        char nca_name[64];
        std::snprintf(nca_name, sizeof(nca_name), "%s%s", hexIdToStr(e.content_id).str, e.content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");

        fs::FsPath path;
        std::snprintf(path, sizeof(path), "/dumps/NCA/%s %s[%016lX][v%u][%s]/%s", name_buf.s, version, m_meta_entry.status.application_id, m_meta_entry.status.version, ncm::GetMetaTypeShortStr(m_meta_entry.status.meta_type), nca_name);

        paths.emplace_back(path);
    }

    auto source = std::make_shared<NcaSource>(m_meta.cs, m_entry.image, entries);
    dump::Dump(source, paths, [](Result){}, dump::DumpLocationFlag_All &~ dump::DumpLocationFlag_UsbS2S);
}

Result Menu::MountNcaFs() {
    const auto& e = GetEntry();

    FsFileSystemType type;
    R_TRY(GetFsFileSystemType(e.header.content_type, type));

    // get fs path from ncm.
    u64 program_id;
    fs::FsPath path;
    R_TRY(ncm::GetFsPathFromContentId(m_meta.cs, m_meta.key, e.content_id, &program_id, &path));

    // ensure that mounting worked.
    auto fs = std::make_shared<fs::FsNativeId>(program_id, type, path);
    R_TRY(fs->GetFsOpenResult());

    const filebrowser::FsEntry fs_entry{
        .name = "NCA",
        .root = "/",
        .type = filebrowser::FsType::Custom,
        .flags = filebrowser::FsEntryFlag_ReadOnly,
    };

    App::Push<filebrowser::Menu>(fs, fs_entry, "/");
    R_SUCCEED();
}

} // namespace sphaira::ui::menu::game::meta_nca
