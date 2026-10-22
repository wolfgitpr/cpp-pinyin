using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace CppPinyin2
{
    /// <summary>
    /// The C ABI's structures, transcribed from <c>include/cpp-pinyin2/ext/c_api.h</c> (field order, widths
    /// and the padding the C compiler inserts). Nothing here is engine behaviour; the shapes are the
    /// whole point.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct QueryOptions
    {
        /// <summary><c>size_t struct_size</c> -- must be written BEFORE cppp2_query_opts_init (D19).</summary>
        internal UIntPtr struct_size;

        internal int style;
        internal int on_error;
        internal int include_candidates;
        internal int v_to_u;
        internal int mark_neutral_as_5;

        /// <summary>
        /// <c>int32_t reserved_0</c> -- NOT an option, and declared here anyway: it is the four bytes the
        /// FIRST declaration of this struct ended in (its <c>sizeof</c> was 32, so offsets 28..31 were
        /// trailing padding). The header claims that padding (S9c) and appends <c>include_reading</c>
        /// after it, because a caller built against the older declaration still declares
        /// <c>struct_size = 32</c> -- which covers the padding -- and an option placed there would be read
        /// out of the caller's uninitialised stack. Declaring it is what puts <see cref="include_reading"/>
        /// at offset 32 where the library reads it: a transcription that skipped this field would place it
        /// at 28, keep working byte for byte everywhere else, and silently stop asking for the structure.
        /// </summary>
        internal int reserved_0;

        /// <summary>
        /// <c>int32_t include_reading</c> -- 0/1, the structured reading (S8). Appended AFTER
        /// <c>reserved_0</c> rather than after <c>mark_neutral_as_5</c>; see that field. The default (0,
        /// and what a shorter older struct gets) means "do not build the structure", which is NOT "this
        /// span has no reading".
        /// </summary>
        internal int include_reading;

        /// <summary>
        /// <c>int32_t reserved_1</c> -- the same claim as <c>reserved_0</c>, one declaration later: the
        /// fields end at 36 while <c>sizeof</c> is 40, so 36..39 is trailing padding that a caller's
        /// <c>struct_size = 40</c> already covers. Claiming it keeps the NEXT option out of bytes older
        /// callers have already declared (S9c). Never an option; never read or written.
        /// </summary>
        internal int reserved_1;
    }

    /// <summary>
    /// <c>cppp2_syllable</c>: a STRUCTURED reading -- data, never a rendering. No <c>struct_size</c>: the
    /// 2.0 face promises no cross-version compatibility, so the layout just follows the header (rule 6
    /// is deleted). <c>spelling</c> is the table's own bytes -- the row's identity -- and
    /// <c>initial</c>/<c>fin</c> are its projection.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct SyllableRaw
    {
        internal IntPtr initial;
        internal IntPtr fin;
        internal IntPtr spelling;
        internal int tone;
        internal int tone_kind;
        internal int source;
        internal int reserved_0;   // claims the trailing hole the C compiler used to insert (S9c)
    }

    /// <summary>
    /// <c>cppp2_segment</c>: one input span and what the engine read for it.
    /// <para>
    /// The field order is the header's and the padding is the C compiler's: <c>failure_kind</c> is an
    /// <c>int32_t</c> followed by a pointer, so four bytes of padding sit between them on a 64-bit host;
    /// <see cref="LayoutKind.Sequential"/> lays the struct out with the same rules, which is why the order
    /// must stay identical to c_api.h rather than merely "the same fields".
    /// </para>
    /// <para>
    /// <c>rendered</c> is the ONE rendered string of this span (it equals <c>text</c> when the span
    /// failed) and there is deliberately no rendered field on <c>cppp2_syllable</c>: the structure is data,
    /// and a second copy of the same string in the same row would be a second truth.
    /// </para>
    /// <para>
    /// <c>syllables</c>/<c>syllable_count</c> are EMPTY (NULL / 0) both when the structure was not
    /// requested (<c>include_reading == 0</c>) and when the span failed; <c>candidates</c> is NULL with
    /// count 0 unless candidates were requested. Check each count before dereferencing its pointer --
    /// never "is the structure filled" to decide whether the span failed.
    /// </para>
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct SegmentRaw
    {
        internal IntPtr text;
        internal IntPtr rendered;
        internal int failure_kind;
        internal int reserved_0;  // claims the hole before the pointer, so no padding is implicit (S9c)
        internal IntPtr syllables;        // cppp2_syllable*, `syllable_count` entries; NULL unless requested
        internal UIntPtr syllable_count;
        internal UIntPtr candidate_count;
        internal IntPtr candidates;       // const char* const*, `candidate_count` strings
    }

    /// <summary><c>cppp2_result</c>. Everything reachable from it dies with cppp2_result_free.</summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct ResultRaw
    {
        internal UIntPtr count;
        internal IntPtr segments; // cppp2_segment*
    }

    /// <summary>
    /// <c>cppp2_error</c>: the entry points' optional error channel -- which kind of failure it was, plus
    /// this occurrence's message.
    /// <para>
    /// <see cref="detail"/> is an <see cref="IntPtr"/> rather than a marshalled string on purpose: the
    /// address of the library's allocation is the one thing <c>cppp2_string_free</c> needs, and a
    /// marshaller that turned the field into a managed string would take that address away. OWNERSHIP:
    /// <c>detail</c> belongs to the CALLER, is NULL on success, and is released by
    /// <see cref="Status.TakeDetail"/>.
    /// </para>
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct ErrorRaw
    {
        internal int code;
        internal int reserved_0;  // claims the hole before `detail` (S9c)
        internal IntPtr detail;
    }

    /// <summary>
    /// The C ABI's entry points (P/Invoke). All of them: a binding that skips one is a binding whose gap
    /// nobody notices until a caller needs it. The frozen list and its count live in
    /// tests/abi/expected_c_api_symbols.txt; no number is repeated here, so the two cannot drift apart.
    /// <para>
    /// <c>cppp_create</c> is deliberately absent. The entry point that took a dialect id with no registry
    /// and reported failure as "NULL handle plus a message" was replaced by <see cref="cppp2_open"/>, whose
    /// status code distinguishes an unknown dialect (<c>CPPP2_ERR_NOT_READY</c>) from a dictionary that
    /// would not load (<c>CPPP2_ERR_LOAD</c>) -- the two failures the old shape made indistinguishable.
    /// </para>
    /// <para>
    /// Every string parameter is an <see cref="IntPtr"/> holding UTF-8 allocated by the caller with
    /// <see cref="Utf8String"/>, not a <c>[MarshalAs(UnmanagedType.LPUTF8Str)] string</c>. Reason
    /// (measured, not stylistic): <c>LPUTF8Str</c> is absent from netstandard2.0 and the compiler refuses
    /// it for that target (CS0117); the nearest marshaller alternative, <c>LPStr</c>, means the platform's
    /// ANSI code page, which mangles Chinese on a non-UTF-8 host without failing.
    /// </para>
    /// </summary>
    internal static class NativeMethods
    {
        /// <summary>
        /// The native library's file name, recorded when the package was built (decision D20=A).
        /// It is a compile-time constant on purpose: <c>DllImport</c> needs one, and the file name
        /// differs by toolchain (MinGW produces <c>libcpp-pinyin2</c>, MSVC <c>cpp-pinyin2</c>).
        /// </summary>
        internal const string Library = BuildInfo.NativeLibraryName;

        internal const CallingConvention Convention = CallingConvention.Cdecl;

        /// <summary>
        /// Install the resolver before the first P/Invoke is JIT-resolved. Touch this type (rather than
        /// relying on Engine's own static constructor) so no call path can reach a DllImport first.
        /// </summary>
        static NativeMethods()
        {
            NativeLoader.Register();
        }

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern IntPtr cppp2_version();

        /// <summary>
        /// The dictionary root this build installed to, resolved against the library's own module directory,
        /// or <see cref="IntPtr.Zero"/> when the build has no such anchor (a static build).
        /// </summary>
        /// <remarks>
        /// Static string, never freed. This reports the NATIVE build's install location, which is not the
        /// managed default: a nupkg consumer resolves the packaged copy first (Engine.DefaultDictDirectory).
        /// </remarks>
        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern IntPtr cppp2_dict_dir_candidate();

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_string_free(IntPtr value);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_string_list_free(IntPtr list, UIntPtr count);
        /// <summary>One row of a dialect's own style table: <c>cppp2_style</c>. The field names are the C
        /// ones on purpose -- tools/check_bindings.py compares them field-for-field, in order.</summary>
        internal struct StyleRaw
        {
            internal IntPtr name;
            internal int alias;
            internal int is_default;
        }

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_style_list(EngineHandle engine, out IntPtr list, out UIntPtr count);
        /// <summary><c>cppp2_tone_sandhi_opts</c>: caller-owned, three switches, and all three default
        /// to ON in the library. Field names and order are the C ones on purpose --
        /// <c>tools/check_bindings.py</c> compares them field-for-field, in order.
        /// <para>MANDARIN only: see <see cref="Engine.ToneSandhi"/>.</para></summary>
        [StructLayout(LayoutKind.Sequential)]
        internal struct ToneSandhiOptsRaw
        {
            internal UIntPtr struct_size;
            internal int third_tone;
            internal int bu;
            internal int yi;
            // Claims this declaration's trailing padding, exactly as c_api.h:357-359 says: without it the
            // three switches end at 20 while sizeof is 24, and the four bytes of padding would be free for
            // a future field to be appended into -- the drift the `reserved_0` rule exists to prevent.
            internal int reserved_0;
        }

        /// <summary>Applies the ENGINE'S DIALECT'S tone-sandhi pass to a LIVE result, in place; the rules live
        /// in the library (<c>core/ToneSandhi.h</c>), never here.
        /// <para>The engine handle comes first, and it is not decoration: tone sandhi is a dialect property, so
        /// the library asks the dialect whether it declares a pass. A dialect without one (Cantonese) answers
        /// <c>CPPP2_ERR_INVALID_ARG</c> and leaves the rows alone -- which is the whole reason this takes a
        /// handle: jyutping is letters-plus-a-digit too, so no shape test could tell it apart from Mandarin.</para>
        /// <para>The return value is a STATUS, like every other entry point here; the number of rows
        /// rewritten comes back through <c>changed</c> (discard it with <c>out _</c>).</para></summary>
        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_apply_tone_sandhi(EngineHandle engine, IntPtr result, ref ToneSandhiOptsRaw opts,
            out UIntPtr changed);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_style_list_free(IntPtr list, UIntPtr count);


        /// <summary>The library's own wording for a status code; a static string the caller never frees.</summary>
        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern IntPtr cppp2_code_text(int code);

        /// <summary>
        /// <c>cppp2_open(const cppp2_registry *registry, const char *dialect_id, const char *dict_dir,
        /// cppp2_error *err)</c>. <paramref name="registry"/> is NULL for "the library's own two
        /// built-ins" -- this shell has no C++ host to hand it a registry.
        /// </summary>
        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern IntPtr cppp2_open(
            IntPtr registry,
            IntPtr dialectId,
            IntPtr dictDirectory,
            ref ErrorRaw error);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_destroy(IntPtr engine);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_dialect_ids(IntPtr registry, out IntPtr list, out UIntPtr count);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_query_opts_init(ref QueryOptions options);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_query(
            EngineHandle engine,
            IntPtr text,
            ref QueryOptions options,
            out IntPtr result);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern void cppp2_result_free(IntPtr result);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_render_syllable(
            EngineHandle engine,
            ref SyllableRaw syllable,
            ref QueryOptions options,
            out IntPtr text);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_readings(
            EngineHandle engine,
            IntPtr hanzi,
            ref QueryOptions options,
            out IntPtr list,
            out UIntPtr count);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_to_simplified(
            EngineHandle engine,
            IntPtr hanzi,
            out IntPtr simplified);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_tone_convert(
            EngineHandle engine,
            IntPtr reading,
            int style,
            out IntPtr converted);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_is_polyphonic(
            EngineHandle engine,
            IntPtr hanzi,
            out int polyphonic);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_load_user_dict(
            EngineHandle engine,
            IntPtr path,
            int clearExisting);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_load_traditional_dict(
            EngineHandle engine,
            IntPtr path,
            int clearExisting);

        // The `_ex` spellings (2026-10-20, audit H04): the same two loads WITH the error channel, so a managed
        // host can read WHY a table was refused instead of only that it was (`CPPP2_ERR_LOAD` used to be the
        // whole answer). Declared here because `BindingGates` fails when the frozen C list names a symbol no
        // binding declares -- that gap is exactly what this pair closed. The managed shell keeps calling the
        // short names for now: its public methods report through exceptions built from the code, and switching
        // them is a separate decision about the C# surface, not a prerequisite for using the C one.
        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_load_user_dict_ex(
            EngineHandle engine,
            IntPtr path,
            int clearExisting,
            ref ErrorRaw error);

        [DllImport(Library, CallingConvention = Convention, ExactSpelling = true)]
        internal static extern int cppp2_load_traditional_dict_ex(
            EngineHandle engine,
            IntPtr path,
            int clearExisting,
            ref ErrorRaw error);
    }
}
