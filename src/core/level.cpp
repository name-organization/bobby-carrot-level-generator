#include "level.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "tilechar.hpp"
#include "tiles.hpp"

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

    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
            switch (grid.at(x, y)) {
                case T_CARROT: ++carrotTotal; break;
                case T_GRASS:  ++eggTotal;    break;
                case T_START:  start = Coord{x, y}; break;
                case T_EXIT:   exit  = Coord{x, y}; break;
                default: break;
            }
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

    // Every line but a comment is a row, blank ones included: with path
    // written as a space, a row of open ground looks exactly like an empty
    // line, so skipping blanks would quietly delete it. The level is as high
    // as there are rows and as wide as the longest one.
    struct Row {
        std::string_view text;
        int lineNo;
    };
    std::vector<Row> rows;
    int width = 0;
    for (std::string_view raw : lines) {
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

        if (static_cast<int>(line.size()) > Grid::kMaxW) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "line %d: expected at most %d characters, got %d",
                          lineNo, Grid::kMaxW, static_cast<int>(line.size()));
            fail(error, buf);
            return false;
        }
        rows.push_back({line, lineNo});
        width = std::max(width, static_cast<int>(line.size()));
    }

    if (rows.empty() || width == 0) {
        fail(error, "no map rows");
        return false;
    }
    if (static_cast<int>(rows.size()) > Grid::kMaxH) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "expected at most %d rows, got %d", Grid::kMaxH,
                      static_cast<int>(rows.size()));
        fail(error, buf);
        return false;
    }
    parsed.w = width;
    parsed.h = static_cast<int>(rows.size());

    for (const Row& r : rows) {
        // A short row is the rest of it being path.
        for (int x = static_cast<int>(r.text.size()); x < width; ++x) parsed.at(x, row) = T_GROUND;

        for (int x = 0; x < static_cast<int>(r.text.size()); ++x) {
            const int tile = charToTile(r.text[x]);
            if (tile < 0) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "line %d, column %d: unknown character '%c'", r.lineNo,
                              x + 1, r.text[x]);
                fail(error, buf);
                return false;
            }
            parsed.at(x, row) = static_cast<std::uint8_t>(tile);
        }
        ++row;
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
    out.reserve(static_cast<std::size_t>((grid.w + 1) * grid.h) + title.size() + 8);

    if (!title.empty()) {
        out += kCommentChar;
        out += ' ';
        out.append(title);
        out += '\n';
    }

    for (int y = 0; y < grid.h; ++y) {
        for (int x = 0; x < grid.w; ++x) {
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
    char name[32];
    std::snprintf(name, sizeof(name), "%s%02d.lvl",
                  mode == Mode::Normal ? "normal" : "egg", index + 1);
    return root + "/level/" + name;
}

}  // namespace bc
