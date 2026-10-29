#pragma once

#include <cpp-pinyin2/core/PinyinGlobal.h>
#include <cpp-pinyin2/core/ToneConverter.h>
#include <cpp-pinyin2/core/ToneSandhi.h>
#include <cpp-pinyin2/core/Types.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cpp_pinyin::v2
{
    /// A complete description of one dialect -- the plugin entry point.
    ///
    /// Adding a dialect does NOT require touching this library: provide dictionary files, a
    /// `ToneConverter` subclass and one `Dialect`, then hand either to `Engine` (directly, or through a
    /// `Registry` so it can be addressed by id).
    ///
    /// Two properties are deliberate and load-bearing:
    ///
    ///  * **`tone` is held by value, not by pointer.** `EngineImpl` copies the converter it is given, and
    ///    a value copy slices to the `ToneConverter` base subobject -- harmless here because every dialect
    ///    behaviour lives in that base's *function pointer* table (`m_converts`, `m_normalize`), never in
    ///    a virtual. Do not "improve" this into a virtual.
    ///
    ///  * **A `Dialect` carries no dictionary path and no loaded state.** The root is a constructor
    ///    argument of `Engine`, so two instances of one dialect can read two directories at the same time.
    ///
    /// Thread safety: a `Dialect` is an inert value; the `Engine` objects built from it are independent.
    struct CPP_PINYIN2_EXPORT Dialect {
        /// Identifier used by `Engine::open`/`Registry`, e.g. "mandarin", "cantonese".
        std::string id;

        /// Subdirectory of the dictionary root that holds this dialect's tables. Empty means
        /// "use `id`". Set it explicitly when the directory name differs from the id.
        std::string dictSubDir;

        /// The dialect's output styles and external-reading normalization. Default-constructed
        /// `ToneConverter` is legal: every style slot is empty, so every style passes the reading
        /// through unchanged (see `ToneConverter::convert`).
        ToneConverter tone;

        /// Whether the `vToU` switch means anything for this dialect. When false, a caller that
        /// sets `EngineConfig::vToU` has it ignored rather than silently changing the output --
        /// this mirrors what the built-in Cantonese dialect has always done.
        bool supportsVToU = false;

        /// Same, for `markNeutralAs5`.
        bool supportsNeutralTone5 = false;

        /// What this dialect can emit and what each style is called -- the single authority for "does
        /// this dialect have a 'tone3', and which number is it". EMPTY means it declares nothing: every
        /// style number passes the reading through unchanged and there is no default to fall back to.
        /// `findStyle` (core/Types.h) is the lookup; tests/cpp/test_style_table.cpp pins the built-ins.
        ///
        /// The DEFAULT is the entry whose `isDefault` is set (`defaultStyleOf` reads it,
        /// `cppp2_style_list` publishes the same flag). None marked means "no opinion, ask explicitly" and
        /// resolves to style 0; more than one is a plugin bug (the first wins). Deliberately no second
        /// field saying it: that field let a description declare a default its renderer did not use.
        std::vector<StyleDecl> styles;

        /// Which tone-sandhi pass this dialect declares (`core/ToneSandhi.h` has the enumerator and the
        /// reasoning). `None` means "this dialect has no tone-sandhi pass", and every entry point that
        /// offers sandhi asks THIS field instead of assuming the Mandarin rules apply:
        /// `cppp2_apply_tone_sandhi` refuses with `CPPP2_ERR_INVALID_ARG` when the answer is `None`, which is
        /// what stops a Cantonese result from being rewritten by 不/一 rules that Cantonese does not have.
        /// Setting it to `Mandarin` is a claim that this dialect's readings ARE Mandarin-shaped (tone digit
        /// on a TONE3/FINALS_TONE3 syllable); flipping it without that is a lie the tests cannot catch for you.
        ToneSandhiPass toneSandhi = ToneSandhiPass::None;

        /// Optional post-pass over a finished result, applied by the `Result`-returning
        /// `Engine::query` only (its result is the thing a caller rewrites in place).
        /// Empty means "do not intervene". This is the supported place for caller-side fixes; the
        /// engine's own disambiguation window is frozen and must not be reimplemented here.
        ///
        /// THREADS: like the disambiguation callback, this runs on the CALLING thread, in place, and is NOT
        /// serialised -- concurrent queries on one shared engine can be inside it simultaneously, so it must
        /// be thread-safe itself and must not capture mutable state the queries share.
        std::function<void(Result &)> postProcess;
    };

}  // namespace cpp_pinyin::v2
