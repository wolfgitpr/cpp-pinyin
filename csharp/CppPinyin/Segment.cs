using System;
using System.Collections.Generic;

namespace CppPinyin2
{
    /// <summary>
    /// One input span and what the engine read for it -- <c>cppp2_segment</c>.
    /// <para>
    /// <see cref="Text"/> is a span of the input, not a character: a run of ASCII letters is one span.
    /// <see cref="Rendered"/> is the ONE rendered string of this span, and it equals <see cref="Text"/>
    /// when the span failed -- which is why it is not called a "reading" here: the reading as data lives
    /// in <see cref="Syllables"/>.
    /// </para>
    /// <para>
    /// <see cref="FailureKind"/> is the failure classification (numerically a <c>cpp_pinyin::v2::Code</c>, 0 =
    /// OK; <see cref="Engine.CodeText"/> is the single authority for its wording). It -- never "is
    /// <see cref="Syllables"/> filled" -- is the failure test: <see cref="Syllables"/> is empty both when
    /// the span failed and when the query did not ask for the structure.
    /// </para>
    /// <para>
    /// <see cref="Candidates"/> is empty unless the query asked for candidates -- with one measured
    /// exception that the conformance fixture pins down: a <em>failed</em> span carries its own text as
    /// a single candidate even when candidates were not requested. It is also not necessarily a
    /// superset of <see cref="Rendered"/>: a phrase-table hit can carry a reading the character table
    /// does not list.
    /// </para>
    /// </summary>
    public sealed class Segment
    {
        internal Segment(
            string text,
            string rendered,
            int failureKind,
            IReadOnlyList<string> candidates,
            IReadOnlyList<Syllable> syllables)
        {
            Text = text;
            Rendered = rendered;
            FailureKind = failureKind;
            Candidates = candidates;
            Syllables = syllables;
        }

        /// <summary>The span of the input this segment came from.</summary>
        public string Text { get; }

        /// <summary>The one rendered string of this span, or the span's own text when it failed.</summary>
        public string Rendered { get; }

        /// <summary>The failure classification: 0 (OK) when the span has a reading, a <c>CPPP2_ERR_*</c>
        /// value otherwise.</summary>
        public int FailureKind { get; }

        /// <summary>Other readings the span could have; empty unless requested.</summary>
        public IReadOnlyList<string> Candidates { get; }

        /// <summary>
        /// The structural side of <see cref="Rendered"/>: exactly one entry on a non-failed span when the
        /// query asked for it (<c>includeReading</c>), and empty when it did not or when the span failed
        /// -- a failed span deliberately does not enter the array.
        /// </summary>
        public IReadOnlyList<Syllable> Syllables { get; }

        /// <summary>Debug-friendly rendering, matching the Python shell's fields.</summary>
        public override string ToString()
        {
            return Text + " -> " + Rendered + (FailureKind == 0 ? string.Empty : " (failed)");
        }
    }

    /// <summary>
    /// Projections over a query result: the C# counterpart of C++'s <c>Result::join()</c>.
    /// <para>
    /// They live here, and not on <see cref="Engine"/>, on purpose: a conversion result is data, and asking it for
    /// its space-separated form is a projection of that data -- exactly how the C++ face words it. Python does the
    /// same with a module-level <c>to_text(segments)</c>.
    /// </para>
    /// </summary>
    public static class SegmentProjections
    {
        /// <summary>
        /// The segments joined into one space-separated string. A failed span contributes its original text
        /// (<see cref="Segment.Rendered"/> equals <see cref="Segment.Text"/> there), so the input stays readable
        /// in the output.
        /// </summary>
        public static string ToText(this IReadOnlyList<Segment> segments)
        {
            if (segments == null)
            {
                throw new ArgumentNullException(nameof(segments));
            }

            var builder = new System.Text.StringBuilder();
            for (int i = 0; i < segments.Count; i++)
            {
                if (i != 0)
                {
                    builder.Append(' ');
                }

                builder.Append(segments[i].Rendered);
            }

            return builder.ToString();
        }
    }
}
