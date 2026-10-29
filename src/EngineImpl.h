#pragma once

#include "dict/PhraseIndex.h"
#include "dict/PhraseLayers.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/core/U16Str.h>

#include "dict/DictUtil.h"
#include "toneUtil/ReadingCodec.h"

namespace cpp_pinyin::v2
{
    class EngineImpl {
    public:
        /// `toneConverter` is copied into the engine (sliced to the ToneConverter base, which is
        /// why the dialect-specific hooks are function pointers and not virtuals). It must be
        /// supplied at construction: it decides both the output styles and how external
        /// dictionary readings are normalized, and dictionaries are loaded while constructing.
        EngineImpl(std::filesystem::path dictDir, ToneConverter toneConverter);
        ~EngineImpl();

        EngineImpl(const EngineImpl &) = delete;
        EngineImpl &operator=(const EngineImpl &) = delete;
        EngineImpl(EngineImpl &&) noexcept = default;
        EngineImpl &operator=(EngineImpl &&) noexcept = default;

        bool isReady() const noexcept;

        /// Empty when every dictionary file loaded. Otherwise names the files that
        /// failed, so a caller seeing isReady()==false knows whether it is a wrong
        /// path, a missing file, or a malformed one instead of guessing.
        [[nodiscard]] const std::string &lastError() const noexcept { return m_lastError; }

        // ---- Core query with raw UTF-8 string ----
        std::vector<Segment> query(std::string_view text, int style, ErrorHandling onError, bool includeCandidates,
                                   bool includeReading, bool v_to_u, bool neutralTone5) const;

        // ---- Core query with pre-segmented strings ----
        std::vector<Segment> query(const std::vector<std::string> &chars, int style, ErrorHandling onError,
                                   bool includeCandidates, bool includeReading, bool v_to_u, bool neutralTone5) const;

        // ---- Single character helpers ----
        Outcome<std::vector<std::string>> readings(std::string_view hanzi, int style, bool v_to_u,
                                                   bool neutralTone5) const;

        /// `Code::NotReady` (this impl is not ready) on failure -- the facade adds the moved-from case.
        Outcome<std::string> toSimplified(std::string_view hanzi) const;
        bool isPolyphonic(std::string_view hanzi) const;

        // ---- Customization ----
        // setToneConverter() was deleted: the converter is what decides, at construction time, the
        // output styles AND how an external dictionary's readings are normalised -- and dictionaries are
        // loaded during construction. Replacing the converter afterwards would leave the user dictionaries
        // already loaded inconsistent with the current converter.
        void setDisambiguationCallback(DisambiguationCallback callback);
        bool loadUserDict(const std::filesystem::path &path, bool clear = true);
        bool loadTraditionalDict(const std::filesystem::path &path, bool clear = true);

    private:
        // ---- Dictionary data ----
        // The phrase layer -- built-in + user tables, the two window gates and the derived index --
        // now lives in dict/PhraseLayers.h, together with the load-time code that fills it
        // (P2 split #2). Its per-member comments moved there. What stays here is the character
        // conversion table and the character-word table, neither of which is part of that layer.
        detail::PhraseLayers m_phrases;

        std::array<char16_t, CHAR16_TABLE_SIZE> m_transTable{};
        std::array<uint16_t, CHAR16_TABLE_SIZE> m_wordIndex{};
        std::vector<std::vector<std::u16string>> m_wordValues;
        /// The structure of every reading in `m_wordValues`, cell for cell and in the same order
        /// (S7): `m_wordPreparedCells` is one flat block in bucket order, and `m_wordBucketCells[b]`
        /// is a pointer to the first cell of bucket `b` -- or null when that bucket has no reading.
        /// Built once, by prepareWordReadings(), from the prepared entries in `m_readings`, so the
        /// same reading stored in a thousand cells costs one structure and a thousand pointers.
        std::vector<const detail::PreparedReading *> m_wordPreparedCells;
        std::vector<const detail::PreparedReading *const *> m_wordBucketCells;

        /// The same arrangement for the phrase layer, parallel to `m_phrases.values`: index i is the
        /// `i`-th entry of the phrase index (S7). Rebuilt by rebuildPhraseIndex(), which is the only
        /// place the phrase index is built or invalidated.
        std::vector<const detail::PreparedReading *> m_phrasePreparedCells;
        std::vector<const detail::PreparedReading *const *> m_phraseBucketCells;

