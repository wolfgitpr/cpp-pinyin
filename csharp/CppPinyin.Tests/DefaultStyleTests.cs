using System.Collections.Generic;
using CppPinyin2;
using Xunit;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// The shell default must mean "whatever the dialect defaults to", not one shell's idea of a
    /// neutral rendering. `Style.Default` is the sentinel the library resolves against the dialect's
    /// default (`styles[].isDefault`, read by `defaultStyleOf` and resolved by `resolveStyle` in `core/Types.h`); `Readings` was the one entry point still hard-coding
    /// `Style.Normal`, so a C# caller got `zhong` where every other entry point -- and the Python
    /// shell -- gives the dialect default `zhōng`.
    ///
    /// The expectation is measured rather than chosen: `src/Mandarin.cpp` marks `ManTone::Style::TONE`
    /// as the `styles[]` default (`isDefault`), so the sentinel and `Style.Tone` must agree -- and, by the same
    /// measurement, must NOT agree with `Style.Normal`.
    /// </summary>
    public class DefaultStyleTests
    {
        [Fact]
        public void Readings_uses_the_dialect_default_style()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            IReadOnlyList<string> bySentinel = engine.Readings("\u4e2d");              // no style given
            IReadOnlyList<string> askedForTone = engine.Readings("\u4e2d", Style.Tone);
            IReadOnlyList<string> askedForNormal = engine.Readings("\u4e2d", Style.Normal);

            Assert.Equal(askedForTone, bySentinel);
            Assert.NotEqual(askedForNormal, bySentinel);
        }

        /// <summary>
        /// The two switches reach the library for <c>readings</c> too, not just for <c>query</c>.
        /// <para>
        /// They used to be missing from this entry point while <c>cppp2_readings</c> honoured them, so a
        /// C# caller got a different answer than a C caller for the same arguments (the gap was recorded
        /// in <c>docs/PLUGIN_INTERFACE_AND_BINDINGS.md</c> as "待补"). The expectations are measured
        /// against <c>data/dialects</c>; the Python shell asserts the same lists in
        /// <c>python/tests/test_conformance.py</c>, so the two shells are pinned to one answer.
        /// </para>
        /// </summary>
        [Fact]
        public void Readings_passes_the_two_switches_through()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            // ü is written v by default; the switch is what makes the second reading of 女 distinct.
            Assert.Equal(new[] { "nv3", "nv" }, engine.Readings("\u5973", Style.Tone3));
            Assert.Equal(new[] { "nv3", "n\u00fc" }, engine.Readings("\u5973", Style.Tone3, vToU: true));

            // The neutral tone reports its number only when asked for.
            Assert.Equal(new[] { "de", "di4", "di1", "di2" }, engine.Readings("\u7684", Style.Tone3));
            Assert.Equal(
                new[] { "de5", "di4", "di1", "di2" },
                engine.Readings("\u7684", Style.Tone3, markNeutralAs5: true));
        }
    }
}