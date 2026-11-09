#include "level.hpp"

#include <cstdio>
#include <vector>

#include "tiles.hpp"

namespace bc {

bool Level::loadFromMemory(const std::uint8_t* data, std::size_t size) {
    if (!data || size < kFileSize) return false;

    const std::uint32_t payload = (std::uint32_t(data[0]) << 24) |
                                  (std::uint32_t(data[1]) << 16) |
                                  (std::uint32_t(data[2]) << 8) |
                                   std::uint32_t(data[3]);
    if (payload != Grid::kCells) return false;

    for (int i = 0; i < Grid::kCells; ++i) grid.cells[i] = data[kHeaderSize + i];

    carrotTotal = 0;
    eggTotal    = 0;
    start       = Coord{0, 0};
    exit        = Coord{0, 0};

    for (int i = 0; i < Grid::kCells; ++i) {
        switch (grid.cells[i]) {
            case T_CARROT: ++carrotTotal; break;
            case T_GRASS:  ++eggTotal;    break;
            case T_START:  start = Coord{i % Grid::W, i / Grid::W}; break;
            case T_EXIT:   exit  = Coord{i % Grid::W, i / Grid::W}; break;
            default: break;
        }
    }
    return true;
}

bool Level::loadFromFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    std::vector<std::uint8_t> buf(kFileSize);
    const std::size_t got = std::fread(buf.data(), 1, kFileSize, f);
    std::fclose(f);

    return got == kFileSize && loadFromMemory(buf.data(), got);
}

std::string Level::pathFor(Mode mode, int index, const std::string& root) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s/level/%s%02d.blm", root.c_str(),
                  mode == Mode::Normal ? "normal" : "egg", index + 1);
    return std::string(name);
}

}  // namespace bc
