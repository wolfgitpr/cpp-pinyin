using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace CppPinyin2
{
    /// <summary>
    /// Resolves the native library on the modern targets (net7.0 and net10.0), where
    /// <c>NativeLibrary.SetDllImportResolver</c> exists.
    /// <para>
    /// The package is expected to carry the library under <c>runtimes/&lt;rid&gt;/native/</c>. .NET 5+
    /// already probes that for <c>DllImport</c>, but a failure there surfaces as a bare
    /// <c>DllNotFoundException</c> with no context, and the documentation forbids that: the resolver
    /// looks in a short, named list of places and otherwise throws an exception naming them.
    /// </para>
    /// <para>
    /// On netstandard2.0 there is no resolver hook, so the library must sit where the OS loader looks
    /// (next to the assembly or on the search path), and failures surface as
    /// <see cref="DllNotFoundException"/> wrapped by <see cref="NativeLoader.DescribeMissingLibrary"/>.
    /// </para>
    /// </summary>
    internal static class NativeLoader
    {
#if !NETSTANDARD2_0
        private static bool _registered;
#endif

        internal static void Register()
        {
#if !NETSTANDARD2_0
            if (_registered)
            {
                return;
            }

            _registered = true;
            NativeLibrary.SetDllImportResolver(typeof(NativeLoader).Assembly, Resolve);
#endif
        }

        /// <summary>A message naming what was looked for and where, for a load failure.</summary>
        internal static string DescribeMissingLibrary()
        {
            var places = SearchPath();
            return "could not load the native library '" + NativeMethods.Library +
                   "'. Looked in: " + string.Join(", ", places) +
                   ". Build it (cmake) and place it next to the assembly, or install the package that " +
                   "carries it under runtimes/<rid>/native/.";
        }

        private static List<string> SearchPath()
        {
            var places = new List<string> { AppContext.BaseDirectory };
            string? rid = Rid();
            if (rid != null)
            {
                places.Add(Path.Combine(AppContext.BaseDirectory, "runtimes", rid, "native"));
            }

            return places;
        }

        private static string? Rid()
        {
            string? architecture = RuntimeInformation.ProcessArchitecture switch
            {
                Architecture.X64 => "x64",
                Architecture.X86 => "x86",
                Architecture.Arm64 => "arm64",
                Architecture.Arm => "arm",
                _ => null,
            };
            if (architecture == null)
            {
                return null;
            }

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                return "win-" + architecture;
            }

            if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                return "osx-" + architecture;
            }

            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {
                return "linux-" + architecture;
            }

            return null;
        }

#if !NETSTANDARD2_0
        private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        {
            if (!string.Equals(libraryName, NativeMethods.Library, StringComparison.Ordinal))
            {
                return IntPtr.Zero; // not ours: let the runtime decide
            }

            foreach (string directory in SearchPath())
            {
                if (!Directory.Exists(directory))
                {
                    continue;
                }

                foreach (string candidate in Candidates(directory))
                {
                    if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out IntPtr handle))
                    {
                        return handle;
                    }
                }
            }

            throw new CppPinyinException(DescribeMissingLibrary());
        }

        private static IEnumerable<string> Candidates(string directory)
        {
            string baseName = Path.Combine(directory, NativeMethods.Library);
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                yield return baseName + ".dll";
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            {
                yield return baseName + ".dylib";
            }
            else
            {
                yield return baseName + ".so";
            }
        }
#endif
    }
}
