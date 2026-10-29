#include "EngineImpl.h"
#include "Utf8Conv.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <utility>
#include <vector>

#include <cpp-pinyin2/core/G2pglobal.h>

namespace cpp_pinyin::v2
{
    // Construction / destruction

    EngineImpl::EngineImpl(std::filesystem::path dictDir, ToneConverter toneConverter)
        // The initialisation order must match the member declaration order (m_dictDir before
        // m_toneConverter, m_notation before m_readings).
        :
        m_dictDir(std::move(dictDir)), m_toneConverter(std::move(toneConverter)),
        // Asked of the constructed member, not of the moved-from parameter, and asked once: the
        // answer is a property of the dialect, not of a query.
        m_notation(detail::notationOf(m_toneConverter)), m_readings(m_notation) {
        m_transTable.fill(0);
        m_wordIndex.fill(0);
        m_ready = init();
    }

    EngineImpl::~EngineImpl() = default;

    bool EngineImpl::init() {
        // UNREACHABLE THROUGH THE PUBLIC API, and that is why no test pins this wording (checked
        // 2026-10-27 across every suite: the string appears nowhere). `Engine::Engine` resolves an
        // empty subdirectory to the dialect's own id (`src/Engine.cpp:137`) before constructing the
        // inner engine, so `dictDir / subdir` is non-empty even when the caller's root is; the three-
        // place constructor was tried with both arguments empty and still opened "mandarin". The branch
        // stays as insurance for an internal caller that hands over an empty path, where "dictDir is
        // empty" is a far better answer than the four "cannot open <table>" lines that follow.
        if (m_dictDir.empty()) {
            m_lastError = "dictDir is empty";
            return false;
        }

        m_lastError.clear();
        // Each required file is checked individually so the failure reason survives;
        // && would collapse the four distinct causes below into one silent false (defect C-20).
        // user_dict.txt is deliberately NOT one of them: it is optional and reported separately.
        struct Step {
            const char *name;
            bool ok;
        };
        const Step steps[] = {
            {"phrases_map.txt", loadDict(m_dictDir / "phrases_map.txt", m_phrases.builtinStart)},
            {"phrases_dict.txt", loadDict(m_dictDir / "phrases_dict.txt", m_phrases.merged)},
            {"word.txt", loadDict(m_dictDir / "word.txt", m_wordIndex, m_wordValues)},
            {"trans_word.txt", loadDict(m_dictDir / "trans_word.txt", m_transTable)},
        };
        for (const auto &s : steps) {
            if (!s.ok) {
                if (!m_lastError.empty())
                    m_lastError += ", ";
                m_lastError += s.name;
            }
        }

        // The shipped user_dict.txt is optional: a distribution may ship without one,
        // and that must not make the engine unusable.
        const auto userPath = m_dictDir / "user_dict.txt";
        std::error_code ec;
        // Absent stays silent (a distribution may ship without one). Present must be usable:
        // a path that exists but is not a regular file is a loud failure, because POSIX opens a
        // directory happily while Windows refuses -- without this the two platforms disagreed
        // (board-only failure of tests/cpp/test_bug_regression.cpp:326, rounds 45-54).
        const bool isRegularFile = std::filesystem::is_regular_file(userPath, ec);
        if (std::filesystem::exists(userPath, ec) && !isRegularFile) {
            if (!m_lastError.empty())
                m_lastError += ", ";
            m_lastError += "user dict is not a regular file: " + userPath.string();
        } else if (isRegularFile) {
            loadUserDict(userPath, true);
        }

        // Built once every phrase layer is in place; rebuildPhrases() redoes it whenever a user
        // dictionary changes that layer at runtime.
        rebuildPhraseIndex();

        // The structure of every reading, now that both tables are final (S6). Moved out of the query
        // path, where building it per reading cost 4x (see applyTone()). Each layer is walked exactly
        // once here: the user-dictionary load above runs rebuildPhrases(), whose preparation step is
        // skipped while the engine is still constructing (m_ready is false), because this is the call
        // that covers that case -- including a distribution that ships no user_dict.txt at all.
        prepareWordReadings();
        preparePhraseReadings();

        m_ready = m_lastError.empty();
        if (!m_ready)
            m_lastError = "failed to load: " + m_lastError + " (under " + m_dictDir.string() + ")";
        return m_ready;
    }

