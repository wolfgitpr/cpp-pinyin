using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace CppPinyin2
{
    /// <summary>
    /// A dictionary-backed converter for one dialect, over the C ABI (docs section 6.8.3).
    ///
    /// <code>
    /// using (var engine = new Engine("mandarin", "data/dialects"))
    /// {
    ///     foreach (var span in engine.Query("你好世界", Style.Tone3))
    ///         Console.WriteLine(span.Text + " " + span.Rendered);
    /// }
    /// </code>
    ///
    /// <para>
    /// Thread-safety is the library's: one engine may be queried from several threads once it exists,
    /// but <see cref="LoadUserDictionary"/> and <see cref="LoadTraditionalDictionary"/> mutate it and
    /// must not run concurrently with anything else.
    /// </para>
    /// </summary>
    public sealed class Engine : IDisposable
    {
        private const int MaxTextBytes = 16 * 1024 * 1024;

        private readonly EngineHandle _handle;
        private readonly string _dialectId;
        private readonly string _dictDirectory;
        private bool _disposed;

        /// <summary>
        /// Create an engine for <paramref name="dialectId"/>.
        /// </summary>
        /// <param name="dialectId">A dialect id from <see cref="DialectIds"/>, e.g. <c>"mandarin"</c>.</param>
        /// <param name="dictDirectory">
        /// Directory holding <c>&lt;dialectId&gt;/</c> dictionary files, or <see langword="null"/> to use
        /// the copy shipped next to this assembly (the package's build-time copy of <c>data/dialects</c>).
        /// There is deliberately no environment-variable lookup: a hidden search path is how the wrong
        /// dictionary gets loaded without anyone noticing.
        /// </param>
        public Engine(string dialectId, string? dictDirectory = null)
        {
            RequireText(dialectId, nameof(dialectId));
            _dictDirectory = dictDirectory ?? DefaultDictDirectory();
            string absolute = Path.GetFullPath(_dictDirectory);
            RequireText(absolute, nameof(dictDirectory));

            var error = new ErrorRaw();
            IntPtr raw;
            try
            {
                using (var dialect = new Utf8String(dialectId))
                using (var directory = new Utf8String(absolute))
                {
                    // The registry argument is NULL: this shell has no C++ host to hand it one, and NULL is
                    // the header's own way of asking for the library's two built-ins (mandarin, cantonese).
                    raw = NativeMethods.cppp2_open(IntPtr.Zero, dialect.Pointer, directory.Pointer, ref error);
                }
            }
            catch (DllNotFoundException ex)
            {
                throw new CppPinyinException(NativeLoader.DescribeMissingLibrary(), ex);
            }

            // The channel's ownership rule, applied once whatever the outcome was: `detail` belongs to the
            // caller and is NULL on success, so this is "nothing to free" on the happy path as well.
            string detail = Status.TakeDetail(ref error);

            if (raw == IntPtr.Zero)
            {
                string where = "cppp2_open for dialect '" + dialectId + "' with dictionaries in '" + absolute +
                               "'";
                if (error.code == Status.NotReady)
                {
                    // The two failures the old entry point collapsed into "NULL handle plus a message" are
                    // distinguishable now, and this is the one it collapsed hardest: an id the registry does
                    // not know. The library never resolves it to another dialect, and its own detail names
                    // the id, so the shell passes that on instead of inventing a category of its own.
                    throw new CppPinyinUnknownDialectException(Status.Describe(where, error.code, detail));
                }

                // Every other code -- including one this shell has never seen -- becomes its own exception
                // (Status.ThrowIfFailed ends in a fallback branch for exactly that reason).
                Status.ThrowIfFailed(error.code, where, detail);

                // Reached only if a NULL handle ever arrives with CPPP2_OK, which the header says cannot
                // happen. Adopting it anyway would hand out an "engine" whose every later call fails, so it
                // is refused here, where the reason is still known.
                throw new CppPinyinNotReadyException(
                    where + " returned no handle with " + Status.Name(error.code));
            }

            _handle = EngineHandle.Adopt(raw);
            _dialectId = dialectId;
            DialectId = dialectId;
        }

        /// <summary>The dialect this engine was created for.</summary>
        public string DialectId { get; private set; } = string.Empty;

        /// <summary>The dictionary directory in use, resolved to an absolute path.</summary>
        public string DictDirectory => _dictDirectory;

        /// <summary>The library's version, read from the native library itself.</summary>
        public static string Version => Utf8.FromPtr(NativeMethods.cppp2_version());

        /// <summary>
        /// The dictionary root the NATIVE library was installed to, or <c>null</c> when it has no such anchor.
        /// </summary>
        /// <remarks>
        /// D2 of docs/DECISIONS.md section 4.1 the native side reports the neighbour of
        /// its own module directory (<c>&lt;module&gt;/../share/cpp-pinyin2/dict</c>), which is what lets a
        /// relocated application work without hard-coding a path. It is deliberately NOT consulted by
        /// <see cref="DefaultDictDirectory"/>: a nupkg carries its own copy and that copy is the right one for a
        /// package consumer. Use this to diagnose a native library installed somewhere other than the package.
        /// <c>null</c> means "no anchor" -- a static build, where the library is code inside the host.
        /// </remarks>
        public static string? DictDirectoryCandidate()
        {
            IntPtr candidate = NativeMethods.cppp2_dict_dir_candidate();
            return candidate == IntPtr.Zero ? null : Utf8.FromPtr(candidate);
        }

        /// <summary>
        /// The dialects the library has registered, read from the library rather than hardcoded so a
        /// binding cannot drift from the engine's dialect set.
        /// </summary>
        public static IReadOnlyList<string> DialectIds()
        {
            IntPtr list;
            UIntPtr count;
            // The registry argument is NULL, exactly like the constructor's: the shell has no C++ host to
            // hand it one, so it gets the library's own built-ins (in ascending byte order).
            Status.ThrowIfFailed(
                NativeMethods.cppp2_dialect_ids(IntPtr.Zero, out list, out count),
                "cppp2_dialect_ids",
                string.Empty);
            try
            {
                int n = checked((int)count.ToUInt64());
                var ids = new List<string>(n);
                for (int i = 0; i < n; i++)
                {
                    IntPtr item = Marshal.ReadIntPtr(list, i * IntPtr.Size);
                    ids.Add(Utf8.FromPtr(item));
                }

                return ids;
            }
            finally
            {
                NativeMethods.cppp2_string_list_free(list, count);
            }
        }

        /// <summary>
        /// Convert <paramref name="text"/> into one <see cref="Segment"/> per input span.
        /// With <see cref="OnError.Drop"/> a span without a reading is dropped instead of marked, so
        /// the result is then not one item per span. An empty input is not an error.
        /// <para>
        /// <paramref name="includeReading"/> asks for the structural side (<see cref="Syllable"/> rows):
        /// every non-failed span then carries exactly one, and a FAILED span carries none -- a failure has
        /// no structure to describe, and a failed span deliberately does not enter the array. The default is
        /// off, and "off" is not "this span has no reading": <see cref="Segment.Rendered"/> carries the
        /// reading on every span either way.
        /// </para>
        /// <para>
        /// It is the LAST parameter for the same reason the C header appends new options at the end of
        /// <c>cppp2_query_opts</c>: an existing caller's positional arguments keep meaning what they meant.
        /// </para>
        /// <para>
        /// <paramref name="vToU"/> reads backwards from its name, and the name is deliberate: ü is
        /// written <c>v</c> BY DEFAULT, and setting this to <see langword="true"/> is what keeps ü.
        /// That is the opposite direction from pypinyin's <c>v_to_u</c>, so the parameter is not named
        /// after it -- the plugin guide records the three-place naming rule, and <c>include/cpp-pinyin2/core/Types.h</c>
        /// states the behaviour.
        /// </para>
        /// </summary>
        public IReadOnlyList<Segment> Query(
            string text,
            Style style = Style.Default,
            bool includeCandidates = false,
            bool vToU = false,
            bool markNeutralAs5 = false,
            OnError onError = OnError.Default,
            bool includeReading = false)
        {
            return RunQuery(text, style, includeCandidates, vToU, markNeutralAs5, onError, includeReading, null);
        }

        /// <summary>Shared by <see cref="Query"/> and <see cref="ToneSandhi"/>: one conversion, and
        /// optionally the Mandarin tone sandhi that has to run on the result while it is alive.</summary>
        private IReadOnlyList<Segment> RunQuery(
            string text,
            Style style = Style.Default,
            bool includeCandidates = false,
            bool vToU = false,
            bool markNeutralAs5 = false,
            OnError onError = OnError.Default,
            bool includeReading = false,
            (bool ThirdTone, bool Bu, bool Yi)? sandhi = null)
        {
            RequireText(text, nameof(text));
            EngineHandle handle = RequireHandle();

            QueryOptions options = NewOptions(handle, _dialectId, style, onError, includeCandidates, vToU, markNeutralAs5, includeReading);
            IntPtr raw;
            using (var native = new Utf8String(text))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_query(handle, native.Pointer, ref options, out raw), "cppp2_query", text);

            if (sandhi is not null)
            {
                // struct_size first, before the call: the library reads it to learn what this caller owns.
                var sandhiOptions = new NativeMethods.ToneSandhiOptsRaw
                {
                    struct_size = (UIntPtr)Marshal.SizeOf<NativeMethods.ToneSandhiOptsRaw>(),
                    third_tone = sandhi.Value.ThirdTone ? 1 : 0,
                    bu = sandhi.Value.Bu ? 1 : 0,
                    yi = sandhi.Value.Yi ? 1 : 0,
                };
                // On the LIVE result, before the copy-out below and before the finally frees it.
                int sandhiStatus = NativeMethods.cppp2_apply_tone_sandhi(handle, raw, ref sandhiOptions, out _);
                if (sandhiStatus != 0)
                {
                    // Free BEFORE throwing. `raw` is a live result the caller owns, and the try/finally
                    // below only covers the copy-out path -- so a refused pass used to leak the whole
                    // result. `cppp2_apply_tone_sandhi` refuses outright on a dialect with no sandhi
                    // pass (Cantonese, CPPP2_ERR_INVALID_ARG), which makes this the COMMON path, not a
                    // corner (F21; the Python shell had the same shape and was fixed in 44aef8f).
                    NativeMethods.cppp2_result_free(raw);
                    Status.ThrowIfFailed(sandhiStatus, "cppp2_apply_tone_sandhi", string.Empty);
                }
            }
            }

            try
            {
                var result = Marshal.PtrToStructure<ResultRaw>(raw);
                int count = checked((int)result.count.ToUInt64());
                var spans = new List<Segment>(count);
                for (int i = 0; i < count; i++)
                {
                    IntPtr at = new IntPtr(result.segments.ToInt64() + (i * Marshal.SizeOf<SegmentRaw>()));
                    var span = Marshal.PtrToStructure<SegmentRaw>(at);

                    // Both arrays are NULL with count 0 when they were not requested, so the COUNT is what
                    // decides dereferencing -- never "is the structure filled", which cannot tell a failed
                    // span from one that was simply not asked about.
                    int candidateCount = checked((int)span.candidate_count.ToUInt64());
                    var candidates = new List<string>(candidateCount);
                    for (int c = 0; c < candidateCount; c++)
                    {
                        IntPtr item = Marshal.ReadIntPtr(span.candidates, c * IntPtr.Size);
                        candidates.Add(Utf8.FromPtr(item));
                    }

                    int syllableCount = checked((int)span.syllable_count.ToUInt64());
                    var syllables = new List<Syllable>(syllableCount);
                    for (int s = 0; s < syllableCount; s++)
                    {
                        IntPtr row = new IntPtr(span.syllables.ToInt64() + (s * Marshal.SizeOf<SyllableRaw>()));
                        var rawRow = Marshal.PtrToStructure<SyllableRaw>(row);
                        syllables.Add(new Syllable(
                            Utf8.FromPtr(rawRow.initial),
                            Utf8.FromPtr(rawRow.fin),
                            rawRow.tone,
                            rawRow.tone_kind,
                            rawRow.source,
                            spelling: Utf8.FromPtr(rawRow.spelling)));
                    }

                    // Copied now, freed below: nothing reachable from the result outlives cppp2_result_free.
                    spans.Add(new Segment(
                        Utf8.FromPtr(span.text),
                        Utf8.FromPtr(span.rendered),
                        span.failure_kind,
                        candidates,
                        syllables));
                }

                return spans;
            }
            finally
            {
                NativeMethods.cppp2_result_free(raw);
            }
        }

        /// <summary>
        /// Convert <paramref name="text"/> and then apply Mandarin tone sandhi to the result.
        /// <para>
        /// The rules are not implemented here: this is <see cref="Query"/> followed by the C entry point
        /// <c>cppp2_apply_tone_sandhi</c>, the C face of <c>include/cpp-pinyin2/core/ToneSandhi.h</c> -- the one place the three
        /// rules (third-tone runs, bu, yi) and their two deliberate differences from pypinyin's
        /// <c>contrib.tone_sandhi</c> are written down. The three switches default to ON, the library's own
        /// default, and are always sent explicitly: NULL would test the C default instead of the value the
        /// caller asked for.
        /// </para><para>
        /// SCOPED BY THE DIALECT. The pass is whatever this engine's dialect declares (`Dialect::toneSandhi`);
        /// Mandarin is the only one today, and Cantonese declares none -- jyutping readings are letters plus a
        /// tone digit too, so no shape test could tell them apart, and calling this on a Cantonese engine throws
        /// <see cref="CppPinyinInvalidArgumentException"/> (<c>CPPP2_ERR_INVALID_ARG</c>) instead of rewriting the rows.
        /// </para><para>
        /// What changes is <see cref="Segment.Rendered"/>, and only where the reading is spelled as letters
        /// plus a tone digit; the span count never changes. Everything else is <see cref="Query"/>.
        /// </para></summary>
        public IReadOnlyList<Segment> ToneSandhi(
            string text,
            Style style = Style.Default,
            bool includeCandidates = false,
            bool includeReading = false,
            bool vToU = false,
            bool markNeutralAs5 = false,
            OnError onError = OnError.Default,
            bool thirdTone = true,
            bool bu = true,
            bool yi = true)
        {
            return RunQuery(text, style, includeCandidates, vToU, markNeutralAs5, onError, includeReading,
                (thirdTone, bu, yi));
        }
        /// <summary>
        /// Render ONE structured reading in <paramref name="style"/> -- <c>cppp2_render_syllable</c>.
        /// <para>
        /// This is why the shell implements no style itself:
        /// <c>engine.RenderSyllable(segment.Syllables[0])</c> is the string this engine's own dialect
        /// prints for that reading, including the dialect's tone notation. A reading from another dialect's
        /// engine would render the wrong bytes, which is why the dialect comes from this handle rather than
        /// from the reading.
        /// </para>
        /// <para>
        /// A style the dialect does not support is the library's normal contract: the reading comes back
        /// unchanged, not as an error. An empty string is a legitimate answer too (<see cref="Style.Initials"/>
        /// of a zero-initial syllable is <c>""</c>), and is returned as such. A
        /// <see cref="CppPinyinInvalidArgumentException"/> means the structure does not denote one syllable
        /// (a separator inside <see cref="Syllable.Initial"/> or <see cref="Syllable.Fin"/>) or carries
        /// neither a <see cref="Syllable.Spelling"/> nor a projection that can rebuild one -- a reading that
        /// came out of <see cref="Query"/> always has the spelling.
        /// </para>
        /// </summary>
        public string RenderSyllable(
            Syllable reading,
            Style style = Style.Default,
            bool vToU = false,
            bool markNeutralAs5 = false)
        {
            if (reading == null)
            {
                throw new ArgumentNullException(nameof(reading));
            }

            EngineHandle handle = RequireHandle();
            // A NUL inside any of these fields would be truncated silently by the C ABI, so the same check
            // the string arguments get applies to the fields that become C strings here.
            RequireText(reading.Initial, "reading.Initial");
            RequireText(reading.Fin, "reading.Fin");
            RequireText(reading.Spelling, "reading.Spelling");

            QueryOptions options = NewOptions(handle, _dialectId, style, OnError.Default, false, false, false, false);
            var raw = new SyllableRaw();
            IntPtr rendered;
            using (var initial = new Utf8String(reading.Initial))
            using (var fin = new Utf8String(reading.Fin))
            using (var spelling = new Utf8String(reading.Spelling))
            {
                raw.initial = initial.Pointer;
                raw.fin = fin.Pointer;
                raw.spelling = spelling.Pointer;
                raw.tone = reading.Tone;
                raw.tone_kind = reading.ToneKind;
                raw.source = reading.Source;
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_render_syllable(handle, ref raw, ref options, out rendered),
                    "cppp2_render_syllable",
                    string.Empty);
            }

            string value = Utf8.FromPtr(rendered);
            if (rendered != IntPtr.Zero)
            {
                NativeMethods.cppp2_string_free(rendered);
            }

            return value;
        }

        /// <summary>
        /// The library's own wording for a status code -- <c>cppp2_code_text</c> -- including a
        /// <see cref="Segment.FailureKind"/> or the code carried by one of this shell's exceptions.
        /// <para>
        /// The wording exists exactly once, in the library, so a caller that wants to say what went wrong
        /// asks here instead of writing a sentence of its own. A value outside the status domain is
        /// answered ("unknown code"), not refused: the header's own rule.
        /// </para>
        /// </summary>
        public static string CodeText(int code)
        {
            return Utf8.FromPtr(NativeMethods.cppp2_code_text(code));
        }

        /// <summary>
        /// Every reading of a single hanzi, in the library's order. A non-hanzi yields itself.
        /// <para>
        /// The order is the library's (<c>include/cpp-pinyin2/core/Types.h</c> → <c>Segment::candidates</c>): the dialect's
        /// own dictionary order, de-duplicated by the CONVERTED string keeping the first occurrence, so
        /// <paramref name="style"/> and the two switches decide the count as well as the spellings.
        /// </para>
        /// <para>
        /// <paramref name="vToU"/> and <paramref name="markNeutralAs5"/> mean what they mean on
        /// <see cref="Query(string, Style, bool, bool, bool, OnError, bool)"/> -- both are masked by the
        /// dialect's capability flags, so Cantonese ignores them. They are accepted here because the C
        /// entry point (<c>cppp2_readings</c>) honours them: a shell that did not pass them answered a
        /// different question than the C ABI did for the same arguments.
        /// </para>
        /// </summary>
        public IReadOnlyList<string> Readings(
            string hanzi,
            Style style = Style.Default,
            bool vToU = false,
            bool markNeutralAs5 = false)
        {
            RequireText(hanzi, nameof(hanzi));
            EngineHandle handle = RequireHandle();
            QueryOptions options = NewOptions(handle, _dialectId, style, OnError.Default, false, vToU, markNeutralAs5, false);

            IntPtr list;
            UIntPtr count;
            using (var native = new Utf8String(hanzi))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_readings(handle, native.Pointer, ref options, out list, out count),
                    "cppp2_readings",
                    hanzi);
            }

            try
            {
                int n = checked((int)count.ToUInt64());
                var readings = new List<string>(n);
                for (int i = 0; i < n; i++)
                {
                    readings.Add(Utf8.FromPtr(Marshal.ReadIntPtr(list, i * IntPtr.Size)));
                }

                return readings;
            }
            finally
            {
                NativeMethods.cppp2_string_list_free(list, count);
            }
        }

        /// <summary>
        /// <paramref name="reading"/> in <paramref name="style"/> -- the C face of the dialect's own converter.
        /// Any notation the dialect reads goes in (tone digits, tone marks, toneless); a style the dialect does not
        /// support comes back unchanged, which is the library's documented contract, not an error.
        /// </summary>
        /// <remarks>
        /// No v-to-u and no neutral-5: this entry converts styles, and switching the spelling behind the caller's
        /// back would make a style change look like a different reading. Use <see cref="Query"/> when those switches
        /// are wanted. The dialect comes from this engine, so a Mandarin reading rendered by a Cantonese engine
        /// would be wrong bytes -- which is why this is a method here and not a static.
        /// </remarks>
        /// <param name="reading">One reading. A separator means several, and is refused.</param>
        /// <param name="style">Target style (see the <see cref="Style"/> enum).</param>
        /// <exception cref="CppPinyinException">The library refused the reading or the engine is not usable.</exception>
        public string ToneConvert(string reading, Style style = Style.Default)
        {
            RequireText(reading, nameof(reading));
            EngineHandle handle = RequireHandle();

            IntPtr converted;
            using (var native = new Utf8String(reading))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_tone_convert(handle, native.Pointer, (int)style, out converted),
                    "cppp2_tone_convert", reading);
            }

            string value = Utf8.FromPtr(converted);
            if (converted != IntPtr.Zero)
            {
                NativeMethods.cppp2_string_free(converted);
            }

            return value;
        }

        /// <summary>
        /// The simplified form of the <em>first</em> character of <paramref name="text"/>; the rest is the
        /// library's business (it returns the first character's mapping, not a whole-string conversion).
        /// </summary>
        public string ToSimplified(string text)
        {
            RequireText(text, nameof(text));
            EngineHandle handle = RequireHandle();

            IntPtr simplified;
            using (var native = new Utf8String(text))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_to_simplified(handle, native.Pointer, out simplified), "cppp2_to_simplified", text);
            }

            string value = Utf8.FromPtr(simplified);
            if (simplified != IntPtr.Zero)
            {
                NativeMethods.cppp2_string_free(simplified);
            }

            return value;
        }

        /// <summary>
        /// Whether the FIRST character of <paramref name="hanzi"/> starts a phrase in this dialect's corpus --
        /// the predicate the C ABI documents as <c>cppp2_is_polyphonic</c>. It is NOT "the character has more
        /// than one reading of its own": a single-reading character that opens a phrase answers <c>true</c>.
        /// </summary>
        public bool IsPolyphonic(string hanzi)
        {
            RequireText(hanzi, nameof(hanzi));
            int polyphonic;
            using (var native = new Utf8String(hanzi))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_is_polyphonic(RequireHandle(), native.Pointer, out polyphonic),
                    "cppp2_is_polyphonic",
                    hanzi);
            }

            return polyphonic == 1;
        }

        /// <summary>
        /// Load a user dictionary. Not thread-safe: do not call this while other threads query.
        /// <para>
        /// A file that parses to ZERO ENTRIES IS NOT REFUSED HERE -- this used to claim it was a
        /// <c>CPPP2_ERR_LOAD</c> failure, which the C ABI never delivers: the loader returns success once
        /// the file opens (<c>src/dict/DictUtil.cpp</c>, the end of <c>loadAdditionalDict</c>), because the
        /// shipped Cantonese <c>user_dict.txt</c> is a 0-byte file and an empty table has to stay legal.
        /// The Python shell pre-checks the case and raises (<c>python/cpp_pinyin2/__init__.py</c>,
        /// <c>_check_dict_file</c>, measured 2026-10-27); this shell does not, so a mistyped table loads
        /// "successfully" and changes nothing. Stated here rather than left as a promise the code does not
        /// keep.
        /// </para>
        /// </summary>
        /// <param name="path">The user dictionary to load.</param>
        /// <param name="clearExisting">
        /// Whether to drop the user entries loaded so far first. Defaults to <c>true</c>, so loading a
        /// dictionary replaces the previous user layer, which is the default of the C++
        /// (<c>Engine::loadUserDict</c>), C (<c>cppp2_load_user_dict</c>) and Python
        /// (<c>Engine.load_user_dict</c>) shells as well. Pass <c>false</c> to merge the file on top of
        /// what is already loaded instead.
        /// </param>
        public void LoadUserDictionary(string path, bool clearExisting = true)
        {
            RequireText(path, nameof(path));
            using (var native = new Utf8String(path))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_load_user_dict(RequireHandle(), native.Pointer, clearExisting ? 1 : 0),
                    "cppp2_load_user_dict",
                    path);
            }
        }

        /// <summary>
        /// Load a traditional-character table. Not thread-safe, same as <see cref="LoadUserDictionary"/>.
        /// </summary>
        /// <param name="path">The traditional-character table to load.</param>
        /// <param name="clearExisting">
        /// Whether to drop the table loaded so far first. Defaults to <c>true</c> so that this call has
        /// the same meaning as in the C++ (<c>Engine::loadTraditionalDict</c>), C
        /// (<c>cppp2_load_traditional_dict</c>) and Python (<c>Engine.load_traditional_dict</c>) shells;
        /// pass <c>false</c> to merge instead.
        /// </param>
        public void LoadTraditionalDictionary(string path, bool clearExisting = true)
        {
            RequireText(path, nameof(path));
            using (var native = new Utf8String(path))
            {
                Status.ThrowIfFailed(
                    NativeMethods.cppp2_load_traditional_dict(RequireHandle(), native.Pointer, clearExisting ? 1 : 0),
                    "cppp2_load_traditional_dict",
                    path);
            }
        }

        /// <inheritdoc />
        public void Dispose()
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            _handle.Dispose();
        }

        /// <summary>
        /// The dialect's own style table, as a set of aliases. Read from the engine rather than
        /// duplicated here: the shell must not keep a second, disagreeing copy of the numbering.
        ///
        /// A FAILURE IS THROWN, not answered with an empty set (2026-10-20 audit, G01). Empty and
        /// "the library could not tell me" are different facts, and folding the second into the
        /// first turned an OOM/INTERNAL into "your style number is invalid" three call frames later
        /// -- the one diagnosis that sends a caller looking in the wrong place. The Python shell
        /// already propagates the status here (`raise_for_status`), so this is also the two shells
        /// agreeing again. The handle is passed as a `SafeHandle` (not `DangerousGetHandle()`), so
        /// a concurrent `Dispose()` cannot free it under the native call.
        /// </summary>
        private static System.Collections.Generic.HashSet<int> DeclaredAliases(EngineHandle handle)
        {
            int status = NativeMethods.cppp2_style_list(handle, out IntPtr list, out UIntPtr count);
            if (status != 0)
            {
                Status.ThrowIfFailed(status, "cppp2_style_list", string.Empty);
            }
            try
            {
                var aliases = new System.Collections.Generic.HashSet<int>();
                int n = (int)count;
                int size = Marshal.SizeOf<NativeMethods.StyleRaw>();
                for (int i = 0; i < n; i++)
                {
                    aliases.Add(Marshal.PtrToStructure<NativeMethods.StyleRaw>(list + i * size).alias);
                }
                return aliases;
            }
            finally
            {
                NativeMethods.cppp2_style_list_free(list, count);
            }
        }

        private static QueryOptions NewOptions(
            EngineHandle handle,
            string dialectId,
            Style style,
            OnError onError,
            bool candidates,
            bool vToU,
            bool neutralAs5,
            bool includeReading)
        {
            var options = new QueryOptions();
            // Declared size FIRST: cppp2_query_opts_init only writes the fields inside it (decision D19),
            // so calling init on a zero struct_size would leave every field at the CLR's zeroes and stop
            // honouring the library's defaults without saying so. The size is the struct's own, which is
            // the header's sizeof (40) only because QueryOptions declares reserved_0 as well.
            options.struct_size = new UIntPtr((ulong)Marshal.SizeOf<QueryOptions>());
            NativeMethods.cppp2_query_opts_init(ref options);
            // N20=A: the library's pass-through is deliberate (a style it cannot apply comes back
            // unchanged), but a shell has names -- and the engine hands it the dialect's OWN style
            // table -- so it refuses by name instead of rendering something else.
            if ((int)style != -1)
            {
                // ONE read, reused by the check and the message: the message used to re-read the
                // table, so a rejection cost two native calls plus two allocations.
                System.Collections.Generic.HashSet<int> declaredAliases = DeclaredAliases(handle);
                if (!declaredAliases.Contains((int)style))
                {
                    // Sorted, because `HashSet<int>` enumeration order is unspecified: the same
                    // rejected call used to produce different message text on different runs, and
                    // an assertion on that text is a flaky assertion (2026-10-20 audit, G23).
                    var ordered = new System.Collections.Generic.List<int>(declaredAliases);
                    ordered.Sort();
                    var declared = string.Join(", ", ordered);
                    throw new CppPinyinInvalidArgumentException(
                        $"{(int)style} is not a style of dialect {dialectId}; it declares {declared} -- or pass "
                        + "Style.Default for the dialect's own default");
                }
            }
            options.style = (int)style;
            options.on_error = (int)onError;
            options.include_candidates = candidates ? 1 : 0;
            options.v_to_u = vToU ? 1 : 0;
            options.mark_neutral_as_5 = neutralAs5 ? 1 : 0;
            options.include_reading = includeReading ? 1 : 0;
            return options;
        }

        /// <summary>The environment variable a deployment sets to point at its own dictionary root.</summary>
        /// <remarks>
        /// Read here, in the shell, and never inside the library: the C++/C surface deliberately has no
        /// implicit path (include/cpp-pinyin2/ext/c_api.h), and a shell is the layer that decides what to
        /// fall back on. The value is returned unchecked -- a wrong override must surface the library's own
        /// "which directory, which file" diagnosis instead of silently falling back to the packaged copy.
        /// </remarks>
        internal const string DictDirectoryVariable = "CPP_PINYIN2_DICT_DIR";

        private static string DefaultDictDirectory()
        {
            string? overrideDirectory = Environment.GetEnvironmentVariable(DictDirectoryVariable);
            if (!string.IsNullOrEmpty(overrideDirectory))
            {
                return overrideDirectory;
            }

            string packaged = Path.Combine(AppContext.BaseDirectory, "data", "dict");
            if (!Directory.Exists(packaged))
            {
                throw new CppPinyinInvalidArgumentException(
                    "no dictDirectory was given, " + DictDirectoryVariable + " is not set, and the package's " +
                    "copy was not found at '" + packaged + "'. Pass the directory that holds <dialect>/ " +
                    "(PLUGIN doc section 6.8.3: data/dialects is the single source of truth; a package copy is " +
                    "only a fallback).");
            }

            return packaged;
        }

        private static string RequireText(string value, string what)
        {
            if (value == null)
            {
                throw new ArgumentNullException(what);
            }

            // An empty string is NOT rejected here, and the conformance fixture is the reason: it pins
            // m-normal-empty / c-normal-empty, whose expectation is an empty span list rather than an
            // error. The C ABI treats empty input as valid, so a shell that returned an argument error
            // would be inventing a contract the library does not have -- and the first version of this
            // method did exactly that, which is how the fixture caught it.
            if (value.IndexOf('\0') >= 0)
            {
                // The C ABI takes NUL-terminated UTF-8, so an embedded NUL would truncate silently.
                throw new ArgumentException(what + " contains U+0000", what);
            }

            if (System.Text.Encoding.UTF8.GetByteCount(value) > MaxTextBytes)
            {
                throw new ArgumentException(what + " is over MaxTextBytes (" + MaxTextBytes + ")", what);
            }

            return value;
        }

        private EngineHandle RequireHandle()
        {
            if (_disposed)
            {
                throw new ObjectDisposedException(nameof(Engine));
            }

            return _handle;
        }
    }
}
