// Translation-unit split of the engine's load path.
//
// The functions below were moved here verbatim from EngineImpl.cpp, because that file is over the AC-5
// budget and the load path never runs in the query hot path. Same class, same rules, no interface change.
//
// Scope: this file STARTED as a TU split inside the engine layer, buying the line-count win only. The
// promotion that followed is real -- the layer logic lives in src/dict/PhraseLayers.cpp, and the functions
// here are forwarding shells that supply what belongs to the engine: its reading normalizer and error slot.

#include "EngineImpl.h"

namespace cpp_pinyin::v2
{
    namespace
    {
        // Same bridge as EngineImpl.cpp: the decoding rule lives in dict/PhraseIndex.h, and this
        // keeps the moved call sites unchanged.
        using detail::phraseKeyLength;
    }  // namespace

    bool EngineImpl::loadUserDictLayer(const std::filesystem::path &path, bool clear) {
        // Forwarding shell (P2 split #2, route A): the layer logic now lives in the dict layer. This
        // supplies what belongs to the engine and must NOT live down there -- its dialect reading
        // normalizer (a hardcoded Mandarin one used to mangle Cantonese user dictionaries) and its
        // error slot.
        const bool ok = detail::loadUserDictLayer(
            m_phrases, path, clear,
            [this](const std::u16string &pinyin) { return m_toneConverter.normalizeReading(pinyin); }, m_lastError);
        // The rebuild's answer matters: a failed rebuild leaves the derived index disagreeing with
        // the tables, so the load is a failure even when the layer itself accepted the file. The
        // return value used to be discarded here, which let a half-built engine read as success.
        const bool rebuilt = rebuildPhrases();
        return ok && rebuilt;
    }
    bool EngineImpl::rebuildPhrases() {
        // Forwarding shell (P2 split #2, route A). toSimp() reads m_transTable, which stays in the
        // engine because the query hot path calls it (assemble()), so it is passed in as a callable
        // exactly the way loadAdditionalDict takes its normalizer.
        const bool ok = detail::rebuildPhrases(m_phrases, m_dictDir, [this](char16_t ch) { return toSimp(ch); });
        // A failed rebuild changed nothing: the dict layer assembles the tables in a local and only
        // swaps them in on success, so the index and the prepared readings still describe exactly
        // what they described before. Re-deriving them here anyway is what left a failed reload with
        // a state that two identical runs disagreed about (measured: same library, same input, the
        // failing word moved from 银行 to 音乐 between runs).
        if (!ok)
            return false;
        rebuildPhraseIndex();  // the dict layer only fills the tables; the index and its self-check stay here
        // The phrase layer's readings are new objects after every rebuild, so their prepared structures
        // are rebuilt too (S6). Skipped while the engine is still constructing: init() runs the pass
        // over both layers once, after the last layer change, and m_ready is false until it does.
        // Every later rebuild -- loadUserDict(), loadTraditionalDict() -- goes through here.
        if (m_ready)
            preparePhraseReadings();
        return ok;
    }

    // ---------------------------------------------------------------- load domain
    // Moved here from EngineImpl.cpp by .tmp/split_b5.py (2026-10-20, B5): these three members are the
    // load path's public face -- they run on load, never in the query hot path, and they belong in the
    // TU that already owns `loadUserDictLayer` / `rebuildPhrases`. The text is byte-identical; only the
    // `using detail::phraseKeyLength;` bridge was dropped, because this file has the same bridge at the
    // top of its unnamed namespace (two copies of one using-declaration is legal, one is enough).

    // Custom dictionaries

    bool EngineImpl::loadUserDict(const std::filesystem::path &path, bool clear) {
        return loadUserDictLayer(path, clear);
    }


