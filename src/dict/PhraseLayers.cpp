#include "PhraseLayers.h"

// The phrase layer's load-time logic. Moved out of EngineImplLoad.cpp by line-range slicing, so the
// bodies below are byte-identical apart from four substitutions: the dictionary directory, the error slot,
// the reading normalizer and toSimp became parameters, and the signatures became free functions.
//
// Nothing here reaches for the engine -- everything arrives as an argument. That is what lets this file be
// compiled and tested with only the dict layer (AC-3/AC-4); tests/dict/selfcheck_main.cpp calls
// rebuildPhrases directly.

#include <algorithm>

namespace cpp_pinyin::v2
{
    namespace detail
    {

        bool loadUserDictLayer(PhraseLayers &phrases, const std::filesystem::path &path, bool clear,
                               const std::function<std::u16string(const std::u16string &)> &normalizeReading,
                               std::string &error) {
            if (clear) {
                // Only the USER layer is discarded. The built-in entries come back from
                // phrases_dict.txt in rebuildPhrases(), so clearing here can no longer drop
                // all 6026 built-in entries (defect C-21).
                phrases.user.clear();
                phrases.userStart.reset();
            }

            // The same loader that fills the map also records which characters begin a key,
            // so the window gate can never disagree with the map contents. Readings are normalized
            // by THIS dialect's hook (Mandarin: tone3 -> tone-marked; Cantonese: identity) -- a
            // hardcoded Mandarin normalizer used to mangle Cantonese user dictionaries.
            const bool ok = loadAdditionalDict(path, phrases.user, normalizeReading, ':', " ", &phrases.userStart);
            // The index rebuild is triggered by the engine-side shell (see PhraseLayers.h): the index
            // self-check calls findPhrase(), which is the query hot path and must stay in the engine.
            if (!ok && (clear || error.empty()))
                error = "failed to load user dict: " + path.string();
            return ok;
        }
        bool rebuildPhrases(PhraseLayers &phrases, const std::filesystem::path &dictDir,
                            const std::function<char16_t(char16_t)> &toSimp) {
            // Always rebuild from scratch: phrases_dict.txt is the authoritative built-in layer, and
            // rebuilding is what keeps stale user entries from surviving a clear. The rebuilt table is
            // assembled in a LOCAL and moved into phrases.merged only after every layer succeeded (Q2):
            // clearing merged first meant a failed built-in load left the engine with an empty phrase
            // layer while the caller still got a success back.
            decltype(phrases.merged) rebuilt;
            // The built-in window gate comes from phrases_map.txt, the companion of phrases_dict.txt, and
            // it belongs to the same rebuild: init() loads both, and refreshing the phrases without the
            // gate left the gate describing a different pair of files than the table it guards. A stale
            // gate fails silently -- test() says "no phrase starts here", so a phrase that IS in the table
            // never gets its window opened (Q4).
            decltype(phrases.builtinStart) rebuiltStart;
            const bool gateOk = loadDict(dictDir / "phrases_map.txt", rebuiltStart);
            const bool ok = loadDict(dictDir / "phrases_dict.txt", rebuilt, ':', ",");
            // C-04/D8: the hot path matches on the SIMPLIFIED characters, so a traditionally spelled user key could
            // never match -- a silent failure. Each user key's simplified spelling becomes an ALIAS, its first
            // character also entering the window gate (else the window never opens: C-03); the alias overrides a
            // BUILT-IN entry (user layer beats built-in) but loses to a real user entry. Measured (.tmp/
            // check_userdict.py): no shipped user_dict.txt has such a key, so the gate stays byte-identical.
            std::unordered_map<uint64_t, std::vector<std::u16string>> aliasMap;
            // Window-gate bits are applied only after the rebuild succeeded, so a failed load leaves
            // the previous gate exactly as it was.
            std::vector<uint16_t> gateChars;
            if (!phrases.user.empty()) {
                // Collisions are real, not theoretical: trans_word.txt has 180 groups where several
                // distinct traditional characters simplify to the SAME character (別/彆 -> 别,
                // 併/並/竝 -> 并, 發/髮 -> 发 ...), so two user keys can collapse onto one alias key and
                // phrases.user iteration order (unordered_map!) must not decide which reading survives.
                // The tie-break is stated in terms of the ORIGINAL key -- sorting by the alias key would
                // not separate them, because a collision means the alias keys are equal.
                struct Alias {
                    uint64_t aliasKey;
                    uint64_t sourceKey;
                    std::vector<std::u16string> readings;
                };
                std::vector<Alias> aliases;
                for (const auto &kv : phrases.user) {
                    const size_t len = phraseKeyLength(kv.first);
                    if (len < 2)
                        continue;  // kAttempts only probes lengths 2..4. Keys longer than 4 characters were
                                   // already truncated to their LAST 4 by encodePhrasesKey, so
                                   // phraseKeyLength never reports more than 4 -- there is no "len > 4" case.
                    std::u16string key(len, u'\0');
                    bool changed = false;
                    for (size_t i = 0; i < len; ++i) {
                        const char16_t ch = static_cast<char16_t>((kv.first >> (16 * (len - 1 - i))) & 0xFFFFu);
                        key[i] = toSimp(ch);
                        changed = changed || key[i] != ch;
                    }
                    if (!changed)
                        continue;
                    const uint64_t aliasKey = encodePhrasesKey(key.data(), len);
                    if (phrases.user.count(aliasKey) != 0)
                        continue;  // an EXPLICIT user entry under the simplified key wins over an alias
                    aliases.push_back(Alias{aliasKey, kv.first, kv.second});
                    gateChars.push_back(static_cast<uint16_t>(key[0]));
                }
                std::sort(aliases.begin(), aliases.end(), [](const Alias &a, const Alias &b)
                          { return a.sourceKey != b.sourceKey ? a.sourceKey < b.sourceKey : a.aliasKey < b.aliasKey; });
                uint64_t lastAlias = 0;
                bool haveLast = false;
                for (auto &alias : aliases) {
                    if (haveLast && alias.aliasKey == lastAlias)
                        continue;  // deterministic winner: the smallest original key, see the sort above
                    lastAlias = alias.aliasKey;
                    haveLast = true;
                    aliasMap.emplace(alias.aliasKey, std::move(alias.readings));
                }
            }
            // Aliases are merged FIRST and live only in this local map, never in phrases.user. Storing
            // them in the user layer made an alias look like an EXPLICIT user entry on the next rebuild:
            // that silently broke the documented rule that the newly loaded file wins for duplicate keys
            // (docs/API_zh.md), because a later loadUserDict(path, false) could no longer overwrite an
            // alias, and it let the collision winner drift with the order the files were loaded in.
            // Keeping them out also leaves phrases.user exactly what the caller supplied, which is the
            // authoritative input for every later rebuild.
            for (const auto &kv : aliasMap)
                rebuilt[kv.first] = kv.second;
            // Merge the user layer on top: the assignment wins for duplicate keys, which is
            // the same precedence the previous user-then-builtin lookup had. Because the user layer is
            // applied after the aliases, a real entry always beats an alias -- the documented precedence.
            for (const auto &kv : phrases.user)
                rebuilt[kv.first] = kv.second;
            if (!ok || !gateOk)
                return false;  // phrases was never touched: the caller keeps the layer it already had
            phrases.merged = std::move(rebuilt);
            phrases.builtinStart = std::move(rebuiltStart);
            for (const uint16_t ch : gateChars)
                phrases.userStart.set(ch);
            // Index rebuild deliberately not done here -- the engine shell calls it after this returns.
            return true;
        }
    }  // namespace detail
}  // namespace cpp_pinyin::v2
