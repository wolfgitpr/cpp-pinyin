using System;
using Xunit;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// A style the dialect does not declare must be refused by the shell instead of rendered anyway
    /// (N20=A). The library's pass-through is deliberate -- a style it cannot apply comes back
    /// unchanged -- but a shell has names, and the engine hands it the dialect's OWN style table, so
    /// the shell refuses by name. Measured, not assumed, before these assertions were written:
    ///
    ///     mandarin  : 0 normal, 1 tone (default), 2 tone2, 3 first-letter, 4 bopomofo, 5 initials,
    ///                 6 finals, 7 finals-tone3, 8 tone3
    ///     cantonese : 0 normal, 3 first-letter, 8 tone3 (default)
    ///
    /// The Python half of this batch learned the same lesson first: an early draft called
    /// `cantonese + 3` a defect, and the table said otherwise -- cantonese declares first-letter
    /// itself. So the positive case is pinned here too: a check that over-rejects would be worse than
    /// the gap it closes.
    /// </summary>
    public sealed class StyleValidationTests
    {
        private const string Text = "\u4e2d\u56fd";

        [Fact]
        public void A_style_the_dialect_does_not_declare_is_refused()
        {
            using var engine = new Engine("cantonese", RepoLayout.DictDirectory);
            // 1 is Mandarin's default and is absent from Cantonese's three rows.
            CppPinyinInvalidArgumentException error = Assert.Throws<CppPinyinInvalidArgumentException>(
                () => engine.Query(Text, (Style)1).ToText());
            Assert.Contains("1", error.Message, StringComparison.Ordinal);
            Assert.Contains("cantonese", error.Message, StringComparison.Ordinal);
        }

        [Fact]
        public void An_out_of_range_style_is_refused()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);
            Assert.Throws<CppPinyinInvalidArgumentException>(() => engine.Query(Text, (Style)9).ToText());
            // Only -1 is the sentinel; it must keep working, and it must equal the dialect's default.
            Assert.Equal(engine.Query(Text, Style.Tone).ToText(), engine.Query(Text, Style.Default).ToText());
        }

        [Fact]
        public void Every_declared_style_still_works()
        {
            using var mandarin = new Engine("mandarin", RepoLayout.DictDirectory);
            using var cantonese = new Engine("cantonese", RepoLayout.DictDirectory);
            Assert.Equal("z g", mandarin.Query(Text, (Style)3).ToText());
            // Cantonese declares first-letter itself (alias 3): the value an early draft rejected.
            Assert.Equal("z g", cantonese.Query(Text, (Style)3).ToText());
            Assert.Equal(cantonese.Query(Text).ToText(), cantonese.Query(Text, Style.Tone3).ToText());
        }
    }
}