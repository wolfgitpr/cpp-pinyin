#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

#include <cpp-pinyin2/core/Syllable.h>
#include <cpp-pinyin2/core/ToneConverter.h>

// Producer + renderer of the 2.0 structured syllable (S5); the contract lives in core/Syllable.h (`tone` comes from the
// table's own notation, never from a rendered string), and both halves reuse the tone layer so no conversion table is
// written twice. A `detail` TU under src/toneUtil/, NOT part of the frozen public surface. WHEN IT RUNS (S6): built for
// every table reading at LOAD time, the query path only renders -- measured 440 ns per reading if done per query
// (50 chars 7.0 -> 28.8 us, ~4x, against a ~160 ns/char baseline); the sibling experiment that added indirection failed
// on it. KNOWN GAPS: ReadingSource::user_dict / plugin have no
// producer (a row's origin is no longer known once the phrase layers are merged) and neither does ToneKind::sandhi.
// BEFORE USING surfaceOf: (initial, fin) is a projection, not a re-encoding, and not always invertible.

namespace cpp_pinyin::v2
{
    namespace detail
    {
        /// How a dialect writes a tone on its table surface. The producer has to be told, because the
        /// two built-in dialects disagree and the engine's core deliberately carries no dialect
        /// branch. Use `notationOf()` rather than deciding this by hand.
        enum class ToneNotation {
            /// Mandarin: the tone is a diacritic on a vowel; a reading with no diacritic carries no
            /// tone (轻声). `markNeutralAs5` is a *rendering* option and is deliberately not an input
            /// here: the spellings `de` and `de5` are two different table entries and must stay
            /// distinguishable, or the surface could not be rebuilt (the API still reports 5 whenever
            /// the option says to, because that is what the renderer passes to the converter).
            toneMarked,
            /// Cantonese jyutping: the tone is the reading's own last character. Both shipped tables
            /// always carry one (1..6; the LSHK `*` notation occurs nowhere in `data/dialects`).
            trailingDigit
        };

        /// The notation a dialect's tables are written in, read from the one documented property that
        /// separates the two built-ins: `ToneConverter::normalizeReading()` exists to rewrite an
        /// external tone3 reading into the form the dialect *stores*. A converter that rewrites
        /// `"zhi4"` stores tone-marked readings (Mandarin); the identity map stores the reading
        /// exactly as the table wrote it, which for jyutping means the tone is its last character.
        ///
        /// This is a probe of documented behaviour, not a guess about data: it is answered once, when
        /// the engine is constructed, and never in the query path.
        [[nodiscard]] ToneNotation notationOf(const ToneConverter &converter);

        /// The table surface of one reading, split into the 2.0 face's structure (the S5 producer).
        ///
        /// For `toneMarked` the two halves come from `ManTone::toneToInitials()` and
        /// `ManTone::toneToFinals()`, i.e. from exactly what the INITIALS and FINALS styles render --
        /// which is what makes the structure and those two styles one authority rather than two.
        /// For `trailingDigit` they are the jyutping split (longest leading consonant run; jyutping's
        /// finals all begin with a vowel, so the split is unambiguous by construction).
        ///
        /// `source` is copied through. `tone_kind` is `base`: the reading is the table entry's own
        /// citation form and no rule rewrote it -- nothing in this repository produces `sandhi`
        /// (`core/Syllable.h` says so), and an empty reading stays `unspecified`.
        [[nodiscard]] Syllable parseReading(const std::u16string &stored, ToneNotation notation, ReadingSource source);

        /// The table surface the structure came from -- byte for byte the string `parseReading` was
        /// given, whenever the projection is invertible. Empty means "not invertible"; see the header
        /// comment. Never throws.
        [[nodiscard]] std::u16string surfaceOf(const Syllable &syllable, ToneNotation notation);

        /// `convert(surfaceOf(...))`: the structure, rendered by the dialect's own converter. Returns
        /// empty when the surface cannot be rebuilt, so a caller keeps its own string in that case.
        [[nodiscard]] std::u16string renderReading(const Syllable &syllable, ToneNotation notation,
                                                   const ToneConverter &converter, int style, bool v_to_u,
                                                   bool neutral_tone_with_five);