    bool EngineImpl::isReady() const noexcept { return m_ready; }

    // Tone converter helpers

    void EngineImpl::setDisambiguationCallback(DisambiguationCallback callback) {
        m_disambiguationCallback = std::move(callback);
    }

    void EngineImpl::prepareWordReadings() {
        // Two things in one walk, and they must stay one walk: the flat cell array is filled in
        // bucket order and `m_wordBucketCells[b]` is the address of bucket b's first cell, so a
        // second loop that skipped a cell would attach some other character's structure to a row --
        // a wrong answer, not a crash, which is the kind of bug the frozen dump exists to catch.
        // One block, one allocation: a vector per bucket cost ~11k allocations at load time and one
        // more dependent load per query row, which the A/B measured.
        size_t total = 0;
        for (const auto &readings : m_wordValues)
            total += readings.size();
        m_wordPreparedCells.clear();
        m_wordPreparedCells.reserve(total);
        m_wordBucketCells.assign(m_wordValues.size(), nullptr);

        for (size_t idx = 0; idx < m_wordValues.size(); ++idx) {
            const auto &readings = m_wordValues[idx];
            if (!readings.empty())
                m_wordBucketCells[idx] = m_wordPreparedCells.data() + m_wordPreparedCells.size();
            for (const auto &reading : readings)
                m_wordPreparedCells.push_back(&m_readings.prepare(reading, ReadingSource::dictionary));
        }
    }

    void EngineImpl::preparePhraseReadings() {
        for (const auto &entry : m_phrases.merged) {
            for (const auto &reading : entry.second)
                m_readings.prepare(reading, ReadingSource::phrase);
        }
    }
    std::u16string EngineImpl::applyTone(const std::u16string &pinyin, int style, bool v_to_u,
                                         bool neutralTone5) const {
        // The one place a reading becomes a rendered string. The reading arrives from a table and is
        // handed to the dialect's converter as-is: the load-time pass has already built its structure
        // and checked that the string being rendered here is that structure's own surface (S6). S5
        // rebuilt the structure on this path, per reading, and paid for it: 50 chars 7.0 -> 28.8 us,
        // 95 chars 13.8 -> 54.0 us, `readings()` 0.1 -> 1.2 us. The structure is not consulted here
        // -- a prepared-entry lookup would be a second indirection in the hot path, the shape that
        // lost 9.7% in docs/PERFORMANCE.md 4.15.5.
        return m_toneConverter.convert(pinyin, style, v_to_u, neutralTone5);
    }

    std::vector<std::u16string> EngineImpl::applyTone(const std::vector<std::u16string> &pinyins, int style,
                                                      bool v_to_u, bool neutralTone5) const {
        std::vector<std::u16string> result;
        result.reserve(pinyins.size());
        for (const auto &p : pinyins) {
            result.push_back(applyTone(p, style, v_to_u, neutralTone5));
        }
        return result;
    }

    // Input segmentation

    namespace
    {
        // K1 says whitespace is skipped, but cpp_pinyin::v2::isSpace() is a *public predicate about the
        // ASCII space* -- it answers false for '\t' and U+3000, and its own regression test pins
        // that. The tokenizer therefore needs the wider, documented set: ASCII space, the C0
        // whitespace controls, and the Unicode Zs separators. U+200B (zero width space) is
        // deliberately NOT here: Unicode classifies it as Cf, not whitespace.
        // Measured: the four frozen datasets contain 345,719 whitespace characters and all of them
        // are U+0020, so aligning this branch with K1 cannot move the frozen dump.
        constexpr bool isSkippableSpace(char16_t c) {
            switch (c) {
            case u'\t':
            case u'\n':
            case u'\v':
            case u'\f':
            case u'\r':
            case u' ':
            case 0x00A0:  // no-break space
            case 0x1680:  // ogham space mark
            case 0x2028:  // line separator
            case 0x2029:  // paragraph separator
            case 0x202F:  // narrow no-break space
            case 0x205F:  // medium mathematical space
            case 0x3000:  // ideographic space
                return true;
            default:
                return c >= 0x2000 && c <= 0x200A;  // en/em/thin/hair... spaces
            }
        }
    }  // namespace

