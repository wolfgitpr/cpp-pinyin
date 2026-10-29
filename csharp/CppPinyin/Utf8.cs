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
        /// <c>Marshal.PtrToStringUTF8</c> is the whole implementation: both targets this package ships
        /// (net7.0, net10.0) have it. The hand-written decoder that used to sit beside it existed only
        /// for netstandard2.0 and went with that target on 2026-10-27 (D36 revoked, see DECISIONS.md).
        /// </para>
        /// </summary>
        internal static string FromPtr(IntPtr pointer)
        {
            return pointer == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUTF8(pointer) ?? string.Empty;
        }
    }

    /// <summary>
    /// A UTF-8 copy of a managed string in unmanaged memory, alive for the duration of one call.
    /// <para>
    /// It was written because <c>[MarshalAs(UnmanagedType.LPUTF8Str)]</c> is not part of netstandard2.0
    /// (CS0117 on that target), and that reason left with the target on 2026-10-27. It stays because the
    /// alternative is a marshalling attribute that behaves differently from this explicit copy in ways
    /// nobody has measured here -- <c>LPStr</c> is still the platform ANSI code page, which mangles
    /// Chinese silently -- and one spelling for every string parameter is worth more than the attribute.
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
