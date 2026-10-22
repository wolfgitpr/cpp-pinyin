using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Xunit;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// Binding-level behaviour that the fixture cannot cover: the version chain's C# link, the
    /// <see cref="SafeHandle"/> lifecycle, the failure messages, the ABI structures' layout, the entry
    /// points the fixture does not reach (the structured reading, the renderer, the code text), and the
    /// portable UTF-8 decoder that the netstandard2.0 target depends on.
    /// </summary>
    public sealed class SmokeTests
    {
        [Fact]
        public void Tone_convert_moves_a_stored_reading_between_styles()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            // The C face of the dialect's own converter, fed the tone3 form (letters plus a tone digit) that user
            // dictionaries are written in and that normalizeReading takes. Same expectations as the C++ case
            // (tests/cpp/test_c_api.cpp) and the Python case (python/tests/test_shell.py), and the same numbers
            // pypinyin 0.55.0 gives for the four styles whose numbering the two libraries share.
            Assert.Equal("nǐ", engine.ToneConvert("ni3", Style.Tone));
            Assert.Equal("ha3o", engine.ToneConvert("hao3", Style.Tone2));
            Assert.Equal("zhong", engine.ToneConvert("zhong1", Style.Normal));
            Assert.Equal("zh", engine.ToneConvert("zhong1", Style.Initials));

            // No style means the dialect's DECLARED default. This line is the C# half of the defect the
            // consistency audit found on 2026-10-22: the sentinel -1 reached the converter unresolved, so the
            // default argument answered the input string with a success code.
            Assert.Equal("nǐ", engine.ToneConvert("ni3"));
        }

        [Fact]
        public void Tone_convert_refuses_what_is_not_the_tone3_form()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            // Measured 2026-10-21: the converter does not reject a marked or toneless reading, it mangles it
            // ("ni" -> "nii", "nǐ" -> "nii3"), so the boundary refuses the shape instead of returning garbage.
            Assert.Throws<CppPinyinInvalidArgumentException>(() => engine.ToneConvert("ni", Style.Tone));
            Assert.Throws<CppPinyinInvalidArgumentException>(() => engine.ToneConvert("nǐ", Style.Tone3));
            // One reading, not a phrase: separators are refused here; splitting is the query path's job.
            Assert.Throws<CppPinyinInvalidArgumentException>(() => engine.ToneConvert("ni3 hao3", Style.Tone));
        }
        [Fact]
        public void Tone_sandhi_is_the_library_rules_and_only_rewrites_readings()
        {
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            // Same input and same expectations as the C++ case (tests/cpp/test_tone_sandhi.cpp) and the
            // C case (tests/cpp/test_c_api.cpp): one sentence per rule, plus a control that must not move.
            Assert.Equal(new[] { "ni2", "hao3" }, Rendered(engine.ToneSandhi("你好", Style.Tone3)));
            Assert.Equal(new[] { "bu2", "shi4" }, Rendered(engine.ToneSandhi("不是", Style.Tone3)));
            Assert.Equal(new[] { "yi2", "ding4" }, Rendered(engine.ToneSandhi("一定", Style.Tone3)));
            Assert.Equal(new[] { "xiao3", "xin1" }, Rendered(engine.ToneSandhi("小心", Style.Tone3)));

            // The post-processing is what changed it: a plain Query on the same input still renders the
            // table readings, so this is not a change to the query path.
            Assert.Equal(new[] { "ni3", "hao3" }, Rendered(engine.Query("你好", Style.Tone3)));

            // Punctuation is a boundary, not part of a run: the comma row keeps its own text and the run
            // after it still applies.
            Assert.Equal(new[] { "yi1", "，", "ni2", "hao3" }, Rendered(engine.ToneSandhi("一，你好", Style.Tone3)));

            // Both switches off is the caller's own value, not the library default: nothing is rewritten.
            Assert.Equal(new[] { "ni3", "hao3" },
                Rendered(engine.ToneSandhi("你好", Style.Tone3, thirdTone: false, bu: false, yi: false)));
        }

        private static string[] Rendered(System.Collections.Generic.IReadOnlyList<Segment> spans)
        {
            var rendered = new string[spans.Count];
            for (int i = 0; i < spans.Count; i++)
            {
                rendered[i] = spans[i].Rendered;
            }

            return rendered;
        }
        [Fact]
        public void The_native_library_reports_the_version_generated_from_cmakelists()
        {
            // Two independent paths to the same number: emit_build_info.py parses CMakeLists.txt (the
            // single source of truth) into version.props, and the native library reports its own value.
            // If they disagree, one of them is stale -- which is exactly what version chain place 5 is for.
            string props = File.ReadAllText(Path.Combine(RepoLayout.Root, "csharp", "CppPinyin", "version.props"));
            Match match = Regex.Match(props, @"<Version>([^<]+)</Version>");
            Assert.True(match.Success, "version.props has no <Version>: run python csharp/tools/emit_build_info.py");
            Assert.Equal(match.Groups[1].Value, Engine.Version);
        }

        [Fact]
        public void Disposing_then_using_throws_ObjectDisposedException()
        {
            var engine = new Engine("mandarin", RepoLayout.DictDirectory);
            engine.Dispose();

            // This is what SafeHandle buys: the call cannot reach a freed pointer, and it cannot silently
            // succeed either. A bare IntPtr would do either.
            Assert.Throws<ObjectDisposedException>(() => engine.Query("你好"));
            Assert.Throws<ObjectDisposedException>(
                () => engine.RenderSyllable(new Syllable(string.Empty, "zhi4", 0, 0, 0)));
            engine.Dispose(); // idempotent
        }

        [Fact]
        public void An_unknown_dialect_reports_the_library_message()
        {
            // cppp2_open reports an id the registry does not know as CPPP2_ERR_NOT_READY -- the status the old
            // entry point collapsed into "NULL handle plus a message" -- so the shell can name the failure
            // instead of guessing, and the message still carries both the id and the directory it looked in.
            CppPinyinUnknownDialectException error = Assert.Throws<CppPinyinUnknownDialectException>(
                () => new Engine("klingon", RepoLayout.DictDirectory));

            Assert.Contains("klingon", error.Message, StringComparison.Ordinal);
            Assert.Contains(RepoLayout.DictDirectory, error.Message, StringComparison.Ordinal);
        }

        [Fact]
        public void A_missing_dictionary_directory_is_reported_not_defaulted()
        {
            string missing = Path.Combine(RepoLayout.Root, "data", "dialects-does-not-exist");
            CppPinyinDictionaryException error = Assert.Throws<CppPinyinDictionaryException>(
                () => new Engine("mandarin", missing));
            Assert.Contains(missing, error.Message, StringComparison.Ordinal);
        }

        [Fact]
        public void The_abi_structures_are_the_headers_structs_field_for_field()
        {
            // The layout IS the contract (c_api.h): the library reads cppp2_query_opts at the offsets the C
            // compiler gave it, so a transcription that holds "the same fields in a different order" -- or
            // one that omits reserved_0 and lets include_reading slide back into the bytes the FIRST
            // declaration of the struct ended in (S9c) -- compiles clean, works everywhere else, and
            // silently stops asking for the structure. Offsets are expressed relative to IntPtr.Size so
            // this pins the header's order and not one host's pointer width.
            int pointer = IntPtr.Size;

            // size_t struct_size, five int32 options, then reserved_0, then include_reading appended AFTER
            // it: the new option must never sit inside the old trailing padding.
            Assert.Equal(0, OffsetOf<QueryOptions>("struct_size"));
            Assert.Equal(pointer, OffsetOf<QueryOptions>("style"));
            Assert.Equal(pointer + 4, OffsetOf<QueryOptions>("on_error"));
            Assert.Equal(pointer + 8, OffsetOf<QueryOptions>("include_candidates"));
            Assert.Equal(pointer + 12, OffsetOf<QueryOptions>("v_to_u"));
            Assert.Equal(pointer + 16, OffsetOf<QueryOptions>("mark_neutral_as_5"));
            Assert.Equal(pointer + 20, OffsetOf<QueryOptions>("reserved_0"));
            Assert.Equal(pointer + 24, OffsetOf<QueryOptions>("include_reading"));

            // The declared size (what NewOptions writes into struct_size) must COVER include_reading, or
            // the library's size gate ignores the field -- which is exactly how S9c's bug read.
            Assert.True(
                Marshal.SizeOf<QueryOptions>() >= OffsetOf<QueryOptions>("include_reading") + 4,
                "sizeof(QueryOptions) does not cover include_reading");
            Assert.Equal(pointer == 8 ? 40 : 32, Marshal.SizeOf<QueryOptions>());

            // cppp2_syllable: three strings (the identity, then its two projected halves), then the three
            // int32 value domains.
            Assert.Equal(0, OffsetOf<SyllableRaw>("initial"));
            Assert.Equal(pointer, OffsetOf<SyllableRaw>("fin"));
            Assert.Equal(pointer * 2, OffsetOf<SyllableRaw>("spelling"));
            Assert.Equal(pointer * 3, OffsetOf<SyllableRaw>("tone"));
            Assert.Equal((pointer * 3) + 4, OffsetOf<SyllableRaw>("tone_kind"));
            Assert.Equal((pointer * 3) + 8, OffsetOf<SyllableRaw>("source"));

            // cppp2_segment: failure_kind sits between two pointers and is followed by padding, which is why
            // the ORDER -- not merely the field set -- has to be the header's.
            Assert.Equal(0, OffsetOf<SegmentRaw>("text"));
            Assert.Equal(pointer, OffsetOf<SegmentRaw>("rendered"));
            Assert.Equal(pointer * 2, OffsetOf<SegmentRaw>("failure_kind"));
            Assert.Equal(pointer * 3, OffsetOf<SegmentRaw>("syllables"));
            Assert.Equal(pointer * 4, OffsetOf<SegmentRaw>("syllable_count"));
            Assert.Equal(pointer * 5, OffsetOf<SegmentRaw>("candidate_count"));
            Assert.Equal(pointer * 6, OffsetOf<SegmentRaw>("candidates"));

            // cppp2_result: the count, then the segments array.
            Assert.Equal(0, OffsetOf<ResultRaw>("count"));
            Assert.Equal(pointer, OffsetOf<ResultRaw>("segments"));

            // cppp2_error: the code the caller branches on, then the message the caller frees.
            Assert.Equal(0, OffsetOf<ErrorRaw>("code"));
            Assert.Equal(pointer, OffsetOf<ErrorRaw>("detail"));
        }

        [Fact]
        public void The_reading_structure_is_built_only_when_it_is_asked_for()
        {
            using (var engine = new Engine("mandarin", RepoLayout.DictDirectory))
            {
                // "Not requested" and "this span has no reading" are two different states, and only
                // failure_kind tells them apart -- so the first half of this test is that the default
                // builds no structure while rendered still carries the reading.
                Segment plain = engine.Query("你好", Style.Tone3)[0];
                Assert.Empty(plain.Syllables);
                Assert.Equal("ni3", plain.Rendered);

                Segment asked = engine.Query("你好", Style.Tone3, includeReading: true)[0];
                Syllable reading = Assert.Single(asked.Syllables);
                Assert.Equal("n", reading.Initial);
                Assert.Equal("i", reading.Fin);
                Assert.Equal(3, reading.Tone);

                Segment failed = engine.Query("abc你", Style.Tone3, includeReading: true)[0];
                Assert.Equal(Status.InvalidArgument, failed.FailureKind);
                Assert.Equal("abc", failed.Rendered);
                Assert.Empty(failed.Syllables); // a failed span deliberately does not enter the array
            }
        }

        [Fact]
        public void The_librarys_own_renderer_reproduces_a_spans_rendering()
        {
            using (var engine = new Engine("mandarin", RepoLayout.DictDirectory))
            {
                // The shell owns no style: this is the library's renderer, asked through its own entry point.
                foreach (Segment span in engine.Query("你好世界", Style.Tone3, includeReading: true))
                {
                    Assert.Equal(span.Rendered, engine.RenderSyllable(Assert.Single(span.Syllables), Style.Tone3));
                }

                // The single-syllable contract is checkable at the boundary: a structure carrying a whole
                // multi-syllable string is refused rather than rendered into something invented.
                var multi = new Syllable(string.Empty, "zhi4 fa2", 0, 0, 0);
                Assert.Throws<CppPinyinInvalidArgumentException>(() => engine.RenderSyllable(multi));
            }
        }

        [Fact]
        public void A_reading_the_projection_cannot_rebuild_still_renders()
        {
            using (var engine = new Engine("mandarin", RepoLayout.DictDirectory))
            {
                // `hm` (the interjection) has no final at all, so the (initial, fin) projection comes back
                // empty-handed. Before the identity field existed this entry could only be refused -- which
                // reads as "your structure is malformed" about a reading the library had merely taken apart
                // lossily.
                Segment span = engine.Query("噷", Style.Tone3, includeReading: true)[0];
                Syllable reading = Assert.Single(span.Syllables);
                Assert.Equal("hm", reading.Spelling);
                Assert.Equal(string.Empty, reading.Fin);
                Assert.Equal(span.Rendered, engine.RenderSyllable(reading, Style.Tone3));

                // Drop the identity and the SAME structure is refused: that is the library having nothing to
                // render, not the shell refusing a reading it could not invert.
                var halves = new Syllable(reading.Initial, reading.Fin, reading.Tone, reading.ToneKind,
                                          reading.Source);
                Assert.Equal(string.Empty, halves.Spelling);
                Assert.Throws<CppPinyinInvalidArgumentException>(
                    () => engine.RenderSyllable(halves, Style.Tone3));
            }
        }

        [Fact]
        public void A_code_text_comes_from_the_library_not_from_this_shell()
        {
            Assert.False(string.IsNullOrEmpty(Engine.CodeText(Status.Ok)));
            Assert.NotEqual(Engine.CodeText(Status.Ok), Engine.CodeText(Status.InvalidArgument));

            // A value outside the value domain is ANSWERED, not refused (c_api.h: "a value outside the
            // domain above is reported as unknown rather than aborting"), and the library's sentence for
            // that is the literal "unknown code" (src/Outcome.cpp is its single authority).
            Assert.Equal("unknown code", Engine.CodeText(99));
        }

        [Fact]
        public void The_portable_decoder_agrees_with_the_framework_decoder()
        {
            // The netstandard2.0 target decodes UTF-8 by hand (Marshal.PtrToStringUTF8 does not exist
            // there). That code is compiled on every target so this test can run it rather than trusting
            // that it compiles. Multi-byte, mixed and non-BMP text, because a decoder that only handles
            // ASCII would pass a careless test.
            foreach (string text in new[] { "你好世界", "asdf,123!", "略", "你好,世界🙂", "a" })
            {
                using (var native = new Utf8String(text))
                {
                    Assert.Equal(text, Utf8.FromPtrPortable(native.Pointer));
                    Assert.Equal(text, Marshal.PtrToStringUTF8(native.Pointer));
                }
            }

            Assert.Equal(string.Empty, Utf8.FromPtrPortable(IntPtr.Zero));
        }

        [Fact]
        public void The_generated_native_library_name_is_a_plain_file_name()
        {
            // The name is recorded at build time (D20=A) and is handed to DllImport, which appends the
            // platform suffix itself. A recorded name that already carried ".dll" would look for
            // "libcpp-pinyin2.dll.dll" -- and on Windows that failure is a DllNotFoundException nobody
            // would connect to the generator.
            Assert.False(string.IsNullOrEmpty(BuildInfo.NativeLibraryName), "the generated name is empty");
            Assert.DoesNotContain(".", BuildInfo.NativeLibraryName, StringComparison.Ordinal);
            Assert.DoesNotContain("/", BuildInfo.NativeLibraryName, StringComparison.Ordinal);
            Assert.DoesNotContain("\\", BuildInfo.NativeLibraryName, StringComparison.Ordinal);
        }

        /// <summary>The marshalled offset of one field, so the layout assertions read like the header.</summary>
        private static int OffsetOf<T>(string field)
        {
            return Marshal.OffsetOf(typeof(T), field).ToInt32();
        }
    }
}