    std::vector<std::u16string> EngineImpl::splitInput(std::string_view utf8) const {
        const auto u16 = toU16(std::string(utf8));
        std::vector<std::u16string> res;
        res.reserve(u16.size());

        auto start = u16.begin();
        const auto end = u16.end();

        while (start != end) {
            const auto &currentChar = *start;
            if (currentChar >= 0xD800 && currentChar <= 0xDBFF && start + 1 != end && *(start + 1) >= 0xDC00 &&
                *(start + 1) <= 0xDFFF) {
                // Astral-plane character (emoji, U+20000.., ...): keep the surrogate
                // pair as ONE token. Splitting it would both produce two garbage
                // entries and hand a lone surrogate to u16strToUtf8str, which is not
                // a valid code unit (it used to throw and abort the process).
                res.emplace_back(start, start + 2);
                std::advance(start, 2);
            } else if (isLetter(currentChar)) {
                auto letterStart = start;
                while (start != end && isLetter(*start)) {
                    ++start;
                }
                res.emplace_back(letterStart, start);
            } else if (isKana(currentChar)) {
                const int length = start + 1 != end && isSpecialKana(*(start + 1)) ? 2 : 1;
                res.emplace_back(start, start + length);
                std::advance(start, length);
            } else if (!isSkippableSpace(currentChar)) {
                // Each remaining visible char (hanzi, digit, punctuation) becomes its own token. The old
                // `isHanzi(c) || isDigit(c) || !isSpace(c)` was a dead condition -- the last term alone is
                // true for any non-space -- so isHanzi now serves the public API only. Note isSkippableSpace,
                // not isSpace (U+0020 alone): K1 skips all whitespace, else '\t'/U+3000 become failed rows.
                res.emplace_back(1, currentChar);
                ++start;
            } else {
                ++start;
            }
        }
        return res;
    }

    // Default readings for a single character

    std::vector<std::string> EngineImpl::defaultReadings(char16_t ch, int style, bool v_to_u, bool neutralTone5) const {
        const auto *candidates = wordLookup(ch);
        if (!candidates)
            return {toUtf8(ch)};

        std::vector<std::string> toneCandidates = convertCandidates(*candidates, style, v_to_u, neutralTone5);
        if (toneCandidates.empty())
            return {toUtf8(ch)};
        return toneCandidates;
    }

    std::vector<std::string> EngineImpl::convertCandidates(const std::vector<std::u16string> &candidates, int style,
                                                           bool v_to_u, bool neutralTone5) const {
        std::vector<std::string> converted;
        converted.reserve(4);
        for (const auto &pinyin : candidates) {
            const auto &tar = toUtf8(applyTone(pinyin, style, v_to_u, neutralTone5));
            if (std::find(converted.begin(), converted.end(), tar) == converted.end()) {
                converted.push_back(tar);
            }
        }
        return converted;
    }

    std::string EngineImpl::renderFirstReading(const std::vector<std::u16string> &candidates, char16_t ch, int style,
                                               bool v_to_u, bool neutralTone5) const {
        return candidates.empty() ? toUtf8(ch) : toUtf8(applyTone(candidates.front(), style, v_to_u, neutralTone5));
    }

    // Core algorithm — the heart of the library

    Segment &EngineImpl::commitRow(const size_t zhIndex, size_t &decided, std::vector<Segment> &out,
                                   const std::vector<uint32_t> &tokenOf) const {
        if (zhIndex >= decided)
            decided = zhIndex + 1;
        return out[tokenOf[zhIndex]];
    }

    void EngineImpl::prefetchGates(const std::vector<char16_t> &chars, const size_t cursor,
                                   uint32_t (&firstBits)[kPhraseGateCount], uint64_t (&secondBits)[kPhraseGateCount],
                                   uint8_t (&secondShift)[kPhraseGateCount]) const {
        if (!m_phrases.indexReady)
            return;  // every gate passes: plain probing, i.e. the pre-index behaviour
        for (size_t k = 0; k < kPhraseGateCount; ++k) {
            const long long p = static_cast<long long>(cursor) - 3 + k;
            // p+1 >= n means no phrase of length >= 2 can start there, and the attempt guards reject
            // those anyway -- zeroing the gate keeps this loop from reading past the end.
            if (p < 0 || static_cast<size_t>(p) + 1 >= chars.size()) {
                firstBits[k] = 0;
                secondBits[k] = 0;
                continue;
            }
            const size_t ps = static_cast<size_t>(p);
            const detail::PhraseGate &gate = m_phrases.gates[chars[ps]];
            firstBits[k] = gate.desc;
            secondBits[k] = gate.secondBits;
            secondShift[k] = static_cast<uint8_t>(chars[ps + 1] & 63);
        }
    }