        // REMOVED 2026-10-20 (audit round 2 §12.2, verified in §12.7): `RenderedReading` and
        // `readReading()` lived here with ZERO callers in the whole tree, and the comment justified them
        // by saying the sweeps in `tests/cpp/test_syllable_producer.cpp` held both halves to them -- that
        // test mentions the name in one comment and calls `surfaceOf`/`renderReading` instead. A helper kept
        // alive by a false citation is worse than no helper: it invites the next reader to trust a contract
        // nobody checks. `prepareReading` (below) is what the load-time pass actually uses.

        // The load-time form (S6)

        /// One reading's structure, built once when the dictionary is loaded.
        ///
        /// `surface` is what gets rendered -- the same bytes the table stores. It is `surfaceOf(data)`
        /// when that rebuilds the stored spelling byte for byte (`rebuilt`), and the stored spelling
        /// itself otherwise. **`surface == stored` always**, which is the property the engine's query
        /// path relies on: it hands the dictionary's own string to the converter and does not parse or
        /// invert anything per reading. Establishing that equality *is* the load-time work -- doing it
        /// per query is what S5 did and what cost 4x (see the header comment).
        struct PreparedReading {
            Syllable data;
            std::u16string surface;
            /// true = `surface` came out of the structure (`surfaceOf`); false = this reading's
            /// projection is not invertible and `surface` is the table's own spelling. The set is
            /// exact and named by test_syllable_producer.cpp over the whole shipped data.
            bool rebuilt = false;
        };

        /// `parseReading` + `surfaceOf`, with the fallback rule folded in. Pure and
        /// dialect-neutral: everything dialect-specific arrives through `notation`.
        [[nodiscard]] PreparedReading prepareReading(const std::u16string &stored, ToneNotation notation,
                                                     ReadingSource source);

        /// Every distinct reading the engine holds, prepared once at load and kept for the lifetime of
        /// the dictionary. Load-time only: nothing here is called from a query.
        ///
        /// It exists because the same reading occurs in thousands of table cells (Mandarin: 36,354
        /// cells, 1,561 distinct readings; Cantonese: 33,263 / 2,390), so preparing per *cell* would be
        /// both slower and heavier -- 36k x ~160 B is ~5.8 MB, three times what the peak-memory budget
        /// allows (docs/PERFORMANCE.md:166 records 20.8 MB), while the distinct set costs ~0.4 MB.
        /// The key keeps the source apart: the same spelling read from `word.txt` and from the phrase
        /// table is two entries, because `Syllable::source` says which table it came from.
        class ReadingTable {
        public:
            explicit ReadingTable(ToneNotation notation) : m_notation(notation) {}

            /// Immediately returns the entry if (`stored`, `source`) was prepared before; prepares it
            /// otherwise. The returned reference stays valid until the table is destroyed.
            const PreparedReading &prepare(const std::u16string &stored, ReadingSource source);

            /// The entry for (`stored`, `source`) when it was prepared before, nullptr otherwise.
            ///
            /// CONST on purpose, and that is the whole difference from `prepare`: the query path runs
            /// on objects several threads read at once (the ThreadSafety suite), and an inserting
            /// lookup there would be a data race waiting for the one reading a load-time pass missed.
            /// `EngineImpl` walks every table at load, so the query path only ever needs this.
            [[nodiscard]] const PreparedReading *find(const std::u16string &stored, ReadingSource source) const;

            /// Distinct (reading, source) pairs held.
            [[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }
            /// How many of them the structure can rebuild (`PreparedReading::rebuilt`).
            [[nodiscard]] std::size_t rebuiltCount() const noexcept { return m_rebuilt; }
            [[nodiscard]] std::size_t notRebuiltCount() const noexcept { return m_entries.size() - m_rebuilt; }

        private:
            struct Key {
                std::u16string reading;
                ReadingSource source = ReadingSource::dictionary;
                bool operator==(const Key &other) const { return source == other.source && reading == other.reading; }
            };
            struct KeyHash {
                std::size_t operator()(const Key &key) const {
                    return std::hash<std::u16string>{}(key.reading) ^
                        (static_cast<std::size_t>(key.source) * 0x9E3779B97F4A7C15ull);
                }
            };

            ToneNotation m_notation;
            std::unordered_map<Key, PreparedReading, KeyHash> m_entries;
            std::size_t m_rebuilt = 0;
        };
    }  // namespace detail
}  // namespace cpp_pinyin::v2
