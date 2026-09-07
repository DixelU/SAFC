#define NOMINMAX
#include <Windows.h>

#include "../imgui/preferences.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct registry_key
{
    HKEY value{};
    ~registry_key() { if (value) RegCloseKey(value); }
};

class isolated_user_registry
{
public:
    isolated_user_registry()
        : path_(L"Software\\SAFC-ImGuiPreferenceTest-" + std::to_wstring(GetCurrentProcessId()))
    {
        try
        {
            require(RegOpenCurrentUser(KEY_ALL_ACCESS, &real_user_) == ERROR_SUCCESS, "Open current user registry handle");
            DWORD disposition{};
            require(RegCreateKeyExW(real_user_, path_.c_str(), 0, nullptr, REG_OPTION_VOLATILE,
                KEY_ALL_ACCESS, nullptr, &isolated_, &disposition) == ERROR_SUCCESS, "Create isolated volatile test key");
            require(disposition == REG_CREATED_NEW_KEY, "Refuse to reuse an existing test registry subtree");
            created_ = true;
            require(RegOverridePredefKey(HKEY_CURRENT_USER, isolated_) == ERROR_SUCCESS, "Override HKCU inside the test process");
            overridden_ = true;
        }
        catch (...) { restore(); throw; }
    }

    ~isolated_user_registry() { restore(); }
    isolated_user_registry(const isolated_user_registry&) = delete;
    isolated_user_registry& operator=(const isolated_user_registry&) = delete;

    bool restore() noexcept
    {
        if (overridden_)
        {
            if (RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) != ERROR_SUCCESS) return false;
            overridden_ = false;
        }
        if (isolated_) { RegCloseKey(isolated_); isolated_ = nullptr; }
        bool ok = true;
        // real_user_ was opened before the override. Only the newly created PID
        // subtree is ever deleted; the user's actual Software\SAFC is untouched.
        if (created_)
        {
            const auto result = RegDeleteTreeW(real_user_, path_.c_str());
            ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
            if (ok) created_ = false;
        }
        if (!created_ && real_user_) { RegCloseKey(real_user_); real_user_ = nullptr; }
        return ok;
    }

private:
    std::wstring path_;
    HKEY real_user_{}, isolated_{};
    bool created_{}, overridden_{};
};

void dword(HKEY key, const wchar_t* name, DWORD value)
{
    require(RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS,
        "Write isolated DWORD fixture");
}

DWORD read_dword(HKEY key, const wchar_t* name)
{
    DWORD value{}, type{}, bytes = sizeof(value);
    require(RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) == ERROR_SUCCESS &&
        type == REG_DWORD && bytes == sizeof(value), "Read isolated DWORD value");
    return value;
}

using registry_values = std::map<std::wstring, std::pair<DWORD, std::vector<BYTE>>>;
registry_values values(HKEY key)
{
    DWORD count{}, maximum_name{}, maximum_data{};
    require(RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &count, &maximum_name, &maximum_data, nullptr, nullptr) == ERROR_SUCCESS, "Query isolated registry values");
    registry_values result;
    for (DWORD index = 0; index < count; ++index)
    {
        std::wstring name(maximum_name + 1, L'\0');
        std::vector<BYTE> data(maximum_data);
        DWORD name_size = static_cast<DWORD>(name.size()), data_size = static_cast<DWORD>(data.size()), type{};
        require(RegEnumValueW(key, index, name.data(), &name_size, nullptr, &type, data.data(), &data_size) == ERROR_SUCCESS,
            "Enumerate isolated registry value");
        name.resize(name_size); data.resize(data_size);
        result.emplace(std::move(name), std::make_pair(type, std::move(data)));
    }
    return result;
}
}

int main()
try
{
    isolated_user_registry isolated;
    safc::imgui_ui::preferences_store store;
    require(safc::imgui_ui::application_preferences{}.automatic_updates, "New application preferences enable updates");
    require(store.load().automatic_updates, "Missing legacy registry key enables updates by default");
    {
        registry_key key;
        // Existing volatile children let the unmodified production Create() call
        // open them without attempting nonvolatile creation under a volatile key.
        require(RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SAFC", 0, nullptr, REG_OPTION_VOLATILE,
            KEY_ALL_ACCESS, nullptr, &key.value, nullptr) == ERROR_SUCCESS, "Create isolated legacy SAFC fixture");
        store.save_update_preference(false);
        require(read_dword(key.value, L"AUTOUPDATECHECK") == 0 && values(key.value).size() == 1,
            "Saving only the update preference must not write other defaults");
        require(!store.load().automatic_updates, "Import legacy AUTOUPDATECHECK=0");
        dword(key.value, L"AUTOUPDATECHECK", 1);
        require(store.load().automatic_updates, "Import legacy AUTOUPDATECHECK=1");
        require(RegDeleteValueW(key.value, L"AUTOUPDATECHECK") == ERROR_SUCCESS, "Remove isolated update preference");
        require(store.load().automatic_updates, "Missing flag in an existing legacy key defaults to enabled");

        dword(key.value, L"PLAYER_RENDER_WIDTH", 3840);
        dword(key.value, L"AS_THREADS_COUNT", 17);
        const std::wstring bank = L"D:\\Banks\\Keep this bank.sf2";
        require(RegSetValueExW(key.value, L"SYNCORE_BANK_PATH", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(bank.c_str()), static_cast<DWORD>((bank.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS,
            "Write isolated bank fixture");
        const BYTE opaque[] = {0x00, 0x91, 0xff, 0x05};
        require(RegSetValueExW(key.value, L"FUTURE_SETTING", 0, REG_BINARY, opaque, sizeof(opaque)) == ERROR_SUCCESS,
            "Write isolated unknown preference fixture");
        const auto unrelated = values(key.value);
        for (const bool enabled : {false, true})
        {
            store.save_update_preference(enabled);
            require(read_dword(key.value, L"AUTOUPDATECHECK") == static_cast<DWORD>(enabled), "Persist toggled update preference");
            auto after = values(key.value);
            after.erase(L"AUTOUPDATECHECK");
            require(after == unrelated, "Update-only save changed unrelated registry values or their types");
            require(store.load().automatic_updates == enabled, "Reload toggled update preference");
        }

        auto preferences = store.load();
        preferences.video.width = 1920;
        for (const bool enabled : {false, true})
        {
            preferences.automatic_updates = enabled;
            store.save(preferences);
            require(read_dword(key.value, L"AUTOUPDATECHECK") == static_cast<DWORD>(enabled), "Full preferences save must persist update flag");
            const auto loaded = store.load();
            require(loaded.automatic_updates == enabled && loaded.video.width == 1920 && loaded.sound_bank == bank,
                "Full preferences reload preserves the update flag and other settings");
        }
    }
    require(isolated.restore(), "Restore HKCU override and remove only the private test subtree");
    std::cout << "PASS: default/legacy update preferences, isolated flag writes, full save, and private registry cleanup\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