    bool EngineImpl::emitPhraseAt(const char16_t *phrase, const size_t phraseLen, const size_t offset,
                                  const QuerySpec &spec, size_t &decided, std::vector<Segment> &out,
                                  const std::vector<uint32_t> &tokenOf) const {
        const uint64_t key = encodePhrasesKey(phrase, phraseLen);
        const std::vector<std::u16string> *ph = nullptr;
        // The structure of every reading of the bucket the lookup lands in, filled by the same call (S7).
        // A phrase row therefore costs no second data structure and no hash lookup of its own -- the index
        // that already found the readings hands out their structures as well. Null when the index is
        // absent: the loop below then reaches each structure the old way, the pre-index path.
        const detail::PreparedReading *const *prepared = nullptr;
        if (m_phrases.indexReady) {
            ph = findPhrase(key, phrase, phraseLen, &prepared);
        } else {
            const auto it = m_phrases.merged.find(key);
            if (it != m_phrases.merged.end())
                ph = &it->second;
        }
        if (!ph)
            return false;

        const auto &subRes = applyTone(*ph, spec.style, spec.v_to_u, spec.neutralTone5);
        // A well-formed entry carries one reading per character of the phrase. Anything else cannot be
        // laid out as one row per character; treating it as "not a phrase" keeps the rows aligned with
        // the tokens instead of writing readings (or worse, characters) from beyond the phrase.
        if (subRes.size() != phraseLen)
            return false;

        for (size_t i = 0; i < subRes.size(); i++) {
            const char16_t lyric = phrase[i];
            auto &row = commitRow(offset + i, decided, out, tokenOf);
            row.rendered = toUtf8(subRes[i]);
            row.candidates = spec.includeCandidates ? defaultReadings(lyric, spec.style, spec.v_to_u, spec.neutralTone5)
                                                    : std::vector<std::string>{};
            // The fallback is the pre-index path (the index is absent, so nothing was built): a
            // non-inserting lookup, because an inserting one here would be a data race on a table
            // several threads read at once. With the switch off, neither the array read nor that hash
            // lookup happens at all.
            if (spec.includeReading) {
                if (prepared != nullptr) {
                    attachReading(row, prepared[i]);
                } else if (const auto *cell = m_readings.find((*ph)[i], ReadingSource::phrase); cell != nullptr) {
                    attachReading(row, cell);
                }
            }
            row.failure_kind = Code::Ok;
        }
        return true;
    }

