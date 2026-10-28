#include "../src/SettingsCore.h"
#include <cassert>
#include <cstdio>

int wmain(int argc, wchar_t **argv) {
    assert(argc == 2);
    auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    auto path = (directory / L"settings.ini").wstring();
    SettingsStore store(path);
    assert(store.Migrate());
    assert(store.Get(L"Schema")[L"Version"] == L"4");
    v4::Values values;
    assert(values.Bool(L"PeekPlayingOnly"));
    values.data[L"Radius"] = L"28";
    values.data[L"Theme"] = L"Paper";
    assert(v4::SaveValues(values, path));
    assert(v4::ReadValues(path).data == values.data);
    auto exported = (directory / L"export.ini").wstring();
    assert(store.Export(exported));
    assert(v4::SaveValues(v4::Values{}, path));
    assert(store.Import(exported));
    assert(v4::ReadValues(path).data == values.data);
    values.data[L"Radius"] = L"-1";
    assert(!v4::SaveValues(values, path));
    assert(v4::ReadValues(path).Int(L"Radius") == 28);
    for (const auto &setting : v4::Schema) {
        assert(v4::Valid(setting, setting.value));
        assert(!v4::Valid(setting, L"bad\nvalue"));
    }
    assert(!v4::Valid(*v4::Find(L"Accent"), L"XYZ123"));
    assert(!v4::Valid(*v4::Find(L"Tabs"), L"Controls;Controls"));
    auto corrupt = directory / L"corrupt.ini";
    {
        std::ofstream file(corrupt);
        file << "[broken\n";
    }
    SettingsStore damaged(corrupt.wstring());
    assert(damaged.Migrate());
    assert(damaged.Get(L"Schema")[L"Version"] == L"4");
    bool backup = false;
    for (const auto &file : std::filesystem::directory_iterator(directory))
        if (file.path().filename().wstring().starts_with(L"corrupt.ini.corrupt-"))
            backup = true;
    assert(backup);
    std::puts("PASS V4 schema defaults/bounds, export-reset-import, atomic invalid-value rejection and corrupt-file "
              "preservation");
}
