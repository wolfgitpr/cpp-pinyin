using System;
using System.IO;

namespace CppPinyin2.Tests
{
    /// <summary>
    /// Where the repository is, found by walking up from the test binaries rather than by counting
    /// <c>..</c> segments: the same test must work from <c>bin/Debug</c>, <c>bin/Release</c> and a CI
    /// checkout without anyone adjusting a path.
    /// </summary>
    internal static class RepoLayout
    {
        internal static string Root { get; } = Find();

        /// <summary>The cross-language fixture (PLUGIN doc section 6.8.4), shared with the Python suite.</summary>
        internal static string FixturePath => Path.Combine(Root, "conformance", "cases.json");

        /// <summary>The fixture's SHA256, pinned next to it in the repository.</summary>
        internal static string PinPath => Path.Combine(Root, "conformance", "cases.sha256");

        /// <summary>The single source of truth for dictionaries (never a package copy).</summary>
        internal static string DictDirectory => Path.Combine(Root, "data", "dialects");

        /// <summary>The design document, read for the registered fixture count (reconciliation step 3).</summary>
        internal static string PluginDocPath => Path.Combine(Root, "docs", "PLUGIN_INTERFACE_AND_BINDINGS.md");

        private static string Find()
        {
            DirectoryInfo? directory = new DirectoryInfo(AppContext.BaseDirectory);
            while (directory != null)
            {
                if (File.Exists(Path.Combine(directory.FullName, "CMakeLists.txt")) &&
                    File.Exists(Path.Combine(directory.FullName, "conformance", "cases.json")))
                {
                    return directory.FullName;
                }

                directory = directory.Parent;
            }

            throw new InvalidOperationException(
                "could not find the repository root above '" + AppContext.BaseDirectory +
                "' (a directory holding both CMakeLists.txt and conformance/cases.json)");
        }
    }
}