    bool EngineImpl::tryPhraseAt(const std::vector<char16_t> &chars, const uint32_t (&firstBits)[kPhraseGateCount],
                                 const uint64_t (&secondBits)[kPhraseGateCount],
                                 const uint8_t (&secondShift)[kPhraseGateCount], const QuerySpec &spec, size_t &decided,
                                 size_t &cursor, std::vector<Segment> &out,
                                 const std::vector<uint32_t> &tokenOf) const {
        const size_t at = cursor;
        // The 12 attempts in the frozen order (length 4 -> 2, then dir(1)..dir(4)): `rel` = phrase start
        // relative to the cursor, `rowMode` 0 = the row `decided` points at, 1 = the row before it
        // (dir(2)), 2 = the phrase start itself (dir(3)/dir(4)); `advance` = how far the cursor moves on
        // a hit.
        struct Attempt {
            int8_t rel;
            uint8_t len;
            uint8_t rowMode;
            uint8_t advance;
        };
        static constexpr Attempt kAttempts[] = {
            {0, 4, 0, 4},  {-1, 4, 1, 3}, {-3, 4, 2, 1}, {-2, 4, 2, 2}, {0, 3, 0, 3},  {-1, 3, 1, 2},
            {-2, 3, 2, 1}, {-1, 3, 2, 2}, {0, 2, 0, 2},  {-1, 2, 1, 1}, {-1, 2, 2, 1}, {0, 2, 2, 2},
        };

        // One unified guard (start+len <= n) replaces the four direction guards; they are equivalent and
        // each keeps the exact bound of the C-01 fix -- dir(1) c+L<=n, dir(2) c>=1 && c+L<=n+1 (start =
        // c-1), dir(3) c+1>=L && c+1<=n (start = c+1-L), dir(4) c+2>=L && c+2<=n (start = c+2-L). Note
        // dir(2) still reads chars[c+L-2] (start+len == n+1 is rejected, chars[n-1] stays readable): do
        // NOT tighten to c+L<=n.
        for (const Attempt &a : kAttempts) {
            const long long start = static_cast<long long>(at) + a.rel;
            if (start < 0 || static_cast<size_t>(start) + a.len > chars.size())
                continue;
            const size_t k = kPhraseGateSlotOffset + static_cast<size_t>(a.rel);
            if ((firstBits[k] & (1u << (a.len - 2))) == 0)
                continue;
            if ((secondBits[k] & (1ull << secondShift[k])) == 0)
                continue;
            // rowMode 1 = phrase starts 1 char before cursor, so its row is `decided-1`: legal only because
            // the guard above rejects start < 0 => cursor >= 1. Modes 0/2 OK at decided == 0, so the assert
            // is on the arm, not the loop: loop-level false on every well-formed first attempt (2026-10-06
            // gdb, "中国人民"-style 8, decided=0/rowMode=0), aborting assert builds for correct output --
            // release spared (NDEBUG + DumpEquivalence); Debug and the no-CMAKE_BUILD_TYPE packaged lib
            // aborted.
            assert(a.rowMode != 1 || decided >= 1);
            const size_t offset =
                a.rowMode == 0 ? decided : (a.rowMode == 1 ? decided - 1 : static_cast<size_t>(start));
            if (emitPhraseAt(chars.data() + start, a.len, offset, spec, decided, out, tokenOf)) {
                cursor = at + a.advance;
                return true;
            }
        }
        return false;
    }

