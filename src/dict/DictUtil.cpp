#include "DictUtil.h"

#include "Utf8Conv.h"

#include <bitset>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cpp-pinyin2/core/U16Str.h"

#include "EmbeddedTables.h"

namespace cpp_pinyin::v2
{
    // The ONE place a table is opened. Two sources share this face so the loaders below never learn which one
    // they got (D3 of docs/DECISIONS.md section 4.1: the file on disk always wins over the embedded copy):
    //   * the file on disk, whenever that path EXISTS -- an embedded copy must never shadow a real file, or a
    //     stale resource directory would be silently ignored;
    //   * otherwise, in a build that embeds the dictionaries, the copy compiled into the library.
    // When neither applies the stream is not good(), which is exactly what every loader already reports as
    // "could not load" with the file named in the engine's error text.
    static std::unique_ptr<std::istream> openFile(const std::filesystem::path &dict_dir) {
        std::error_code ec;
        if (std::filesystem::exists(dict_dir, ec))
            return std::make_unique<std::ifstream>(dict_dir);
        const detail::EmbeddedTable embedded = detail::findEmbeddedTable(dict_dir);
        if (embedded.data != nullptr)
            return std::make_unique<std::istringstream>(std::string(embedded.data, embedded.size));
        return std::make_unique<std::ifstream>(dict_dir);
    }

    // Split line into key/value by separator, skipping leading/trailing whitespace.
    // Returns false if line is empty or has no separator.
    static bool splitLine(const std::string &line, const char sep, std::string &key, std::string &value) {
        // Find first non-whitespace
        const size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return false;
        const size_t end = line.find_last_not_of(" \t\r\n") + 1;

        // Find separator within trimmed range
        const size_t sepPos = line.find(sep, start);
        if (sepPos == std::string::npos || sepPos >= end)
            return false;

        key = line.substr(start, sepPos - start);
        value = line.substr(sepPos + 1, end - sepPos - 1);
        return !key.empty();
    }

    // There used to be a `processFile<>` template here (the key/value double-handler version).
    // Measured across the whole repository: zero call sites -- all three array loaders go through
    // `processFileGeneric` below -- so it was dead code and was deleted.

    template <typename InsertFunc>
    static bool processFileGeneric(std::istream &file, const char &sep1, InsertFunc insertFunc) {
        if (!file.good()) {
            return false;
        }

        std::string line, key, value;
        size_t parsed = 0;
        size_t skippedNonBlank = 0;
        while (std::getline(file, line)) {
            if (splitLine(line, sep1, key, value)) {
                insertFunc(key, value);
                parsed++;
            } else if (line.find_first_not_of(" \t\r\n") != std::string::npos) {
                // C-05: a line with content that yields no entry used to be dropped in silence, so a
                // truncated or mis-separated dictionary loaded "successfully" while missing rows.
                // Measured before changing this: all ten shipped dictionaries have ZERO such lines
                // (every non-blank line contains the ':' separator), so the only rows this can newly
                // reject are rows that were already unusable. Third-party tables that carry comment
                // lines will now fail loudly -- with the file name in lastError() -- instead of
                // silently losing whatever they meant to contribute.
                skippedNonBlank++;
            }
        }
        // A file that opens but yields not a single entry is a malformed dictionary, not a valid
        // empty one: reporting success there produced an engine that was "ready" with no knowledge
        // at all. Reported as a load failure, the file name shows up in lastError().
        // Since C-05 the same reasoning covers partially-broken files: "some rows parsed" is not
        // "the file is fine" when other rows silently produced nothing.
        return parsed > 0 && skippedNonBlank == 0;
    }

    static std::vector<std::string> split(const std::string &s, const std::string &delimiter) {
        std::vector<std::string> tokens;
        if (delimiter.empty()) {
            for (char c : s) {
                tokens.emplace_back(1, c);
            }
        } else {
            std::string::size_type start = 0;
            std::string::size_type end = s.find(delimiter);
            while (end != std::string::npos) {
                tokens.push_back(s.substr(start, end - start));
                start = end + delimiter.size();
                end = s.find(delimiter, start);
            }
            tokens.push_back(s.substr(start));
        }
        return tokens;
    }

    // --- Direct-indexed array loaders ---

    bool loadDict(const std::filesystem::path &dict_dir, std::array<char16_t, CHAR16_TABLE_SIZE> &table,
                  const char &sep1) {
        auto file = openFile(dict_dir);
        return processFileGeneric(*file, sep1,
                                  [&table](const std::string &key, const std::string &value)
                                  {
                                      const char16_t k = toU16(key)[0];
                                      const char16_t v = toU16(value)[0];
                                      table[static_cast<uint16_t>(k)] = v;
                                  });
    }

