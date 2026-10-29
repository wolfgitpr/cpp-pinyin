using System.Runtime.CompilerServices;

// The test project needs internals on purpose, and only one of them now:
//   * BuildInfo -- the generated native library name, so a test can assert the loader's assumption
//     against the package layout.
// Utf8.FromPtrPortable was the other one; it went with the netstandard2.0 target on 2026-10-27 (D41).
[assembly: InternalsVisibleTo("CppPinyin.Tests")]
