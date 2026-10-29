#include "dict/DictManifest.h"

#include <array>
#include <fstream>
#include <iterator>

namespace cpp_pinyin::v2
{
    namespace detail
    {
        namespace
        {
            // "1.2.3" -> {1,2,3}. Missing trailing components count as 0, so "2" and "2.0.0" make the same
            // claim. Anything else (letters, an empty component, a fourth component) is not a version.
            bool parseVersion(std::string_view text, std::array<int, 3> &out) {
                out = {0, 0, 0};
                int index = 0;
                int value = -1;  // -1 = this component has no digit yet
                for (const char c : text) {
                    if (c == '.') {
                        if (value < 0 || index == 2)
                            return false;
                        out[static_cast<std::size_t>(index++)] = value;
                        value = -1;
                        continue;
                    }
                    if (c < '0' || c > '9')
                        return false;
                    value = (value < 0 ? 0 : value) * 10 + (c - '0');
                    if (value > 1000000)  // absurd component: refuse instead of overflowing
                        return false;
                }
                if (value < 0)
                    return false;
                out[static_cast<std::size_t>(index)] = value;
                return true;
            }

            // The manifest is machine-written by this repository's generator, where the key appears as
            // `"min_library": "2.0.0"`. A minimal scan is deliberate: this library has no JSON dependency,
            // and the only thing needed is ONE string out of a file whose shape the generator owns. An
            // absent key (a hand-built or 1.x-heritage directory) means "no requirement", which keeps
            // those directories usable.
            bool readMinLibrary(const std::filesystem::path &manifest, std::string &out) {
                std::ifstream in(manifest, std::ios::binary);
                if (!in.is_open())
                    return false;
                const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                constexpr std::string_view key = "\"min_library\"";
                std::size_t at = text.find(key);
                if (at == std::string::npos)
                    return false;
                at = text.find('"', at + key.size());
                if (at == std::string::npos)
                    return false;
                const std::size_t close = text.find('"', at + 1);
                if (close == std::string::npos)
                    return false;
                out = text.substr(at + 1, close - at - 1);
                return true;
            }
        }  // namespace

        ManifestCheck checkDictManifest(const std::filesystem::path &dictDir, const std::filesystem::path &subdir,
                                        int supportedFormat, std::string_view libraryVersion) {
            const std::filesystem::path dir = dictDir / subdir;
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return {};  // Not this check's business: the layer that opens the tables reports that.
            std::ifstream marker(dir / "format.txt");
            if (!marker.is_open()) {
                if (std::filesystem::exists(dir / "dialect.json", ec))
                    return {Code::Load,
                            "dictionary format marker is missing in " + dir.string() +
                                ": a directory carrying dialect.json is written by the generator and must carry "
                                "format.txt (supported data format: " +
                                std::to_string(supportedFormat) + ")"};
                return {};
            }
            std::string text;
            std::getline(marker, text);
            while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
                text.pop_back();
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                return {Code::Load,
                        "dictionary format marker in " + dir.string() + " is not a number: found '" + text +
                            "', supported data format: " + std::to_string(supportedFormat)};
            // Digits only, so "newer than supported" is decided without parsing: a single digit above the
            // supported one, or any multi-digit number.
            if (text.size() > 1 || text[0] > static_cast<char>('0' + supportedFormat))
                return {Code::Load,
                        "dictionary format marker in " + dir.string() + " declares data format " + text +
                            ", newer than this library supports (" + std::to_string(supportedFormat) + ")"};

            // EMPTY TABLES ARE A LOAD FAILURE, NOT AN EMPTY DICTIONARY.
            // The producer refuses to write such a table at all now; this is the consumer half, for data
            // that arrives from elsewhere (a packaging step, a copy, a wheel built from a bad checkout).
            // Only `word.txt` is required to be non-empty: it is the one table a usable dictionary cannot
            // do without, while the phrase tables are legitimately small in a minimal dialect.
            {
                const std::filesystem::path words = dir / "word.txt";
                std::error_code sizeEc;
                const auto bytes = std::filesystem::file_size(words, sizeEc);
                if (!sizeEc && bytes == 0)
                    return {Code::Load,
                            "dictionary in " + dir.string() +
                                " has a zero-byte word.txt: the marker says this generation is complete, so an "
                                "empty table means the data was truncated, not that the dialect has no readings"};
            }

            std::string minLibrary;
            if (readMinLibrary(dir / "dialect.json", minLibrary) && !libraryVersion.empty()) {
                std::array<int, 3> need{};
                std::array<int, 3> have{};
                if (!parseVersion(minLibrary, need))
                    return {Code::Load,
                            "dialect.json's min_library in " + dir.string() + " is not a version: found '" +
                                minLibrary + "'"};
                // THE LIBRARY'S OWN STRING IS HELD TO THE SAME RULE. It used to be
                // `if (parseVersion(libraryVersion, have) && need > have)`: a version string that did not parse
                // simply skipped the comparison, so a build that misdefined its own version could install data
                // asking for a newer library and never hear about it -- while the data half of the same handshake
                // (the line above) is a loud refusal. EMPTY still skips, which DictManifest.h documents as "a
                // build that did not define it does not guess"; a string that IS defined and is not a version is
                // a build fault and fails like one.
                if (!parseVersion(libraryVersion, have))
                    return {Code::Load,
                            "this library's version string is not a version: found '" + std::string(libraryVersion) +
                                "', and " + dir.string() + " asks for min_library " + minLibrary +
                                " -- refusing to skip the comparison"};
                if (need > have)
                    return {Code::DictIncompatible,
                            "dictionary in " + dir.string() + " requires library >= " + minLibrary +
                                ", this library is " + std::string(libraryVersion)};
            }
            return {};
        }
    }  // namespace detail
}  // namespace cpp_pinyin::v2
