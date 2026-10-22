using System.Runtime.CompilerServices;

// The test project needs internals on purpose, and only two of them:
//   * Utf8.FromPtrPortable -- compiled on every target so the netstandard2.0 decoder has a witness that
//     actually executes (see Utf8.cs); without this the fallback would be shipped untested.
//   * BuildInfo -- the generated native library name, so a test can assert the loader's assumption
//     against the package layout.
[assembly: InternalsVisibleTo("CppPinyin.Tests")]
