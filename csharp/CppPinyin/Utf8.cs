using System;
using System.Runtime.InteropServices;
using System.Text;

namespace CppPinyin2
{
    /// <summary>
    /// UTF-8 marshalling helpers. Every string crossing this boundary is UTF-8: the C ABI takes
    /// <c>const char*</c> and promises UTF-8, so decoding with the platform's default code page would
    /// produce mojibake on a non-UTF-8 host and nothing would fail.
    /// </summary>
    internal static class Utf8
    {
        /// <summary>
        /// Decode a NUL-terminated UTF-8 string the library handed out.
        /// <para>
        /// netstandard2.0 has no <c>Marshal.PtrToStringUTF8</c> (it arrived in netstandard2.1), which is
        /// why the first draft of this binding targeted 2.1. Decision D36 = A (2026-10-01) kept .NET
        /// Framework in scope instead, so this decodes by hand on that target.
        /// </para>
        /// </summary>
        internal static string FromPtr(IntPtr pointer)
        {
#if NETSTANDARD2_0
            return FromPtrPortable(pointer);
#else
            return pointer == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
#endif
        }

        /// <summary>
        /// The manual decoder, compiled on <em>every</em> target so a test can actually run it.
        /// <para>
        /// Why it is outside the <c>#if</c>: this host has no .NET Framework, so the netstandard2.0
        /// assembly is compile-verified only. A decoder that existed only inside that compilation would
        /// be shipped on the strength of "it compiles" (the declared-but-never-run shape this
        /// repository keeps auditing for). The test suite runs this decoder on every modern target it
        /// is built for (net7.0 and net10.0) and compares it against the framework's own
        /// <c>PtrToStringUTF8</c>, while the netstandard2.0 build calls the very same code, so there is
        /// one implementation and it has a witness.
        /// </para>
        /// </summary>
        internal static string FromPtrPortable(IntPtr pointer)
        {
            if (pointer == IntPtr.Zero)
            {
                return string.Empty;
            }

            int length = 0;
            while (Marshal.ReadByte(pointer, length) != 0)
            {
                length++;
            }

            if (length == 0)
            {
                return string.Empty;
            }

            byte[] buffer = new byte[length];
            Marshal.Copy(pointer, buffer, 0, length);
            return Encoding.UTF8.GetString(buffer);
        }
    }

    /// <summary>
    /// A UTF-8 copy of a managed string in unmanaged memory, alive for the duration of one call.
    /// <para>
    /// This exists instead of <c>[MarshalAs(UnmanagedType.LPUTF8Str)]</c> because that enum member is not
    /// part of netstandard2.0: the compiler rejects it for that target (CS0117, measured on this host).
    /// The only marshalling alternative there, <c>LPStr</c>, is the platform's ANSI code page, which turns
    /// Chinese text into mojibake on any host whose code page is not UTF-8, silently. Encoding explicitly
    /// is the one spelling that behaves identically on both targets, so every string parameter uses it.
    /// </para>
    /// </summary>
    internal sealed class Utf8String : IDisposable
    {
        private IntPtr _pointer;

        /// <summary>Copies <paramref name="value"/> into unmanaged NUL-terminated UTF-8.</summary>
        internal Utf8String(string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            _pointer = Marshal.AllocHGlobal(bytes.Length + 1);
            Marshal.Copy(bytes, 0, _pointer, bytes.Length);
            Marshal.WriteByte(_pointer, bytes.Length, 0); // the C ABI takes NUL-terminated strings
        }

        /// <summary>The unmanaged pointer, valid until this object is disposed.</summary>
        internal IntPtr Pointer => _pointer;

        /// <inheritdoc />
        public void Dispose()
        {
            if (_pointer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(_pointer);
                _pointer = IntPtr.Zero;
            }
        }
    }
}
