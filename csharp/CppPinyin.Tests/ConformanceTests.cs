using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Xunit;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// The same fixture the Python suite consumes (PLUGIN doc section 6.8.4), run against this binding.
    /// <para>
    /// The fixture is a lock, not an oracle: its expectations were produced by the engine, so it detects
    /// drift rather than wrongness. The oracle role stays with the C++ suite. What this class proves is
    /// the part no C++ test can: that the C# shell passes the right arguments through the right
    /// marshalling and reports back exactly what the C ABI said.
    /// </para>
    /// </summary>
public sealed class ConformanceTests
    {
        /// <summary>
        /// Ops the fixture carries for the C surface only -- this shell exposes no public method for them, so they
        /// are compared when the fixture is GENERATED, the same split the Python consumer makes with its
        /// <c>C_ABI_ONLY_OPS</c> (python/tests/test_conformance.py:46). Declared once and read by BOTH the guard and
        /// the per-case runner: on 2026-10-20 the guard learned about these three and the runner did not, which is
        /// exactly how six tests went red without anybody seeing it (CI has never run).
        /// </summary>
        internal static readonly HashSet<string> CAbiOnlyOps = new HashSet<string>(StringComparer.Ordinal)
        {
            "style_list", "render_syllable", "apply_tone_sandhi",
        };

        private static readonly Dictionary<string, Engine> Engines = new Dictionary<string, Engine>(StringComparer.Ordinal);

        /// <summary>One test per fixture case, named by the case id so a failure is self-describing.</summary>
        public static IEnumerable<object[]> CaseIds =>
            Fixture.Cases.Select(c => new object[] { c.GetProperty("id").GetString()! });

        // ---- the four reconciliation steps of section 6.8.4, plus the two guards -------------------

        [Fact]
        public void The_fixture_is_not_empty()
        {
            // A guard, not a formality: an empty fixture makes every case test vanish while the suite
            // still reports success. The Python side has the same guard for the same reason.
            Assert.True(Fixture.Cases.Count >= 40, "the fixture holds " + Fixture.Cases.Count + " cases");
            Assert.Equal(Fixture.Cases.Count, Fixture.Root.GetProperty("cases_count").GetInt32());
        }

        [Fact]
        public void Every_case_is_one_this_consumer_runs()
        {
            // The three C-ABI-only ops (2026-10-20, audit round 2 B18) are in the fixture on purpose and this
            // shell has no public method for any of them; they are compared against the C surface when the
            // fixture is GENERATED. Listing them here is what keeps "not run by this consumer" from becoming
            // "not noticed by this consumer" -- an unknown op still fails, per the assert below.
            var known = new HashSet<string>(StringComparer.Ordinal)
            {
                "query", "readings", "to_simplified", "is_polyphonic",
            };
            known.UnionWith(CAbiOnlyOps);
            var dialects = new HashSet<string>(StringComparer.Ordinal) { "mandarin", "cantonese" };
            foreach (JsonElement c in Fixture.Cases)
            {
                string op = c.GetProperty("op").GetString()!;
                Assert.True(known.Contains(op), "case " + c.GetProperty("id").GetString() + " has op '" + op + "' which this consumer does not run: a whitelist that skips it would hide it");
                string dialect = c.GetProperty("dialect").GetString()!;
                Assert.True(dialects.Contains(dialect), "case " + c.GetProperty("id").GetString() + " uses dialect '" + dialect + "' which this consumer does not run");
            }
        }

        [Fact]
        public void The_fixture_bytes_match_the_pinned_sha256()
        {
            string pinned = File.ReadAllText(RepoLayout.PinPath, Encoding.ASCII).Split(' ')[0].Trim().ToLowerInvariant();
            byte[] bytes = File.ReadAllBytes(RepoLayout.FixturePath);
            string actual;
            using (SHA256 sha = SHA256.Create())
            {
                actual = string.Concat(sha.ComputeHash(bytes).Select(b => b.ToString("x2", System.Globalization.CultureInfo.InvariantCulture)));
            }

            // Both halves of the check matter: the hash catches a hand-edited or re-encoded file, and the
            // byte count catches a checkout that rewrote line endings (the repository pins eol=lf for
            // exactly this file, see .gitattributes).
            Assert.Equal(pinned, actual);
            Assert.True(bytes.Length > 1000, "the fixture is suspiciously small: " + bytes.Length + " bytes");
        }

        [Fact]
        public void The_fixture_dialects_match_the_engine_the_dict_directory_and_the_document()
        {
            var fromFixture = new HashSet<string>(Fixture.Root.GetProperty("dialects").EnumerateArray().Select(e => e.GetString()!), StringComparer.Ordinal);
            var fromEngine = new HashSet<string>(Engine.DialectIds(), StringComparer.Ordinal);
            var fromDisk = new HashSet<string>(Directory.GetDirectories(RepoLayout.DictDirectory).Select(d => Path.GetFileName(d)!), StringComparer.Ordinal);

            Assert.Equal(fromEngine, fromFixture);
            Assert.Equal(fromEngine, fromDisk);

            // Reconciliation step 3: the count registered in the document's status cell must equal the
            // fixture's. The status cell (field 2 of the row), not the evidence cell -- that one happens
            // to contain the same digits and would make the check pass even if the registry drifted.
            string? row = File.ReadAllLines(RepoLayout.ConformanceDocPath).FirstOrDefault(l => l.Contains("P3 夹具条数", StringComparison.Ordinal));
            Assert.True(row != null, "no 'P3 夹具条数' row in " + RepoLayout.ConformanceDocPath);
            string[] fields = row!.Split('|');
            Match match = Regex.Match(fields[2], @"\*\*(\d+)\s*（");
            Assert.True(match.Success, "the registry row's status cell has no '**<count>（': " + fields[2]);
            Assert.Equal(Fixture.Cases.Count.ToString(System.Globalization.CultureInfo.InvariantCulture), match.Groups[1].Value);
        }

        // ---- one test per case ---------------------------------------------------------------------

        [Theory]
        [MemberData(nameof(CaseIds))]
        public void Case_matches_the_fixture(string id)
        {
            JsonElement c = Fixture.Find(id);
            string dialect = c.GetProperty("dialect").GetString()!;
            string op = c.GetProperty("op").GetString()!;
            if (CAbiOnlyOps.Contains(op))
            {
                // Not this consumer's question: see CAbiOnlyOps. The failure this replaces was
                // Assert.Fail("op '<x>' has no handler") -- a red test whose reason was that the fixture contains
                // cases for a DIFFERENT surface, which no amount of fixing the shell would have changed.
                return;
            }

            string text = c.GetProperty("text").GetString()!;
            var style = (Style)c.GetProperty("style").GetInt32();
            bool candidates = c.GetProperty("candidates").GetBoolean();
            bool vToU = c.GetProperty("vToU").GetBoolean();
            Engine engine = EngineFor(dialect);

            switch (op)
            {
                case "query":
                {
                    IReadOnlyList<Segment> spans = engine.Query(text, style, candidates, vToU);
                    JsonElement expected = c.GetProperty("expect_syllables");
                    Assert.Equal(expected.GetArrayLength(), spans.Count);
                    for (int i = 0; i < spans.Count; i++)
                    {
                        JsonElement e = expected[i];
                        Assert.Equal(e.GetProperty("text").GetString(), spans[i].Text);
                        Assert.Equal(e.GetProperty("reading").GetString(), spans[i].Rendered);
                        // The fixture's `failed` is a boolean and the ABI's field is a classification (a
                        // cpp_pinyin::v2::Code): "failed" means "not CPPP2_OK", and a passed span must carry
                        // exactly OK -- a code that merely happened to be non-zero would fail this.
                        Assert.Equal(e.GetProperty("failed").GetBoolean(), spans[i].FailureKind != Status.Ok);
                        Assert.Equal(Strings(e.GetProperty("candidates")), spans[i].Candidates);
                    }

                    // A query case's readings array is empty in the fixture; asserting that keeps a
                    // regenerated fixture from quietly moving this assertion to the other array.
                    Assert.Equal(0, c.GetProperty("expect_readings").GetArrayLength());
                    break;
                }

                case "readings":
                    Assert.Equal(Strings(c.GetProperty("expect_readings")), engine.Readings(text, style));
                    Assert.Equal(0, c.GetProperty("expect_syllables").GetArrayLength());
                    break;

                case "to_simplified":
                    Assert.Equal(c.GetProperty("expect").GetString(), engine.ToSimplified(text));
                    break;

                case "is_polyphonic":
                    Assert.Equal(c.GetProperty("expect").GetBoolean(), engine.IsPolyphonic(text));
                    break;

                default:
                    Assert.Fail("op '" + op + "' has no handler; Every_case_is_one_this_consumer_runs should have caught this");
                    break;
            }
        }

        // ---- the scale block -----------------------------------------------------------------------

        [Fact]
        public void Scale_block_rebuilds_to_the_same_input()
        {
            JsonElement scale = Fixture.Scale;
            string text = RebuildScaleText(scale);
            IReadOnlyList<Segment> spans = EngineFor(scale.GetProperty("dialect").GetString()!)
                .Query(text, (Style)scale.GetProperty("style").GetInt32());

            Assert.Equal(scale.GetProperty("length").GetInt32(), text.Length);
            Assert.Equal(scale.GetProperty("expect_span_count").GetInt32(), spans.Count);
            Assert.Equal(Strings(scale.GetProperty("expect_head")), spans.Take(8).Select(s => s.Rendered));
            Assert.Equal(Strings(scale.GetProperty("expect_tail")), spans.Skip(spans.Count - 8).Select(s => s.Rendered));
        }

        /// <summary>
        /// Rebuild the scale input from the block alone -- seed, LCG constants, modulus, mask and
        /// alphabet are all in the file so that a consumer in any language can do exactly this without
        /// carrying a copy of the input. Transcribed from the generator's loop
        /// (<c>tests/conformance_emit.c</c>): the multiply-add happens in 32 bits (C's <c>uint32_t</c>
        /// wraps; C# needs <c>unchecked</c>, which is the default) and only then is the modulus applied.
        /// </summary>
        private static string RebuildScaleText(JsonElement scale)
        {
            uint x = scale.GetProperty("seed").GetUInt32();
            uint multiplier = scale.GetProperty("multiplier").GetUInt32();
            uint increment = scale.GetProperty("increment").GetUInt32();
            uint modulus = scale.GetProperty("modulus").GetUInt32();
            int length = scale.GetProperty("length").GetInt32();
            string[] alphabet = scale.GetProperty("alphabet").EnumerateArray().Select(e => e.GetString()!).ToArray();

            var builder = new StringBuilder(length);
            for (int i = 0; i < length; i++)
            {
                x = unchecked(x * multiplier + increment) % modulus;
                builder.Append(alphabet[(int)(x % (uint)alphabet.Length)]);
            }

            return builder.ToString();
        }

        private static Engine EngineFor(string dialect)
        {
            lock (Engines)
            {
                if (!Engines.TryGetValue(dialect, out Engine? engine))
                {
                    engine = new Engine(dialect, RepoLayout.DictDirectory);
                    Engines[dialect] = engine;
                }

                return engine;
            }
        }

        private static List<string> Strings(JsonElement array) =>
            array.EnumerateArray().Select(e => e.GetString()!).ToList();
    }

    /// <summary>The fixture, parsed once. Access is serialised by xunit's per-class collection.</summary>
    internal static class Fixture
    {
        private static readonly Lazy<JsonDocument> Document = new Lazy<JsonDocument>(
            () => JsonDocument.Parse(File.ReadAllText(RepoLayout.FixturePath, Encoding.UTF8)));

        internal static JsonElement Root => Document.Value.RootElement;

        internal static IReadOnlyList<JsonElement> Cases { get; } =
            Root.GetProperty("cases").EnumerateArray().ToList();

        internal static JsonElement Scale => Root.GetProperty("scale");

        internal static JsonElement Find(string id)
        {
            foreach (JsonElement c in Cases)
            {
                if (string.Equals(c.GetProperty("id").GetString(), id, StringComparison.Ordinal))
                {
                    return c;
                }
            }

            throw new InvalidOperationException("no case with id '" + id + "' in the fixture");
        }
    }
}