    void EngineImpl::rebuildPhraseIndex() {
        // The bucket table and the two parallel arrays are built by the dict layer (P2 split #1):
        // that code runs only on load, never in the query hot path, and depends on nothing from this
        // class. The self-check below deliberately stays here -- it calls findPhrase(), which is the
        // hot-path inline reader and must not be pulled into the dict layer (AC-3).
        std::vector<detail::PhraseGate> gates;
        std::vector<uint64_t> keys;
        std::vector<const std::vector<std::u16string> *> values;
        if (!detail::buildPhraseIndex(m_phrases.merged, gates, keys, values)) {
            m_phrases.gates.clear();
            m_phrases.keys.clear();
            m_phrases.values.clear();
            m_phrasePreparedCells.clear();
            m_phraseBucketCells.clear();
            m_phrases.indexReady = false;
            m_phrases.indexChecked = 0;
            return;
        }
        m_phrases.gates.swap(gates);
        m_phrases.keys.swap(keys);
        m_phrases.values.swap(values);
        m_phrases.indexReady = true;

        // The structures of those same readings, bucket for bucket (S7). Built here rather than in
        // the dict layer because `PreparedReading` belongs to the tone layer, and built in this
        // order -- `m_phrases.values` walks the index, `m_readings.prepare` is a hash table -- so the
        // cell array can be addressed by the very index that found the readings. One block, one
        // allocation; the prepare() calls are the same ones preparePhraseReadings() makes, and
        // prepare() dedupes, so a reading reachable through two layers is still one structure.
        m_phrasePreparedCells.clear();
        m_phraseBucketCells.assign(m_phrases.values.size(), nullptr);
        // Reserved first: the array must not move while a bucket's start pointer is taken, or every
        // pointer already stored would dangle (and the rows would read freed memory).
        size_t phraseCells = 0;
        for (const auto *bucket : m_phrases.values)
            phraseCells += bucket->size();
        m_phrasePreparedCells.reserve(phraseCells);
        for (size_t i = 0; i < m_phrases.values.size(); ++i) {
            const auto &readings = *m_phrases.values[i];
            if (!readings.empty())
                m_phraseBucketCells[i] = m_phrasePreparedCells.data() + m_phrasePreparedCells.size();
            for (const auto &reading : readings)
                m_phrasePreparedCells.push_back(&m_readings.prepare(reading, ReadingSource::phrase));
        }

        // Self-check (the index is a correctness-relevant filter, not a hint): every key the
        // window can probe must be found through the index, and findPhrase must never report
        // a hit the dictionary does not have. The first half catches a wrong bucket/offset,
        // the second half a stale or misplaced value pointer.
        size_t checked = 0;
        for (const auto &kv : m_phrases.merged) {
            const size_t len = phraseKeyLength(kv.first);
            if (len < 2 || len > 4)
                continue;
            char16_t phrase[4];
            for (size_t i = 0; i < len; ++i)
                phrase[i] = static_cast<char16_t>(kv.first >> (16 * (len - 1 - i)));
            if (findPhrase(kv.first, phrase, len) != &kv.second) {
                m_phrases.gates.clear();
                m_phrases.keys.clear();
                m_phrases.values.clear();
                m_phrasePreparedCells.clear();
                m_phraseBucketCells.clear();
                m_phrases.indexReady = false;
                m_phrases.indexChecked = 0;
                return;
            }
            checked++;
        }
        m_phrases.indexChecked = checked;
    }


    bool EngineImpl::loadTraditionalDict(const std::filesystem::path &path, bool clear) {
        // Load into a local and swap only on success (Q5): zeroing the live table first meant a failed
        // load destroyed the traditional mapping while isReady() still said true, so every later
        // conversion silently disagreed with the dictionary the caller had asked for.
        decltype(m_transTable) loaded = m_transTable;
        if (clear)
            loaded.fill(0);
        const bool ok = loadDict(path, loaded);
        if (ok)
            m_transTable = std::move(loaded);
        // Rebuild AFTER the conversion table changed: the user-dict aliases are computed with
        // toSimp(), so a stale table would leave them describing the OLD mapping -- the very C-04
        // symptom silently returning -- and a second rebuild with a different table would add a new
        // alias while the old one stayed behind. rebuildPhrases() re-derives the aliases from the
        // user layer each time, so this keeps the alias set a pure function of (user layer, table).
        // Its own return value is deliberately ignored here: it reports the built-in phrases_dict.txt
        // load, and this function reports the traditional-table load through lastError() below.
        if (ok)  // a failed load changed nothing, so nothing derived needs re-deriving (lesson of Q2)
            rebuildPhrases();
        // Same reporting rule as loadUserDictLayer: a failing explicit load must be visible
        // through lastError() instead of only through the bool.
        if (!ok && (clear || m_lastError.empty()))
            m_lastError = "failed to load traditional dict: " + path.string();
        return ok;
    }
}  // namespace cpp_pinyin::v2
