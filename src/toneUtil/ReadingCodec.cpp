#include "ReadingCodec.h"
#include "Utf8Conv.h"

#include <cpp-pinyin2/core/U16Str.h>
#include <cpp-pinyin2/dialects/ManTone.h>

#include "ManToneUtil.h"

namespace cpp_pinyin::v2
{
    namespace detail
    {
        namespace
        {
            bool isDigit16(const char16_t c) { return c >= u'1' && c <= u'9'; }

            bool isAsciiLetter16(const char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); }

            // jyutping finals all begin with a vowel; 'y' belongs to the final (`jyu`, `syu`), never
            // to the initial, so the longest leading consonant run is the dialect's own initial.
            size_t jyutpingInitialLength(const std::u16string &reading) {
                size_t i = 0;
                while (i < reading.size() && isAsciiLetter16(reading[i])) {
                    switch (reading[i]) {
                    case u'a':
                    case u'e':
                    case u'i':
                    case u'o':
                    case u'u':
                    case u'y':
                        return i;
                    default:
                        ++i;
                    }
                }
                return i;
            }

            /// Undoes the rewrites `ManTone::canonicalFinals()` performs (y/w of a zero-initial
            /// syllable, `ü` after j/q/x, `iou`/`uei`/`uen` contraction), in that function's own order;
            /// each branch is one of theirs inverted, kept in step by `test_syllable_producer.cpp`.
            /// Empty means "not invertible": no final (`ǹ`/`ḿ`, `hm`), or a lone `o` behind no initial --
            /// the loose retry ate the `y` of `yo`, so `o` spells both `o` and `yo` and keeps the first.
            std::u16string mandarinSurfaceToneless(const std::u16string &initial, const std::u16string &fin_v) {
                std::u16string fin = fin_v;
                // The structure stores ü as v (`Syllable::fin` says so); the surface spells it ü.
                for (char16_t &c : fin)
                    if (c == u'v')
                        c = u'ü';

                if (fin.empty())
                    return {};

                if (initial.empty()) {
                    if (fin == u"o")
                        return {};
                    if (fin[0] == u'ü')  // yu/ yue/ yuan/ yun
                        return u"yu" + fin.substr(1);
                    if (fin[0] == u'u')  // wu -> u (kept), wa -> ua, wen -> uen
                        return fin == u"u" ? std::u16string(u"wu") : u"w" + fin.substr(1);
                    if (fin[0] == u'i')  // yi -> i (kept), ya -> ia, you -> iou
                        return (fin.size() == 1 || fin[1] == u'n') ? u"y" + fin : u"y" + fin.substr(1);
                    // a/e/ê-initial finals are spelled the same on both sides
                    return fin;
                }

                if (!fin.empty() && fin[0] == u'ü' && (initial[0] == u'j' || initial[0] == u'q' || initial[0] == u'x'))
                    fin[0] = u'u';

                if (fin == u"uei")
                    fin = u"ui";
                else if (fin == u"iou")
                    fin = u"iu";
                else if (fin == u"uen")
                    fin = u"un";

                return initial + fin;
            }
        }  // namespace

        ToneNotation notationOf(const ToneConverter &converter) {
            // "zhi4" is the shape every external Mandarin table writes and every stored Mandarin
            // reading has already left behind (ManTone::normalizeReading's own documented example).
            return converter.normalizeReading(u"zhi4") == u"zhi4" ? ToneNotation::trailingDigit
                                                                  : ToneNotation::toneMarked;
        }

        Syllable parseReading(const std::u16string &stored, ToneNotation notation, ReadingSource source) {
            Syllable out;
            out.source = source;
            if (stored.empty())
                return out;  // tone_kind stays `unspecified`: there was no entry to describe
            out.tone_kind = ToneKind::base;
            // The row's identity: the table's own bytes, the string this function was handed. The two
            // halves below are a *projection* of it and cannot rebuild every reading (see `surfaceOf`),
            // so a consumer that has to render this reading again takes the spelling, not the halves.
            out.spelling = toUtf8(stored);

            if (notation == ToneNotation::trailingDigit) {
                std::u16string body = stored;
                if (isDigit16(body.back())) {
                    out.tone = body.back() - u'0';
                    body.pop_back();
                }
                const size_t split = jyutpingInitialLength(body);
                out.initial = toUtf8(body.substr(0, split));
                out.fin = toUtf8(body.substr(split));
                return out;
            }

            // A Mandarin table spells the tone with a diacritic, and a digit may also appear -- the
            // tone3 form an external table uses (`user_dict.txt` writes `破的:po4 de5`) and that
            // `normalizeReading` rewrites into a diacritic. A trailing digit is notation, not part of
            // the syllable, so the two halves are split from the reading without it: with it the final
            // table would see `e5` and the reading would come back unsplit. (The engine's own stored
            // readings carry no digit at all -- `tone3ToTone` drops 5 and 0 -- but a caller may hand
            // the codec either spelling.)
            std::u16string splitSource = stored;
            const bool spelledAsDigit = isDigit16(stored.back());
            if (spelledAsDigit && stored.back() <= u'5') {
                out.tone = stored.back() - u'0';
                splitSource.pop_back();
            }

            out.initial = toUtf8(ManTone::toneToInitials(splitSource, false, false));
            out.fin = toUtf8(ManTone::toneToFinals(splitSource, false, false));

            if (!spelledAsDigit) {
                // Scan default: a mark-free reading has no tone, so the neutral 5 folds back to 0; the
                // render-time 5 is the converter's `markNeutralAs5`, not data. THE RULING ON `de`/`de5`
                // (S5 decision 2), measured not inferred: `normalizeReading` (= `tone3ToTone`, dropping
                // 5 and 0 in its normalization loop) turns a table's `de5` into `de`: both spellings are
                // ONE stored reading, `tone` can only be 0 and the 5 is a render option, never data
                // ("词表原始注音是唯一权威", the tone rules at the top of core/Syllable.h); `spelledAsDigit` is a
                // table's own notation, i.e. data (test pins `de`->0, `de5`->5).
                const std::u16string tone3 = ManTone::toneToTone3(stored, false, true);
                if (!tone3.empty() && isDigit16(tone3.back()) && tone3.back() <= u'5')
                    out.tone = tone3.back() - u'0';
                if (out.tone == 5)
                    out.tone = 0;
            }
            return out;
        }