    void EngineImpl::queryRawInto(const std::vector<char16_t> &chars, const std::vector<uint32_t> &tokenOf,
                                  const std::vector<size_t> &hanziByteOffset, std::string_view fullText,
                                  const int style, const bool includeCandidates, const bool includeReading,
                                  const bool v_to_u, const bool neutralTone5, std::vector<Segment> &out) const {
        const QuerySpec spec{style, includeCandidates, includeReading, v_to_u, neutralTone5};
        size_t cursor = 0;
        // Rows decided so far. Every attempt writes a contiguous run of rows starting at its own base, and
        // the cursor advance always equals that base plus the run length, so this stays equal to the
        // result.size() the two-pass version based its offsets on.
        size_t decided = 0;

        while (cursor < chars.size()) {
            const char16_t currentChar = chars[cursor];

            // Not in dictionary. PROVEN UNREACHABLE, not assumed (Q8): `chars` is this function's
            // parameter and its only caller passes the list `assemble()` already filtered by
            // `wordLookup() != nullptr` -- a const table lookup that cannot start returning null in
            // between. Kept deliberately: if that invariant ever changes, this branch still decides the
            // row instead of dropping it, and the row is cold, so deleting it buys nothing.
            const auto *wordCandidates = wordLookup(currentChar);
            if (!wordCandidates) {
                auto &row = commitRow(cursor, decided, out, tokenOf);
                row.rendered = toUtf8(currentChar);
                row.candidates.clear();
                row.failure_kind = Code::InvalidArgument;
                cursor++;
                continue;
            }

            // Not polyphonic -- fast path
            if (!hasPolyphonic(currentChar)) {
                if (!includeCandidates) {
                    // Only [0] is observable here; reasoning and both measurements are in
                    // `renderFirstReading`'s declaration (EngineImpl.h), not beside each call.
                    auto &row = commitRow(cursor, decided, out, tokenOf);
                    row.rendered = renderFirstReading(*wordCandidates, currentChar, style, v_to_u, neutralTone5);
                    // The structure of the reading just rendered is the first cell of the same bucket.
                    // Opt-in since S8: with the switch off this is one predictable branch and the row keeps
                    // the default `Syllable` -- no bucket read, and none of the two `std::string`
                    // assignments per row that the switch-on path costs.
                    attachWordReading(row, currentChar, includeReading);
                    row.candidates.clear();
                    row.failure_kind = Code::Ok;
                    cursor++;
                    continue;
                }
                std::vector<std::string> toneCandidates =
                    convertCandidates(*wordCandidates, style, v_to_u, neutralTone5);
                if (toneCandidates.empty())
                    toneCandidates.push_back(toUtf8(currentChar));

                auto &row = commitRow(cursor, decided, out, tokenOf);
                row.rendered = toneCandidates[0];
                row.candidates = std::move(toneCandidates);
                // `[0]` is the conversion of the bucket's first reading whenever the bucket is non-empty;
                // the fallback above pushes `character` itself, which has no reading to describe -- hence
                // the guard rather than an unconditional attach.
                attachWordReading(row, currentChar, includeReading);
                row.failure_kind = Code::Ok;
                cursor++;
                continue;
            }

            // Polyphonic -- try custom disambiguation callback first
            if (m_disambiguationCallback) {
                // Encoded here rather than before the branch: it is what the callback is handed, and with
                // no callback installed it was one wasted conversion per polyphonic character per pass.
                const std::string charUtf8 = toUtf8(currentChar);
                const auto &defCandidates = defaultReadings(currentChar, style, v_to_u, neutralTone5);
                const auto &selected =
                    m_disambiguationCallback(fullText, hanziByteOffset[cursor], charUtf8, defCandidates);
                if (!selected.empty()) {
                    auto &row = commitRow(cursor, decided, out, tokenOf);
                    row.rendered = selected;
                    row.candidates = includeCandidates ? defCandidates : std::vector<std::string>{};
                    row.failure_kind = Code::Ok;
                    // No structure is attached: the string came from the callback, not from any table, so
                    // there is no reading to describe. The row keeps the default `Syllable` exactly the way
                    // a failed row does.
                    cursor++;
                    continue;
                }
            }

            // Batched gates: the 12 attempts only ever start at one of four characters (cursor-3 ..
            // cursor) and only ever test four (first, second) pairs, so the direct index is read once
            // per pair instead of once per attempt, and no key is encoded for an attempt a bit test
            // already rejects. With the index disabled every gate passes and this degrades to plain
            // probing, i.e. exactly the pre-index behaviour.
            uint32_t firstBits[kPhraseGateCount] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
            uint64_t secondBits[kPhraseGateCount] = {~0ull, ~0ull, ~0ull, ~0ull};
            uint8_t secondShift[kPhraseGateCount] = {0, 0, 0, 0};
            prefetchGates(chars, cursor, firstBits, secondBits, secondShift);
            if (tryPhraseAt(chars, firstBits, secondBits, secondShift, spec, decided, cursor, out, tokenOf))
                continue;

            // Not found in any phrase -- use default pinyin
            const std::vector<std::string> pinyin = includeCandidates
                ? defaultReadings(currentChar, style, v_to_u, neutralTone5)
                : std::vector<std::string>{};
            auto &row = commitRow(cursor, decided, out, tokenOf);
            if (includeCandidates) {
                row.rendered = pinyin[0];
                row.candidates = pinyin;
            } else {
                // Same trade as the fast path above -- one pointer, one place.
                row.rendered = renderFirstReading(*wordCandidates, currentChar, style, v_to_u, neutralTone5);
                row.candidates.clear();
            }
            // Both arms render the bucket's first reading (or `character` when the bucket is empty, which
            // describes nothing).
            attachWordReading(row, currentChar, includeReading);
            row.failure_kind = Code::Ok;
            cursor++;
        }
    }

