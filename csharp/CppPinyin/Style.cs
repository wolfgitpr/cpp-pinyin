namespace CppPinyin2
{
    /// <summary>
    /// The library's own style numbering (transcribed from <c>Types.h</c> via the verified Python
    /// shell) -- NOT pypinyin's, and deliberately not identical to it.
    /// <para>
    /// <see cref="Normal"/> (0) is the only value both built-in dialects share; <see cref="FirstLetter"/>
    /// (3) and <see cref="Tone3"/> (8) exist for both; the rest are Mandarin-only. A style a dialect
    /// does not support returns the input unchanged, which is the library's contract.
    /// </para>
    /// <para>
    /// <see cref="Default"/> (-1) is not a style: it is the sentinel "whatever this dialect defaults
    /// to", and it is what every entry point below asks for when the caller names no style. The library
    /// resolves it (<c>core/Types.h</c> -&gt; <c>resolveStyle</c>), so an unspecified style means
    /// Mandarin <see cref="Tone"/> and Cantonese <see cref="Tone3"/> in ALL FOUR layers -- C#, Python,
    /// C++ and the C ABI alike. It used to be <see cref="Normal"/>, which made this binding one of the
    /// two layers that disagreed with the C++ facades about the same sentence (C4/N27 gave "unset" a
    /// value of its own).
    /// </para>
    /// </summary>
    public enum Style
    {
        /// <summary>
        /// No style asked for (-1): whatever the dialect itself defaults to. Not a style -- the
        /// numbering starts at 0 -- and never valid where the library expects a real style.
        /// </summary>
        Default = -1,

        /// <summary>Plain pinyin without tone marks (0).</summary>
        Normal = 0,

        /// <summary>Standard tone style, the MARK over the final's first vowel (1). 中国 -&gt;
        /// zhōng guó. The default.</summary>
        Tone = 1,

        /// <summary>Tone as a digit placed AFTER the final's vowel (2), not "an alternate form of 1":
        /// 中国 -&gt; zho1ng guo2. See the per-value comments on <c>MandarinStyle</c> in Types.h, which
        /// is the authority this enum transcribes.</summary>
        Tone2 = 2,

        /// <summary>First letter of each syllable only (3).</summary>
        FirstLetter = 3,

        /// <summary>Bopomofo (4).</summary>
        Bopomofo = 4,

        /// <summary>Initials only (5).</summary>
        Initials = 5,

        /// <summary>Finals only (6).</summary>
        Finals = 6,

        /// <summary>Finals with tone as a digit (7).</summary>
        FinalsTone3 = 7,

        /// <summary>Tone as a digit on the whole syllable (8).</summary>
        Tone3 = 8,
    }

    /// <summary>What a query does with a span it cannot read (c_api.h <c>ErrorHandling</c>).</summary>
    public enum OnError
    {
        /// <summary>Mark the span as failed (0).</summary>
        Default = 0,

        /// <summary>Drop the span from the result (1).</summary>
        Drop = 1,
    }
}
