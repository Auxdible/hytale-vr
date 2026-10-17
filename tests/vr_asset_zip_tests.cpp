#include "vr_item_assets.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

std::string as_text(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: hytale_vr_asset_zip_tests <fixture.zip>\n";
        return 2;
    }
    const std::string narrow_path = argv[1];
    const std::wstring zip_path(narrow_path.begin(), narrow_path.end());
    bool success = true;
    std::vector<uint8_t> bytes;
    std::string error;

    success &= check(hytalevr::read_vr_asset_by_suffix(
                         zip_path, "Characters\\Player_Textures\\Player_Greyscale.png",
                         bytes, error) &&
                         as_text(bytes) == "player-texture",
                     "backslash relative paths must resolve under a Common/ prefix");

    bytes.clear();
    success &= check(hytalevr::read_vr_asset_by_suffix(
                         zip_path, "CharacterCreator/GradientSets.json", bytes, error) &&
                         as_text(bytes) == "{\"gradients\":[]}",
                     "the shortest suffix match must win and partial folder names must not match");

    bytes.clear();
    error.clear();
    success &= check(!hytalevr::read_vr_asset_by_suffix(
                         zip_path, "CharacterCreator/Missing.json", bytes, error) &&
                         !error.empty(),
                     "a missing asset must fail with an error");

    return success ? 0 : 1;
}