        std::filesystem::path m_dictDir;
        ToneConverter m_toneConverter;
        /// The notation this dialect's tables are written in, asked of the converter once here (never
        /// per query): `detail::notationOf` (toneUtil/ReadingCodec.h). It is what lets the structured
        /// syllable be produced without the core ever naming a dialect.
        detail::ToneNotation m_notation = detail::ToneNotation::toneMarked;
        /// The structure of every distinct reading this engine holds, built once per dictionary (S6):
        /// "the reading's structure, prepared at load". The query path renders the string a reading is
        /// stored as and does NOT consult this table -- see applyTone() for why (the sibling
        /// experiment in docs/PERFORMANCE.md 4.15.5 lost 9.7% by putting a table between the hot path
        /// and the data). What the table is for: the structure of every reading the engine can report,
        /// which `Segment` will carry in a later slice, and the load-time proof that each stored
        /// spelling IS its structure's own surface (`PreparedReading::rebuilt`).
        detail::ReadingTable m_readings{detail::ToneNotation::toneMarked};
        bool m_ready = false;
        /// Why the last load failed, empty when everything loaded. Surfaced through the
        /// public lastError() so callers are not left with a bare isReady()==false.
        std::string m_lastError;

        DisambiguationCallback m_disambiguationCallback;

        // ---- Internal helpers ----
        bool init();

        /// Loads into the USER layer only, then re-merges both layers.
        bool loadUserDictLayer(const std::filesystem::path &path, bool clear);

        /// Re-merges m_phrases.user into m_phrases.merged. The built-in layer is ALWAYS reloaded from
        /// phrases_dict.txt first (that file is authoritative and needs no extra snapshot), so a
        /// user-dict load can never drop it -- an earlier version took a "reload builtin?" flag and
        /// loadUserDict(path, /*clear=*/false) silently lost every built-in phrase.
        bool rebuildPhrases();

        /// (Re)builds m_phraseIndex/m_phrases.keys/m_phrases.values from m_phrases.merged and verifies
        /// that every key stays reachable through it. Load paths only, so the index stays
        /// immutable -- and therefore safe to read from concurrent queries -- while serving.
        void rebuildPhraseIndex();