    bool loadDict(const std::filesystem::path &dict_dir, std::bitset<CHAR16_TABLE_SIZE> &bits, const char &sep1) {
        auto file = openFile(dict_dir);
        return processFileGeneric(*file, sep1,
                                  [&bits](const std::string &key, const std::string &)
                                  {
                                      const char16_t k = toU16(key)[0];
                                      bits.set(static_cast<uint16_t>(k));
                                  });
    }

    bool loadDict(const std::filesystem::path &dict_dir, std::array<uint16_t, CHAR16_TABLE_SIZE> &index,
                  std::vector<std::vector<std::u16string>> &values, const char &sep1, const std::string &sep2) {
        auto file = openFile(dict_dir);
        if (values.empty()) {
            values.emplace_back();
            values.reserve(20000);
        }

        return processFileGeneric(*file, sep1,
                                  [&index, &values, &sep2](const std::string &key, const std::string &value)
                                  {
                                      const char16_t k = toU16(key)[0];
                                      std::vector<std::u16string> u16list;
                                      for (const auto &str : split(value, sep2)) {
                                          if (!str.empty())
                                              u16list.emplace_back(toU16(str));
                                      }
                                      values.emplace_back(std::move(u16list));
                                      index[static_cast<uint16_t>(k)] = static_cast<uint16_t>(values.size() - 1);
                                  });
    }

    // --- uint64_t-keyed loaders (for phrase dicts with 2-4 char keys) ---

    bool loadDict(const std::filesystem::path &dict_dir,
                  std::unordered_map<uint64_t, std::vector<std::u16string>> &resultMap, const char &sep1,
                  const std::string &sep2) {
        auto file = openFile(dict_dir);
        if (resultMap.empty())
            resultMap.reserve(7000);

        if (!file->good()) {
            return false;
        }

        std::string line, key, value;
        size_t parsed = 0;
        // Same predicate as processFileGeneric, and it matters most HERE: this overload fills the merged
        // phrase layer (phrases_dict.txt) and the user layer, where a dropped row used to be invisible -- the very
        // same "# note" line fails the load of phrases_dict.txt but left this table "successfully"
        // loaded, so one file's corruption was loud and the other's was silent. Measured before
        // landing: every shipped dictionary has zero such rows (10 files / 73,032 rows, plus 1122
        // third-party candidate corpora), so this cannot turn a good file into a rejected one here.
        size_t skippedNonBlank = 0;
        while (std::getline(*file, line)) {
            if (splitLine(line, sep1, key, value)) {
                const auto u16key = toU16(key);
                std::vector<std::u16string> u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(toU16(str));
                }
                resultMap[encodePhrasesKey(u16key)] = std::move(u8strlist);
                parsed++;
            } else if (line.find_first_not_of(" \t\r\n") != std::string::npos) {
                skippedNonBlank++;
            }
        }
        // See processFileGeneric: "opened but parsed nothing" is a failure, not an empty success.
        return parsed > 0 && skippedNonBlank == 0;
    }

    bool loadAdditionalDict(const std::filesystem::path &dict_dir,
                            std::unordered_map<uint64_t, std::vector<std::u16string>> &resultMap,
                            const std::function<std::u16string(const std::u16string &pinyin)> &normalizeReading,
                            const char &sep1, const std::string &sep2, std::bitset<CHAR16_TABLE_SIZE> *phraseStartOut) {
        auto file = openFile(dict_dir);

        if (!file->good()) {
            return false;
        }

        std::string line, key, value;
        while (std::getline(*file, line)) {
            if (splitLine(line, sep1, key, value)) {
                const auto u16key = toU16(key);
                if (phraseStartOut && !u16key.empty())
                    phraseStartOut->set(static_cast<uint16_t>(u16key[0]));
                std::vector<std::u16string> u8strlist;
                for (const auto &str : split(value, sep2)) {
                    if (!str.empty())
                        u8strlist.emplace_back(normalizeReading(toU16(str)));
                }
                resultMap[encodePhrasesKey(u16key)] = std::move(u8strlist);
            }
        }
        // Deliberately NOT the `parsed > 0` rule processFileGeneric uses: an EMPTY user dictionary is
        // legal here. data/dialects/cantonese/user_dict.txt ships as a 0-byte file, so treating "no
        // entries" as a malformed dictionary would make the built-in Cantonese fail to load. The rule
        // this file does follow is C-05's, one level up: rows that have content but yield no entry make
        // the TABLE loaders fail, while a hand-written user file stays tolerant by design.
        return true;
    }
}  // namespace cpp_pinyin::v2
