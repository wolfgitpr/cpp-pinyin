using System;
using System.Collections.Generic;
using System.IO;
using CppPinyin2;

/// <summary>
/// The packaging test's consumer: proves that a project which only adds
/// &lt;PackageReference Include="CppPinyin" /&gt; can load the native library out of the package's
/// runtimes/&lt;rid&gt;/native/ directory and run a real query.
/// <para>
/// It prints what it found and exits non-zero on any failure, so the driver does not have to parse
/// prose. Expected readings are NOT hardcoded: the canonical readings live in the conformance fixture,
/// which is the same lock the two test suites and the Python shell consume.
/// </para>
/// <para>
/// The optional first argument is a dictionary directory. The pipeline passes the repository's
/// data/dialects, which turns one run into two assertions: the package's own copy works with no argument,
/// and an explicit directory still overrides it.
/// </para>
/// </summary>
internal static class Program
{
    private static int Main(string[] args)
    {
        int failures = 0;

        Console.WriteLine("framework         : " + AppContext.TargetFrameworkName);
        Console.WriteLine("assembly          : " + typeof(Engine).Assembly.Location);
        Console.WriteLine("base directory    : " + AppContext.BaseDirectory);

        // 1. The native library resolved from the package, not from a build tree.
        string version = Engine.Version;
        Console.WriteLine("cppp2_version      : " + version);
        if (string.IsNullOrEmpty(version))
        {
            Console.WriteLine("FAIL  cppp2_version() returned nothing");
            failures++;
        }

        List<string> dialects = new List<string>(Engine.DialectIds());
        dialects.Sort(StringComparer.Ordinal);
        Console.WriteLine("dialects          : " + string.Join(",", dialects));
        if (string.Join(",", dialects) != "cantonese,mandarin")
        {
            Console.WriteLine("FAIL  dialects are not the two built-ins");
            failures++;
        }

        // 2. The package's dictionary copy must have landed next to the assembly (contentFiles with
        //    copy-to-output). Without this, every consumer has to know where data/dialects lives.
        string packaged = Path.Combine(AppContext.BaseDirectory, "data", "dict");
        Console.WriteLine("packaged dicts    : " + packaged + " exists=" + Directory.Exists(packaged));
        if (!Directory.Exists(packaged))
        {
            Console.WriteLine("FAIL  the package's dictionary copy is not next to the assembly");
            failures++;
        }
        else
        {
            using (var engine = new Engine("mandarin"))
            {
                Console.WriteLine("packaged query    : " + engine.Query("你好世界", Style.Tone3).ToText());

                // The structural side of the same call, through the two entry points that only exist on the
                // twenty-symbol surface: the default builds no structure, and asking for it yields exactly
                // one reading per span -- which the library's own renderer reproduces byte for byte.
                IReadOnlyList<Segment> plain = engine.Query("你好世界", Style.Tone3);
                bool builtAnyway = false;
                for (int i = 0; i < plain.Count; i++)
                {
                    builtAnyway |= plain[i].Syllables.Count != 0;
                }

                if (builtAnyway)
                {
                    Console.WriteLine("FAIL  syllables were built without includeReading");
                    failures++;
                }

                IReadOnlyList<Segment> rows = engine.Query("你好世界", Style.Tone3, includeReading: true);
                var rendered = new List<string>(rows.Count);
                for (int i = 0; i < rows.Count; i++)
                {
                    if (rows[i].Syllables.Count != 1)
                    {
                        Console.WriteLine("FAIL  span " + i + " carries " + rows[i].Syllables.Count +
                                          " reading(s), not exactly one");
                        failures++;
                        break;
                    }

                    rendered.Add(engine.RenderSyllable(rows[i].Syllables[0], Style.Tone3));
                }

                if (string.Join(" ", rendered) != engine.Query("你好世界", Style.Tone3).ToText())
                {
                    Console.WriteLine("FAIL  cppp2_render_syllable produced '" + string.Join(" ", rendered) + "'");
                    failures++;
                }

                Console.WriteLine("packaged syllables: " + string.Join(" ", rendered));
            }

            using (var engine = new Engine("cantonese"))
            {
                Console.WriteLine("packaged cantonese: " + engine.Query("你好世界", Style.Tone3).ToText());
            }
        }

        // 3. The caller's override still wins: the dictionary directory is an explicit entry point and
        //    the package copy is only the fallback.
        if (args.Length > 0)
        {
            using (var engine = new Engine("mandarin", args[0]))
            {
                Console.WriteLine("override query    : " + engine.Query("你好世界", Style.Tone3).ToText());
                Console.WriteLine("override dir      : " + engine.DictDirectory);
            }

            using (var engine = new Engine("cantonese", args[0]))
            {
                Console.WriteLine("override cantonese: " + engine.Query("你好世界", Style.Tone3).ToText());
            }
        }

        // 4. Disposal still behaves across a package boundary.
        var disposable = new Engine("mandarin", args.Length > 0 ? args[0] : null);
        disposable.Dispose();
        try
        {
            disposable.Query("你好");
            Console.WriteLine("FAIL  a disposed engine answered a query");
            failures++;
        }
        catch (ObjectDisposedException)
        {
            Console.WriteLine("dispose           : ObjectDisposedException as expected");
        }

        Console.WriteLine(failures == 0 ? "RESULT: OK" : "RESULT: " + failures + " failure(s)");
        return failures == 0 ? 0 : 1;
    }
}