        std::u16string surfaceOf(const Syllable &syllable, ToneNotation notation) {
            if (notation == ToneNotation::trailingDigit) {
                std::u16string surface = toU16(syllable.initial) + toU16(syllable.fin);
                if (syllable.tone > 0)
                    surface += static_cast<char16_t>(u'0' + syllable.tone);
                return surface;
            }

            const std::u16string toneless = mandarinSurfaceToneless(toU16(syllable.initial), toU16(syllable.fin));
            if (toneless.empty())
                return {};
            // 0 = 轻声 with no mark at all (the "de" spelling): the surface is the plain syllable.
            if (syllable.tone <= 0)
                return toneless;
            // 5 is only ever the reading's own literal digit (the tone3 spelling `de5`): the diacritic
            // table has no 5, and a mark-free reading carries tone 0 above. Rebuilding it through
            // tone3ToTone would *drop* the digit (that function strips 5 and 0), so it goes back
            // literally -- that is what the table wrote.
            if (syllable.tone == 5)
                return toneless + static_cast<char16_t>(u'5');
            if (syllable.tone > 5)
                return {};  // not a Mandarin tone number: do not invent a surface for it

            std::u16string marked = tone3ToTone(toneless + static_cast<char16_t>(u'0' + syllable.tone));
            // tone3ToTone spells ü as v (that is the tone3 convention) while the tables spell it ü,
            // and it leaves the ü unmarked whenever rightMarkIndex() picked another vowel of the
            // final (`lüè`, `nüè`). Pinyin has no literal v, so this only puts the table's own
            // spelling back.
            for (char16_t &c : marked)
                if (c == u'v')
                    c = u'ü';
            return marked;
        }

        std::u16string renderReading(const Syllable &syllable, ToneNotation notation, const ToneConverter &converter,
                                     int style, bool v_to_u, bool neutral_tone_with_five) {
            std::u16string surface = surfaceOf(syllable, notation);
            if (surface.empty())
                return {};
            return converter.convert(std::move(surface), style, v_to_u, neutral_tone_with_five);
        }

        PreparedReading prepareReading(const std::u16string &stored, ToneNotation notation, ReadingSource source) {
            PreparedReading out;
            out.data = parseReading(stored, notation, source);

            // Trusted only when the structure rebuilds the very string it was parsed from, byte for
            // byte: that equality IS what "invertible" means here, so a reading whose projection lost
            // information is never rendered from the structure (the row keeps the table's string and
            // stays correct; `rebuilt` blames the structure instead). Either way `surface == stored`,
            // which is what makes the query path free -- it renders without asking the codec (S6).
            out.surface = surfaceOf(out.data, notation);
            out.rebuilt = (out.surface == stored);
            if (!out.rebuilt)
                out.surface = stored;
            return out;
        }

        const PreparedReading &ReadingTable::prepare(const std::u16string &stored, ReadingSource source) {
            Key key{stored, source};
            const auto it = m_entries.find(key);
            if (it != m_entries.end())
                return it->second;

            PreparedReading prepared = prepareReading(stored, m_notation, source);
            if (prepared.rebuilt)
                ++m_rebuilt;
            return m_entries.emplace(std::move(key), std::move(prepared)).first->second;
        }

        const PreparedReading *ReadingTable::find(const std::u16string &stored, ReadingSource source) const {
            const Key key{stored, source};
            const auto it = m_entries.find(key);
            return it == m_entries.end() ? nullptr : &it->second;
        }
    }  // namespace detail
}  // namespace cpp_pinyin::v2
