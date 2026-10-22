using System;
using System.Runtime.InteropServices;

namespace CppPinyin2
{
    /// <summary>
    /// Owns a <c>cppp2_engine*</c>.
    /// <para>
    /// Using a <see cref="SafeHandle"/> rather than a bare <see cref="IntPtr"/> buys two things the
    /// documentation asks for: the handle is released even if the caller forgets to dispose (and is
    /// not released twice), and every P/Invoke that takes it throws
    /// <see cref="ObjectDisposedException"/> after disposal instead of calling into a freed pointer.
    /// </para>
    /// </summary>
    internal sealed class EngineHandle : SafeHandle
    {
        internal EngineHandle() : base(IntPtr.Zero, ownsHandle: true)
        {
        }

        /// <inheritdoc />
        public override bool IsInvalid => handle == IntPtr.Zero;

        /// <inheritdoc />
        protected override bool ReleaseHandle()
        {
            NativeMethods.cppp2_destroy(handle);
            return true;
        }

        internal static EngineHandle Adopt(IntPtr raw)
        {
            var handle = new EngineHandle();
            handle.SetHandle(raw);
            return handle;
        }
    }
}
