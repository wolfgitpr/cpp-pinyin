using System.Collections.Generic;
using System.IO;
using System.Text;
using Xunit;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// The default of <see cref="Engine.LoadUserDictionary"/> and
    /// <see cref="Engine.LoadTraditionalDictionary"/>: replacing what is loaded so far, as the C++, C and
    /// Python shells do. These are the assertions that pin the default rather than a call site, so they
    /// fail if it drifts back to <c>false</c> (see the note in each test).
    /// <para>
    /// They write their tables next to the repository (under the ignored <c>.tmp</c>) instead of a shared
    /// system temp path: a fixed name in <c>%TEMP%</c> is a race with every other run on the machine, and
    /// this workspace has already had a compiled program refused permission to create files there.
    /// </para>
    /// </summary>
    public sealed class DictionaryLoadTests
    {
        [Fact]
        public void Loading_a_user_dictionary_replaces_the_previous_user_layer_by_default()
        {
            // 2026-10-17 alignment: the C# shell used to declare `clearExisting = false`, so a second
            // load merged and the first file's entries survived. The C++ (include/cpp-pinyin2/core/Engine.h),
            // the C ABI (ext/c_api.h) and the Python shell all defaulted to replacing, and this test is the
            // one that fails under the old default (the second assertion at the end would read the FIRST
            // file's "yi2 dan4" back instead of the built-in "yi1 dan4").
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);
            Assert.Equal("yi1 dan4", Joined(engine.Query("一旦", Style.Tone3))); // no user layer yet

            string first = WriteTable("userdict-first.txt", "一旦:yi2 dan4\n");
            engine.LoadUserDictionary(first); // no argument: the default is what is being pinned
            Assert.Equal("yi2 dan4", Joined(engine.Query("一旦", Style.Tone3)));

            // The second load must drop the first file's entries, and the built-in layer must be back:
            // "replace" means the user layer is replaced, not that the dictionary is emptied.
            string second = WriteTable("userdict-second.txt", "仄声:ze4 sheng1\n");
            engine.LoadUserDictionary(second);
            Assert.Equal("ze4 sheng1", Joined(engine.Query("仄声", Style.Tone3)));
            Assert.Equal("yi1 dan4", Joined(engine.Query("一旦", Style.Tone3)));
        }

        [Fact]
        public void An_explicit_clearExisting_false_still_merges()
        {
            // Merging is not gone, it is no longer the default: a caller that wants it says so by name.
            // Same shape as the C++ case tests/cpp/test_user_dict.cpp ("loadUserDict(clear=false) keeps
            // the built-in phrase layer"), and both loads here pass the parameter explicitly so this test
            // does not depend on the default at all.
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);

            string first = WriteTable("userdict-merge-first.txt", "一旦:yi2 dan4\n");
            engine.LoadUserDictionary(first, clearExisting: false);
            Assert.Equal("yi2 dan4", Joined(engine.Query("一旦", Style.Tone3)));

            string second = WriteTable("userdict-merge-second.txt", "仄声:ze4 sheng1\n");
            engine.LoadUserDictionary(second, clearExisting: false);
            Assert.Equal("ze4 sheng1", Joined(engine.Query("仄声", Style.Tone3)));
            Assert.Equal("yi2 dan4", Joined(engine.Query("一旦", Style.Tone3))); // merged, so still there
        }

        [Fact]
        public void Loading_a_traditional_table_replaces_the_previous_one_by_default()
        {
            // The other half of the same 2026-10-17 alignment (LoadTraditionalDictionary). The shipped
            // table maps both characters, so under the old default `false` the second load merged and the
            // final "學" would still read "学"; replacing makes it fall back to unmapped.
            using var engine = new Engine("mandarin", RepoLayout.DictDirectory);
            Assert.Equal("学", engine.ToSimplified("學"));
            Assert.Equal("台", engine.ToSimplified("臺"));

            string taiOnly = WriteTable("trans-tai.txt", "臺:台\n");
            engine.LoadTraditionalDictionary(taiOnly); // no argument: the default is what is being pinned
            Assert.Equal("台", engine.ToSimplified("臺"));
            Assert.NotEqual("学", engine.ToSimplified("學")); // the shipped entry went with the table

            string xueOnly = WriteTable("trans-xue.txt", "學:学\n");
            engine.LoadTraditionalDictionary(xueOnly);
            Assert.Equal("学", engine.ToSimplified("學"));
            Assert.NotEqual("台", engine.ToSimplified("臺"));
        }

        /// <summary>
        /// One rendered reading per span, space separated: the native <c>Result::join</c> the C++ cases in
        /// <c>tests/cpp/test_user_dict.cpp</c> compare against, so the expectations below read the same way
        /// in both suites.
        /// </summary>
        private static string Joined(IReadOnlyList<Segment> spans)
        {
            var joined = new StringBuilder();
            for (int i = 0; i < spans.Count; i++)
            {
                if (i > 0)
                {
                    joined.Append(' ');
                }

                joined.Append(spans[i].Rendered);
            }

            return joined.ToString();
        }

        /// <summary>Write one scratch table under the repository's ignored <c>.tmp</c> and return its path.</summary>
        private static string WriteTable(string name, string content)
        {
            string directory = Path.Combine(RepoLayout.Root, ".tmp", "csharp-dictionary-load-tests");
            Directory.CreateDirectory(directory);
            string path = Path.Combine(directory, name);
            File.WriteAllText(path, content, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            return path;
        }
    }
}
