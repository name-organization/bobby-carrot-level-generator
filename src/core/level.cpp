#include "level.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "tilechar.hpp"
#include "tiles.hpp"
#include "base64.h"

namespace bc {
namespace {

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

// Only CR goes: trailing spaces are ground, so they carry meaning now. They are
// still allowed to go missing -- a short row is padded out below -- because
// plenty of editors strip trailing whitespace on save and a row of path would
// otherwise come back truncated.
std::string_view trimCarriageReturn(std::string_view line) {
    while (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

}  // namespace

void Level::rescan() {
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
}

bool Level::loadFromText(std::string_view text, std::string* error) {
    Grid parsed{};
    int row = 0;
    int lineNo = 0;
    std::string title;

    // Split first. A file that ends with a newline must not yield a phantom
    // empty line after the last row: now that a blank line means a row of
    // path, that phantom would pad a 15-row file out to a valid-looking 16 and
    // hide the missing row.
    std::vector<std::string_view> lines;
    for (std::size_t pos = 0; pos <= text.size();) {
        const std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) {
            if (pos < text.size()) lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, eol - pos));
        pos = eol + 1;
    }

    for (std::string_view raw : lines) {
        if (row >= Grid::H) break;
        ++lineNo;
        std::string_view line = trimCarriageReturn(raw);

        if (!line.empty() && line.front() == kCommentChar) {
            if (title.empty()) {
                std::string_view rest = line.substr(1);
                while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
                while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) {
                    rest.remove_suffix(1);
                }
                title.assign(rest);
            }
            continue;
        }

        // Every remaining line is a row, blank ones included: with path written
        // as a space, a row of open ground looks exactly like an empty line, so
        // skipping blanks would quietly delete it.
        if (line.size() > Grid::W) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "line %d: expected at most %d characters, got %d",
                          lineNo, Grid::W, static_cast<int>(line.size()));
            fail(error, buf);
            return false;
        }

        // A short row is the rest of it being path.
        for (int x = static_cast<int>(line.size()); x < Grid::W; ++x) {
            parsed.cells[row * Grid::W + x] = T_GROUND;
        }

        for (int x = 0; x < static_cast<int>(line.size()); ++x) {
            const int tile = charToTile(line[x]);
            if (tile < 0) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "line %d, column %d: unknown character '%c'", lineNo,
                              x + 1, line[x]);
                fail(error, buf);
                return false;
            }
            parsed.cells[row * Grid::W + x] = static_cast<std::uint8_t>(tile);
        }
        ++row;
    }

    if (row != Grid::H) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "expected %d rows, got %d", Grid::H, row);
        fail(error, buf);
        return false;
    }

    grid = parsed;
    name = std::move(title);
    rescan();
    return true;
}

bool Level::loadFromFile(const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        fail(error, path + ": cannot open");
        return false;
    }

    std::string text;
    char buf[4096];
    while (const std::size_t n = std::fread(buf, 1, sizeof(buf), f)) {
        text.append(buf, n);
    }
    std::fclose(f);

    std::string inner;
    if (!loadFromText(text, &inner)) {
        fail(error, path + ": " + inner);
        return false;
    }
    return true;
}

std::string Level::toText(std::string_view title) const {
    std::string out;
    out.reserve(Grid::kCells + Grid::H + title.size() + 8);

    if (!title.empty()) {
        out += kCommentChar;
        out += ' ';
        out.append(title);
        out += '\n';
    }

    for (int y = 0; y < Grid::H; ++y) {
        for (int x = 0; x < Grid::W; ++x) {
            out += tileToChar(grid.at(x, y));
        }
        out += '\n';
    }
    return out;
}

bool Level::saveToFile(const std::string& path, std::string_view title) const {
    const std::string text = toText(title);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return written == text.size();
}

std::string Level::pathFor(Mode mode, int index, const std::string& root) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s/level/%s%02d.lvl", root.c_str(),
                  mode == Mode::Normal ? "normal" : "egg", index + 1);
    return std::string(name);
}

std::string Level::encode() const {
    std::string flattenGrid;
    flattenGrid.reserve(grid.cells.size());

    for (auto cell : grid.cells) {
        flattenGrid += static_cast<char>(cell);
    }

    return base64_encode(flattenGrid);
}

bool Level::decode(const std::string& base64Str, std::string* error) {
    std::string decoded;
    try {
        decoded = base64_decode(base64Str);
    } catch (const std::exception& e) {
        fail(error, std::string("base64: ") + e.what());
        return false;
    }

    if (decoded.size() != static_cast<std::size_t>(Grid::kCells)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "level payload: expected %d bytes, got %zu",
                      Grid::kCells, decoded.size());
        fail(error, buf);
        return false;
    }

    Grid parsed{};
    for (int i = 0; i < Grid::kCells; ++i) {
        const auto tile = static_cast<std::uint8_t>(decoded[i]);
        if (tile >= kTileCount) {
            char buf[80];
            std::snprintf(buf, sizeof(buf), "level payload: unknown tile %u at cell %d",
                          static_cast<unsigned>(tile), i);
            fail(error, buf);
            return false;
        }
        parsed.cells[i] = tile;
    }

    grid = parsed;
    rescan();
    return true;
}

}  // namespace bc
