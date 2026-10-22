#include <cpp-pinyin2/core/ToneSandhi.h>

#include <string>
#include <vector>

namespace cpp_pinyin::v2
{
    namespace
    {
        // The shape of a TONE3 reading: letters (including multibyte ones such as UTF-8's ü) followed by a
        // single trailing tone digit.
        // With a neutral tone that got no 5 (markNeutralAs5=false) there is no digit, so this returns
        // false -- consistent with pypinyin's
        // tone_to_tone2('le') == 'le': when no tone can be read, treat it as "not a fourth tone".
        bool parseTone3(const std::string &reading, int &tone) {
            if (reading.size() < 2)
                return false;
            const char last = reading.back();
            if (last < '1' || last > '5')
                return false;
            for (size_t i = 0; i + 1 < reading.size(); ++i) {
                const auto c = static_cast<unsigned char>(reading[i]);
                const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
                if (!letter)
                    return false;
            }
            tone = last - '0';
            return true;
        }

        void setTone(std::string &reading, const int tone) { reading.back() = static_cast<char>('0' + tone); }

        bool isAllAsciiDigits(const std::string &s) {
            if (s.empty())
                return false;
            for (const char c : s)
                if (c < '0' || c > '9')
                    return false;
            return true;
        }
    }  // namespace

    size_t applyMandarinToneSandhi(Result &result, const ToneSandhiOptions &options) {
        const size_t n = result.size();
        if (n == 0 || (!options.thirdTone && !options.bu && !options.yi))
            return 0;

        // The rewrite still writes the RENDERED string -- byte for byte what it always wrote -- and
        // mirrors the new tone number into the row's structured reading, so the two descriptions of
        // one row cannot disagree. Making the structure the *subject* of the rewrite (and stamping
        // `ToneKind::sandhi` on it) is the plan's later slice: nothing in this repository produces
        // that value yet, and changing which field is authoritative here would move bytes.
        //
        // RULED (S8), and this is why the sync below stays unconditional: the post-process receives a
        // `Result` and does not know whether the caller asked for structured readings. When the
        // switch was off, `reading.tone` is therefore the ONE field this function writes into an
        // otherwise default-constructed `Syllable` -- "the sandhi rule's own output", which is not
        // something a caller can be told to ignore by accident. `initial`/`fin` stay empty, so the
        // structure still describes nothing beyond the rewritten tone. Whether the structure should
        // BECOME the rewrite's subject (`ToneKind::sandhi`, `rendered` rendered from it) is decided
        // in a later slice; until then the bytes here are unchanged either way.
        auto rewriteTone = [&](const size_t row, const int tone)
        {
            setTone(result[row].rendered, tone);
            result[row].reading.tone = tone;
        };

        // tones[i] == 0 means no tone could be read for that row (a failed row, a non-TONE3 style, or a
        // neutral tone with no 5 appended).
        std::vector<int> tones(n, 0);
        for (size_t i = 0; i < n; ++i) {
            if (result[i].failure_kind != Code::Ok)
                continue;
            int tone = 0;
            if (parseTone3(result[i].rendered, tone))
                tones[i] = tone;
        }

        size_t changed = 0;

        // A run of third tones: in every consecutive run, all but the last become second.
        if (options.thirdTone) {
            size_t i = 0;
            while (i < n) {
                if (tones[i] != 3) {
                    ++i;
                    continue;
                }
                size_t end = i;
                while (end < n && tones[end] == 3)
                    ++end;
                for (size_t k = i; k + 1 < end; ++k) {
                    rewriteTone(k, 2);
                    tones[k] = 2;
                    ++changed;
                }
                i = end;
            }
        }

        // 不 and 一 depend on the NEXT row's tone. Failed rows and the end of the result both count as
        // boundaries.
        for (size_t i = 0; i < n; ++i) {
            const bool isBu = options.bu && result[i].text == "不";
            const bool isYi = options.yi && result[i].text == "一";
            if (!isBu && !isYi)
                continue;
            if (tones[i] == 0)
                continue;

            const bool atBoundary = (i + 1 >= n) || result[i + 1].failure_kind != Code::Ok;
            int want;
            if (atBoundary) {
                want = isBu ? 4 : 1;
            } else if (tones[i + 1] == 4) {
                want = 2;
            } else if (isYi && (result[i + 1].text == "一" || isAllAsciiDigits(result[i + 1].text))) {
                // A 一 in a numeral or count reads first tone when a digit (or another 一) follows it.
                want = 1;
            } else {
                want = 4;
            }

            if (tones[i] == want)
                continue;
            rewriteTone(i, want);
            tones[i] = want;
            ++changed;
        }

        return changed;
    }
}  // namespace cpp_pinyin::v2