        /// Exact phrase lookup through the direct index: the length bit rejects
        /// (first character, length) pairs the dictionary never contains, the second-character
        /// bitmap rejects the pairs it does contain but with a different second character, and
        /// only then is the bucket scanned key by key. Returns nullptr when the phrase is absent.
        [[nodiscard]] const std::vector<std::u16string> *
        findPhrase(uint64_t key, const char16_t *phrase, size_t len,
                   const detail::PreparedReading *const **cells = nullptr) const {
            const detail::PhraseGate &gate = m_phrases.gates[static_cast<uint16_t>(phrase[0])];
            if ((gate.desc & (1u << (len - 2))) == 0)
                return nullptr;
            if ((gate.secondBits & (1ull << (phrase[1] & 63))) == 0)
                return nullptr;
            const uint32_t begin = gate.desc >> 16;
            const uint32_t count = (gate.desc >> 8) & 0xFFu;
            for (uint32_t i = begin; i < begin + count; ++i) {
                if (m_phrases.keys[i] == key) {
                    // `cells` is the structure of every reading of the same bucket, parallel to
                    // `m_phrases.values` and built with it (S7). Out-parameter rather than a second
                    // lookup, and defaulted to nullptr so the load-time self-check below reads
                    // exactly the code the query path does.
                    if (cells != nullptr)
                        *cells = m_phraseBucketCells[i];
                    return m_phrases.values[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] const std::vector<std::u16string> *wordLookup(char16_t ch) const {
            const uint16_t idx = m_wordIndex[static_cast<uint16_t>(ch)];
            return idx != 0 ? &m_wordValues[idx] : nullptr;
        }

        /// The structure of the FIRST reading of `ch`'s bucket, or nullptr when the character has no
        /// reading at all (S7). One array read plus one load, with no hash lookup: the structures were
        /// built at load time, cell for cell, in `m_wordPreparedCells` -- the lookup is what S6
        /// deliberately kept out of the hot path (docs/PERFORMANCE.md 4.15.5 records the 9.7% that a
        /// second table between the hot path and the data cost, and this pays an array read, not a
        /// hash).
        [[nodiscard]] const detail::PreparedReading *wordPreparedLookup(char16_t ch) const {
            const uint16_t idx = m_wordIndex[static_cast<uint16_t>(ch)];
            const auto *cells = idx != 0 ? m_wordBucketCells[idx] : nullptr;
            return cells != nullptr ? cells[0] : nullptr;
        }

        /// The structure of the reading a row selected (S7). `prepared` may be null -- a callback
        /// wrote the string itself, or a table row carried no reading at all -- and the row then
        /// keeps the default `Syllable`, which describes nothing: the same situation a failed row
        /// is in, and the reason `failure_kind` exists separately from "is `reading` filled".
        static void attachReading(Segment &row, const detail::PreparedReading *prepared) {
            if (prepared != nullptr)
                row.reading = prepared->data;
        }

        /// The word-table variant, and the ONE place the S8 switch is honoured: with `includeReading`
        /// false it returns before the lookup, so the default path pays neither bucket read nor copy.
        /// MEASURED, not assumed (27-char TONE3, candidates off): switch-off 1.9 us == lookup-without-
        /// copy 1.8-2.0 us, switch-on 1.9 -> 2.2 us (12 rounds, min-of-3, median ratio 1.100); the copy
        /// is the cost (~11 ns over 27 rows) and the guard skips the lookup too -- it only feeds it.
        void attachWordReading(Segment &row, char16_t ch, bool includeReading) const {
            if (!includeReading)
                return;
            attachReading(row, wordPreparedLookup(ch));
        }

        [[nodiscard]] bool hasPolyphonic(char16_t ch) const {
            // A character must open the phrase window if EITHER layer says a phrase may
            // start here. Checking only the built-in layer is the gate defect.
            const auto c = static_cast<uint16_t>(ch);
            return m_phrases.builtinStart.test(c) || m_phrases.userStart.test(c);
        }

        [[nodiscard]] char16_t toSimp(char16_t ch) const {
            const char16_t mapped = m_transTable[static_cast<uint16_t>(ch)];
            return mapped != 0 ? mapped : ch;
        }

        /// Builds the structure of every reading of the word table (S6). Load path only: that table
        /// is filled once, by init(), and never changes afterwards.
        void prepareWordReadings();
        /// The phrase layer alone. Called after every rebuild of that layer, which is what keeps the
        /// load-time structures in step with a user dictionary loaded at runtime.
        void preparePhraseReadings();

        /// Renders one table reading. The string handed over is the one the dictionary stores, and
        /// the load-time pass (prepareWordReadings / preparePhraseReadings) has established for every one that it IS
        /// the reading's structure surface -- `detail::PreparedReading::surface`, byte for byte equal to the stored
        /// spelling, or the stored spelling itself where the projection is not invertible. Nothing is parsed or
        /// inverted here: doing that per reading is what S5 added to this call and what cost 440 ns per reading (50
        /// chars 7.0 -> 28.8 us; 95 chars 13.8 -> 54.0 us; 4x).
        std::u16string applyTone(const std::u16string &pinyin, int style, bool v_to_u, bool neutralTone5) const;

        std::vector<std::u16string> applyTone(const std::vector<std::u16string> &pinyins, int style, bool v_to_u,
                                              bool neutralTone5) const;

        std::vector<std::string> defaultReadings(char16_t ch, int style, bool v_to_u, bool neutralTone5) const;
        /// Converts every reading of `candidates` to `style` and drops duplicates, keeping the first
        /// occurrence. The loop existed verbatim in `defaultReadings` and in `queryRawInto`: same table, same
        /// conversion, same de-duplication key, two copies to keep in step. Returns an EMPTY vector when the bucket is
        /// empty -- callers decide what "no reading" means for them
        /// (`defaultReadings` falls back to the character, `queryRawInto` pushes the character and continues).
        [[nodiscard]] std::vector<std::string> convertCandidates(const std::vector<std::u16string> &candidates,
                                                                 int style, bool v_to_u, bool neutralTone5) const;
        /// The one observable string of a bucket: its first reading converted, or `ch` itself when the bucket
        /// is empty. Spelled out because it was written twice (`queryRawInto`'s two paths) with the same
        /// three-way shape, and the performance note beside each copy explained the same trade -- 2026-10-20
        /// (redundancy sweep): that note now lives HERE, once, instead of beside both call sites.
        ///
        /// WHY THE WHOLE BUCKET IS NOT BUILT ON THESE PATHS: when the caller did not ask for candidates the
        /// vector is discarded immediately, so only `[0]` is observable and building the rest is pure waste.
        /// The two paths are NOT the same cost, which is why the note used to be written twice and why both
        /// numbers are kept here as measured on RK3399: 273.6 -> 135.3 ns per character over 1,105,402
        /// fast-path characters per pass, and 457.4 -> 121.7 ns per call over 900,063 calls per pass.
        [[nodiscard]] std::string renderFirstReading(const std::vector<std::u16string> &candidates, char16_t ch,
                                                     int style, bool v_to_u, bool neutralTone5) const;

        /// One-pass assembly of the final result: each token is UTF-8 encoded exactly once and the
        /// window writes readings straight into the output rows through tokenOf. The previous shape
        /// built a throw-away syllable vector for the hanzi subsequence and walked every token again to
        /// merge it back: double encoding, extra field copies, a wasted vector (measured 23% of a full
        /// pass, ~6 points of it the duplicated encoding).
        std::vector<Segment> assemble(std::vector<std::u16string> &&tokens, std::string_view fullText, int style,
                                      ErrorHandling onError, bool includeCandidates, bool includeReading, bool v_to_u,
                                      bool neutralTone5) const;

        /// The sliding-window algorithm, writing rows instead of building a vector.
        /// `chars` are the simplified hanzi that take part in matching; tokenOf[k] names the
        /// output row of hanzi k. Rows are decided strictly in order, so the internal
        /// `decided` counter stays equal to the result.size() the two-pass version read.
        /// `hanziByteOffset[k]` is where hanzi k starts inside `fullText`, in UTF-8 BYTES.
        /// The disambiguation callback is handed that number, not k, so that the two
        /// parameters it reads together (`text` and `position`) are in one coordinate system.
        void queryRawInto(const std::vector<char16_t> &chars, const std::vector<uint32_t> &tokenOf,
                          const std::vector<size_t> &hanziByteOffset, std::string_view fullText, int style,
                          bool includeCandidates, bool includeReading, bool v_to_u, bool neutralTone5,
                          std::vector<Segment> &out) const;

        /// The number of (first, second) pair gates the phrase attempts share. The window only ever starts
        /// a phrase at cursor-3 .. cursor (Attempt::rel is -3..0; it can REACH cursor+1 but never starts
        /// there) and only ever tests four pairs, so four slots cover every
        /// attempt -- the same invariant the 12-entry attempt table relies on.
        static constexpr size_t kPhraseGateCount = 4;
        /// A `rel` start maps to its gate slot by this offset: slot = rel + kPhraseGateSlotOffset, which is
        /// why the table's `rel` range is -3..0.
        static constexpr size_t kPhraseGateSlotOffset = 3;

        /// The five rendering switches one query carries, so the query-path helpers below do not each
        /// grow a nine-parameter signature. Passed by const reference and never stored: `EngineImpl` is
        /// moved (the move constructor is defaulted), so anything pointing at a member would dangle after
        /// a move -- that is why this is a parameter object, not a member.
        struct QuerySpec {
            int style;
            bool includeCandidates;
            bool includeReading;
            bool v_to_u;
            bool neutralTone5;
        };

        /// Marks row `zhIndex` decided and returns it. `character` is already filled in by `assemble()`
        /// from the ORIGINAL token, so only reading/candidates/failure_kind are written by the caller.
        Segment &commitRow(size_t zhIndex, size_t &decided, std::vector<Segment> &out,
                           const std::vector<uint32_t> &tokenOf) const;

        /// The four (first, second) pair gates the 12 attempts share, fetched once per cursor position.
        /// With the index disabled every gate passes (the arrays keep their all-ones initialisers).
        void prefetchGates(const std::vector<char16_t> &chars, size_t cursor, uint32_t (&firstBits)[kPhraseGateCount],
                           uint64_t (&secondBits)[4], uint8_t (&secondShift)[4]) const;

        /// Writes the rows of one phrase hit. False when nothing was written (no bucket, or a bucket whose
        /// reading count does not equal the phrase length). Does NOT move the cursor: only the caller
        /// knows which attempt matched, and its `advance` belongs to that attempt.
        [[nodiscard]] bool emitPhraseAt(const char16_t *phrase, size_t phraseLen, size_t offset, const QuerySpec &spec,
                                        size_t &decided, std::vector<Segment> &out,
                                        const std::vector<uint32_t> &tokenOf) const;

        /// The 12 attempts in the frozen order (length 4 -> 2, then dir(1)..dir(4)). Returns true and
        /// advances `cursor` on a hit.
        [[nodiscard]] bool tryPhraseAt(const std::vector<char16_t> &chars,
                                       const uint32_t (&firstBits)[kPhraseGateCount],
                                       const uint64_t (&secondBits)[kPhraseGateCount],
                                       const uint8_t (&secondShift)[kPhraseGateCount], const QuerySpec &spec,
                                       size_t &decided, size_t &cursor, std::vector<Segment> &out,
                                       const std::vector<uint32_t> &tokenOf) const;

        std::vector<std::u16string> splitInput(std::string_view utf8) const;
    };
}  // namespace cpp_pinyin::v2
