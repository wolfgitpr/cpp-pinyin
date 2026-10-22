using System;

namespace CppPinyin2
{
    /// <summary>
    /// Base class for every error this binding raises (mirrors the Python shell's hierarchy).
    /// <para>
    /// The names are the C ABI's value domain (<c>CPPP2_OK</c> / <c>CPPP2_ERR_*</c>, numerically
    /// <c>cpp_pinyin::v2::Code</c> 0..5); the sentence for a code is not written here -- it is
    /// <see cref="Engine.CodeText"/>, which asks the library.
    /// </para>
    /// </summary>
    public class CppPinyinException : Exception
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinException(string message) : base(message) { }

        /// <summary>Creates the exception with a message and an inner exception.</summary>
        public CppPinyinException(string message, Exception inner) : base(message, inner) { }
    }

    /// <summary>
    /// <c>CPPP2_ERR_INVALID_ARG</c> -- an argument was NULL or empty.
    /// <para>
    /// Also what a span's own <see cref="Segment.FailureKind"/> carries when the library cannot read that
    /// span (a non-Hanzi or an unknown character is exactly "something the library cannot use"), and what
    /// <see cref="Engine.RenderSyllable"/> reports for a structure that does not denote one syllable or
    /// whose spelling cannot be rebuilt.
    /// </para>
    /// </summary>
    public sealed class CppPinyinInvalidArgumentException : CppPinyinException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinInvalidArgumentException(string message) : base(message) { }
    }

    /// <summary>
    /// <c>CPPP2_ERR_NOT_READY</c> -- the handle exists but is not usable.
    /// Not reachable through <see cref="Engine"/> after a successful open (cppp2_open publishes only ready
    /// handles), mapped anyway so a shell never has to guess what an unfamiliar status meant.
    /// </summary>
    public class CppPinyinNotReadyException : CppPinyinException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinNotReadyException(string message) : base(message) { }
    }

    /// <summary>
    /// <c>CPPP2_ERR_NOT_READY</c> from <c>cppp2_open</c>: the id is not in the registry.
    /// <para>
    /// Reachable since the ABI's entry point reports an unknown id as a status of its own, instead of a
    /// NULL handle plus a message: the library never resolves an unknown id to another dialect, and
    /// <c>cppp2_open</c>'s <c>detail</c> names the id (the shell carries that detail into its message). It
    /// derives from <see cref="CppPinyinNotReadyException"/> because it IS that status -- the refined name
    /// is what lets a caller catch this case without catching every not-ready failure.
    /// </para>
    /// </summary>
    public sealed class CppPinyinUnknownDialectException : CppPinyinNotReadyException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinUnknownDialectException(string message) : base(message) { }
    }

    /// <summary><c>CPPP2_ERR_LOAD</c> -- the path was readable but the table did not load.</summary>
    public sealed class CppPinyinDictionaryException : CppPinyinException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinDictionaryException(string message) : base(message) { }
    }

    /// <summary>
    /// <c>CPPP2_ERR_DICT_INCOMPATIBLE</c> -- the shipped data is NEWER than this library understands.
    /// <para>
    /// The engine reads the dialect's <c>format.txt</c> before any table and refuses a value above its own
    /// <c>kSupportedDictFormat</c> instead of half-reading the data. A type of its own because the ACTION
    /// differs from <see cref="CppPinyinDictionaryException"/>: nothing is wrong with the file, the library
    /// is old -- upgrade it, or point at older data.
    /// </para>
    /// </summary>
    public sealed class CppPinyinDictionaryIncompatibleException : CppPinyinException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinDictionaryIncompatibleException(string message) : base(message) { }
    }

    /// <summary><c>CPPP2_ERR_INTERNAL</c> -- an exception was caught at the C boundary, or a marshal failed.</summary>
    public sealed class CppPinyinInternalException : CppPinyinException
    {
        /// <summary>Creates the exception with a message.</summary>
        public CppPinyinInternalException(string message) : base(message) { }
    }

    /// <summary>
    /// The status codes of <c>include/cpp-pinyin2/ext/c_api.h</c> and their mapping to exceptions.
    /// Values are the header's, verified against it (0..5).
    /// </summary>
    internal static class Status
    {
        internal const int Ok = 0;
        internal const int InvalidArgument = 1;
        internal const int NotReady = 2;
        internal const int Load = 3;
        internal const int Internal = 4;
        internal const int DictionaryIncompatible = 5;

        internal static string Name(int status)
        {
            switch (status)
            {
                case Ok: return "CPPP2_OK";
                case InvalidArgument: return "CPPP2_ERR_INVALID_ARG";
                case NotReady: return "CPPP2_ERR_NOT_READY";
                case Load: return "CPPP2_ERR_LOAD";
                case Internal: return "CPPP2_ERR_INTERNAL";
                case DictionaryIncompatible: return "CPPP2_ERR_DICT_INCOMPATIBLE";
                default: return "unknown CPPP_ status " + status.ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
        }

        /// <summary>
        /// Take the message the library wrote into a <c>cppp2_error</c> and release it -- the struct's
        /// ownership rule, applied once, so no caller has to branch on success before it can clean up.
        /// Safe on success (<c>detail</c> is NULL, nothing is freed, "" comes back). The pointer is
        /// cleared afterwards, which makes a second call a no-op instead of a double free; the status code
        /// stays readable, because it is the caller's own field.
        /// </summary>
        internal static string TakeDetail(ref ErrorRaw error)
        {
            string detail = Utf8.FromPtr(error.detail);
            if (error.detail != IntPtr.Zero)
            {
                NativeMethods.cppp2_string_free(error.detail);
                error.detail = IntPtr.Zero;
            }

            return detail;
        }

        /// <summary>The one place a status becomes a sentence: <c>&lt;where&gt; failed with &lt;code&gt;</c>,
        /// with the library's own detail appended when there is one.</summary>
        internal static string Describe(string where, int status, string detail)
        {
            return where + " failed with " + Name(status) +
                   (string.IsNullOrEmpty(detail) ? string.Empty : ": " + detail);
        }

        /// <summary>
        /// Turn a C status into an exception. Success is an explicit <c>status == Ok</c>.
        /// The fallback branch matters: an unrecognised code must not fall through as success, which
        /// is how "returned nothing" becomes "returned nothing and nobody noticed".
        /// </summary>
        internal static void ThrowIfFailed(int status, string where, string detail)
        {
            if (status == Ok)
            {
                return;
            }

            string message = Describe(where, status, detail);
            switch (status)
            {
                case InvalidArgument: throw new CppPinyinInvalidArgumentException(message);
                case NotReady: throw new CppPinyinNotReadyException(message);
                case Load: throw new CppPinyinDictionaryException(message);
                case Internal: throw new CppPinyinInternalException(message);
                case DictionaryIncompatible: throw new CppPinyinDictionaryIncompatibleException(message);
                default: throw new CppPinyinException(message);
            }
        }
    }
}
