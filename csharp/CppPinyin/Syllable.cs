using System;

namespace CppPinyin2
{
    /// <summary>
    /// One STRUCTURED reading -- <c>cppp2_syllable</c>: data, never a rendering.
    /// <para>
    /// <see cref="Initial"/> is "" for a zero-initial syllable; <see cref="Fin"/> is the canonical final
    /// (ü written as <c>v</c>, uei/iou/uen spelled out, <c>zhi</c> -&gt; <c>i</c>) -- the two strings the
    /// <see cref="Style.Initials"/> and <see cref="Style.Finals"/> styles render. The pair is a PROJECTION
    /// of the table reading, not a re-encoding of it, so it does not always rebuild the spelling the table
    /// stores -- <see cref="Spelling"/> is that spelling, verbatim, and is what
    /// <see cref="Engine.RenderSyllable"/> renders when it is set.
    /// </para>
    /// <para>
    /// <see cref="Tone"/> is a dialect-internal tone-category number and is NOT comparable across dialects
    /// (Mandarin 0..5, Cantonese 1..6): the same integer means a different category in each. It is filled
    /// for every style, including the ones whose string carries no tone. <see cref="ToneKind"/> and
    /// <see cref="Source"/> are the two value domains of the C header (<c>CPPP2_TONE_KIND_*</c> /
    /// <c>CPPP2_SOURCE_*</c>); the shell does not rename them, so a caller can compare them against that
    /// header directly.
    /// </para>
    /// </summary>
    public sealed class Syllable
    {
        /// <summary>
        /// Creates one structured reading. Public on purpose: a caller that holds the two halves can build
        /// the structure itself and hand it to <see cref="Engine.RenderSyllable"/>, so this shell never owns
        /// a second, disagreeing style implementation.
        /// <para>
        /// <paramref name="spelling"/> is OPTIONAL and last. A caller holding only the halves leaves it
        /// empty and rendering falls back to the (initial, fin) projection -- exactly what a NULL
        /// <c>spelling</c> means in the C struct. A reading that came out of <see cref="Engine.Query"/>
        /// always carries it, and it is the row's IDENTITY: the projection cannot rebuild it for every
        /// reading (the interjection <c>hm</c> has no final at all, so the projection has nothing to work
        /// with), and before this field existed such a reading could only be refused.
        /// </para>
        /// </summary>
        public Syllable(string initial, string fin, int tone, int toneKind, int source,
                        string spelling = "")
        {
            Initial = initial ?? throw new ArgumentNullException(nameof(initial));
            Fin = fin ?? throw new ArgumentNullException(nameof(fin));
            Tone = tone;
            ToneKind = toneKind;
            Source = source;
            Spelling = spelling ?? throw new ArgumentNullException(nameof(spelling));
        }

        /// <summary>The initial, "" for a zero-initial syllable.</summary>
        public string Initial { get; }

        /// <summary>The canonical final (ü written as <c>v</c>).</summary>
        public string Fin { get; }

        /// <summary>
        /// The table's own reading bytes -- this row's identity. <see cref="Initial"/> and
        /// <see cref="Fin"/> are its projection, which is not invertible for every reading.
        /// </summary>
        public string Spelling { get; }

        /// <summary>The dialect-internal tone-category number (never compared across dialects).</summary>
        public int Tone { get; }

        /// <summary>The kind of table entry that was read, a <c>CPPP2_TONE_KIND_*</c> value.</summary>
        public int ToneKind { get; }

        /// <summary>The table the reading came from, a <c>CPPP2_SOURCE_*</c> value.</summary>
        public int Source { get; }

        /// <summary>Debug-friendly rendering, matching the Python shell's fields.</summary>
        public override string ToString()
        {
            return Initial + "-" + Fin + "/" + Tone;
        }
    }
}