    std::vector<Segment> EngineImpl::assemble(std::vector<std::u16string> &&tokens, std::string_view fullText,
                                              int style, ErrorHandling onError, bool includeCandidates,
                                              bool includeReading, bool v_to_u, bool neutralTone5) const {
        std::vector<Segment> result(tokens.size());
        std::vector<char16_t> hansList;
        std::vector<uint32_t> tokenOf;
        // Where each hanzi starts inside `fullText`, in UTF-8 bytes. Built here because this is
        // the only place that sees both the token list and the text they were joined into.
        std::vector<size_t> hanziByteOffset;
        hansList.reserve(tokens.size());
        tokenOf.reserve(tokens.size());
        hanziByteOffset.reserve(tokens.size());
        size_t byteOffset = 0;

        for (size_t i = 0; i < tokens.size(); ++i) {
            auto &row = result[i];
            row.text = toUtf8(tokens[i]);
            const size_t tokenOffset = byteOffset;  // this token's start in `fullText`
            byteOffset += row.text.size();

            // A token takes part in phrase matching only when its simplified first
            // character has readings of its own; every other token (punctuation, latin
            // words, digits, whitespace) becomes the failed row it always was.
            const bool isHanzi = !tokens[i].empty() && wordLookup(toSimp(tokens[i][0])) != nullptr;
            if (isHanzi) {
                hansList.emplace_back(toSimp(tokens[i][0]));
                tokenOf.emplace_back(static_cast<uint32_t>(i));
                hanziByteOffset.emplace_back(tokenOffset);
            } else {
                row.rendered = row.text;
                row.candidates.emplace_back(row.text);
                // Not a reading at all: `failure_kind` replaces the old `failed` flag, and the
                // code says the same thing the flag did -- this fragment is not something the
                // library can read (see `Segment::failure_kind` in core/Types.h). `rendered` stays
                // `text`, which is what the old face promised and what the frozen dump checks.
                row.failure_kind = Code::InvalidArgument;
            }
        }

        queryRawInto(hansList, tokenOf, hanziByteOffset, fullText, style, includeCandidates, includeReading, v_to_u,
                     neutralTone5, result);

        if (onError == ErrorHandling::Drop) {
            std::vector<Segment> filtered;
            for (auto &s : result) {
                if (s.failure_kind == Code::Ok)
                    filtered.emplace_back(std::move(s));
            }
            return filtered;
        }
        return result;
    }

    // Public query API

    std::vector<Segment> EngineImpl::query(std::string_view text, int style, ErrorHandling onError,
                                           bool includeCandidates, bool includeReading, bool v_to_u,
                                           bool neutralTone5) const {
        if (!m_ready)
            return {};

        return assemble(splitInput(text), text, style, onError, includeCandidates, includeReading, v_to_u,
                        neutralTone5);
    }

    std::vector<Segment> EngineImpl::query(const std::vector<std::string> &chars, int style, ErrorHandling onError,
                                           bool includeCandidates, bool includeReading, bool v_to_u,
                                           bool neutralTone5) const {
        if (!m_ready)
            return {};

        std::vector<std::u16string> u16List;
        u16List.reserve(chars.size());
        for (const auto &c : chars) {
            u16List.emplace_back(toU16(c));
        }

        // Reconstruct full text for callback
        std::string fullText;
        for (const auto &c : chars)
            fullText += c;

        return assemble(std::move(u16List), fullText, style, onError, includeCandidates, includeReading, v_to_u,
                        neutralTone5);
    }

    // Single character helpers
    Outcome<std::vector<std::string>> EngineImpl::readings(std::string_view hanzi, int style, bool v_to_u,
                                                           bool neutralTone5) const {
        if (!m_ready)
            return Error{Code::NotReady, "engine is not ready"};
        const auto u16 = toU16(std::string(hanzi));
        if (u16.empty())
            return std::vector<std::string>{};  // empty input is legal: a value, not an error
        return defaultReadings(toSimp(u16[0]), style, v_to_u, neutralTone5);
    }

    Outcome<std::string> EngineImpl::toSimplified(std::string_view hanzi) const {
        // Not ready is a FAILURE now, not an echo of the input: the old `return std::string(hanzi)`
        // was indistinguishable from "converted, nothing changed", which is exactly the silent-value
        // shape the C face never had (it answers CPPP2_ERR_NOT_READY). An empty input stays a legal
        // empty answer -- nothing failed there.
        if (!m_ready)
            return Error{Code::NotReady, m_lastError};
        const auto u16 = toU16(std::string(hanzi));
        if (u16.empty())
            return std::string(hanzi);
        return toUtf8(toSimp(u16[0]));
    }

    bool EngineImpl::isPolyphonic(std::string_view hanzi) const {
        if (!m_ready)
            return false;
        const auto u16 = toU16(std::string(hanzi));
        if (u16.empty())
            return false;
        return hasPolyphonic(u16[0]);
    }

}  // namespace cpp_pinyin::v2
