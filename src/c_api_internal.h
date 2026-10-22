#pragma once

// Shared internals of the C API, split out of c_api.cpp (the P2 split; its ledger was retired in the
// 2026-10-09 documentation cut, see docs/HISTORY.md). The helpers were an anonymous namespace; a header
// cannot keep one (each including TU would get its own copy, breaking the ODR for the handle), so it is now the named
// namespace cppp_detail, and both halves of the C API pull the names in with `using namespace cppp_detail;` -- which is
// why no call site in the moved functions changed.
//
// `struct cppp2_engine` stays OUTSIDE that namespace on purpose: include/cpp-pinyin2/ext/c_api.h only
// typedefs the opaque name, so the definition has to be the same global tag.

// C ABI implementation -- see include/cpp-pinyin2/ext/c_api.h for the contract and the reasons behind
// each rule, and docs/PLUGIN_INTERFACE_AND_BINDINGS.md section 6.2.1 for the incidents those rules came
// from. Nothing here changes engine behaviour: every function is a marshalling wrapper around
// `cpp_pinyin::v2::Engine`, and every allocation it hands out is released by the matching cppp2_*_free.

#include <cpp-pinyin2/ext/c_api.h>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// LAYOUT PINS for the three structs whose padding is now CLAIMED.
//
// `cppp2_error`, `cppp2_syllable` and `cppp2_segment` each carry a `reserved_0` sitting in what used to be the
// compiler's hole. That buys one thing: a future field can be appended WITHOUT changing `sizeof`, so an older
// consumer's array stride (`cppp2_segment *` + k * sizeof) keeps reading the right bytes. These asserts are
// what makes that promise mechanical -- move a field, change a width, or reorder the tail and the build fails
// here instead of in a C host that nobody recompiled.
static_assert(offsetof(cppp2_error, detail) == 2 * sizeof(int32_t),
              "cppp2_error: `detail` must follow `code` + `reserved_0`");
static_assert(offsetof(cppp2_syllable, reserved_0) == 3 * sizeof(void *) + 3 * sizeof(int32_t),
              "cppp2_syllable: `reserved_0` must sit in the trailing hole");
static_assert(offsetof(cppp2_segment, syllables) == 2 * sizeof(void *) + 2 * sizeof(int32_t),
              "cppp2_segment: `reserved_0` must sit in the hole before `syllables`");
static_assert(sizeof(cppp2_segment) == 4 * sizeof(void *) + 2 * sizeof(int32_t) + 2 * sizeof(size_t),
              "cppp2_segment: the tail must have no unclaimed padding left");

#ifndef CPP_PINYIN2_VERSION_STRING
#define CPP_PINYIN2_VERSION_STRING "0.0.0-unknown"
#endif


namespace cppp_detail
{

    // Every string that crosses the boundary is allocated here and released by our own free
    // functions, so a caller with a different CRT never frees our memory (rule 3).
    // Overload for the alloc-free failure path (2026-10-20, audit round 2 §12.6 C6): the C-string form
    // exists so `cppp2_open`'s `fail()` never has to build a `std::string` to report a literal. The
    // allocation below is `nothrow`, so a failed copy leaves NULL -- the documented "we do not own a
    // message" outcome -- instead of throwing out of a `catch` handler.
    inline char *dup(const char *s) {
        if (s == nullptr)
            return nullptr;
        const std::size_t n = std::strlen(s);
        auto *p = new (std::nothrow) char[n + 1];
        if (p != nullptr)
            std::memcpy(p, s, n + 1);
        return p;
    }

    inline char *dup(const std::string &s) {
        auto *p = new (std::nothrow) char[s.size() + 1];
        if (p != nullptr)
            std::memcpy(p, s.c_str(), s.size() + 1);
        return p;
    }

    // How much of the caller's struct its `struct_size` actually covers (rule 6). `struct_size == 0`
    // means "my copy is exactly the declaration in c_api.h" -- c_api.h has said so all along and
    // `cppp2_query_opts_init` (and tests/c_api_smoke.c) have always behaved that way. Normalising here is
    // what makes the READ path agree with both: before 2026-10-18 a zeroed struct whose `style` the
    // caller had filled was read as "no field at all", so the query silently rendered the dialect default
    // instead of the style asked for, with no diagnostic anywhere (S9c's neighbourhood, a different bug).
#define CPPP_DECLARED_SIZE(opts) ((opts)->struct_size == 0 ? sizeof(cppp2_query_opts) : (opts)->struct_size)

    // Reads only the fields the caller's `struct_size` says exist (rule 6).
#define CPPP_HAS_FIELD(opts, member)                                                                                   \
    (CPPP_DECLARED_SIZE(opts) >= offsetof(cppp2_query_opts, member) + sizeof((opts)->member))

    inline cpp_pinyin::v2::EngineConfig config_of(const cppp2_query_opts *opts) {
        cpp_pinyin::v2::EngineConfig cfg;
        if (opts == nullptr)
            return cfg;
        if (CPPP_HAS_FIELD(opts, style))
            cfg.style = opts->style;
        if (CPPP_HAS_FIELD(opts, on_error))
            cfg.onError =
                (opts->on_error == 1) ? cpp_pinyin::v2::ErrorHandling::Drop : cpp_pinyin::v2::ErrorHandling::Default;
        if (CPPP_HAS_FIELD(opts, include_candidates))
            cfg.includeCandidates = (opts->include_candidates != 0);
        if (CPPP_HAS_FIELD(opts, v_to_u))
            cfg.vToU = (opts->v_to_u != 0);
        if (CPPP_HAS_FIELD(opts, mark_neutral_as_5))
            cfg.markNeutralAs5 = (opts->mark_neutral_as_5 != 0);
        // APPENDED field (S9a-2): a caller whose struct ends before it declared no opinion, and the
        // default -- do not build the structure -- is what such a caller has always got.
        if (CPPP_HAS_FIELD(opts, include_reading))
            cfg.includeReading = (opts->include_reading != 0);
        return cfg;
    }

    // Characters that separate one syllable from another on every surface this library writes: the
    // dictionary tables' own separators (`:` in word.txt, `,` in the phrase tables) and the ones a
    // caller types between syllables (whitespace, `-`, `'`, `/`). No syllable's own spelling contains
    // one, which is what makes "is this ONE syllable?" decidable without a parser (plan A2): a
    // `cppp2_syllable` carrying one is a multi-syllable string in the wrong field, and
    // cppp2_render_syllable reports CPPP2_ERR_INVALID_ARG instead of splitting it (splitting is the
    // kernel's job, never the binding's).
    inline bool hasSeparator(const char *s) {
        for (; s != nullptr && *s != '\0'; ++s) {
            switch (*s) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case ',':
            case ':':
            case '-':
            case '\'':
            case '/':
                return true;
            default:
                break;
            }
        }
        return false;
    }

    // CPPP_HAS_FIELD stays defined on purpose: cppp2_query_opts_init() below gates its *writes* with
    // the same rule the read path uses. It is #undef'd right after that function.

    // Deep copy of a C++ Result into the C layout (S9a-2: the element is `cppp2_segment`, the structure
    // is `cppp2_syllable`). Returns nullptr (and frees what it built) when an allocation fails, so a
    // partially built result never escapes.
    //
    // `includeReading` is the CALLER's switch, not something inferred from the result: the C++ face
    // leaves `Segment::reading` default-constructed when nobody asked for it, so a builder that guessed
    // "the structure is empty, so the caller did not want it" could not tell that case from a row the
    // engine genuinely could not describe (a callback-written reading). The switch is carried here, and
    // the rule it implements is one line: a non-failed span gets exactly one syllable when the caller
    // asked for the structure, a failed span never gets one.
    // What a query result OWNS, in one object (2026-10-20, audit H10). The public struct stays exactly as the
    // header declares it -- `const char *` fields a caller reads and never frees -- but those fields are now
    // views into `strings` / `segments` / `syllables` / `candidates` here, and ONE deletion releases all of it.
    // Before this, the releaser walked the result and freed every string through `const_cast<char *>`; the cast
    // was the only place ownership was written down, which is what made it look like a mistake.
    //
    struct ResultOwner {
        // `pub` is FIRST: the pointer handed to C is this block's address, so the releaser recovers the owner
        // with one reinterpret_cast. The static_assert below pins the order -- it is load-bearing, not style.
        cppp2_result pub{};
        std::vector<std::string> strings;
        std::vector<cppp2_segment> segments;
        std::vector<std::vector<cppp2_syllable>> syllables;
        std::vector<std::vector<const char *>> candidates;
        // One slot per segment for the ONLY in-place mutation the C face has: `cppp2_apply_tone_sandhi`
        // re-renders a span and repoints `segments[i].rendered`. Sized once, at build, so a write never
        // reallocates the vector and never invalidates a pointer already handed out.
        std::vector<std::string> renderedOverrides;
    };

    /// The owner block a public result pointer addresses (its FIRST member). One function, so the invariant is
    /// stated once: `cppp2_result_free` and the tone-sandhi entry both recover through here (audit H10).
    inline ResultOwner *ownerOf(cppp2_result *result) noexcept { return reinterpret_cast<ResultOwner *>(result); }
    static_assert(offsetof(ResultOwner, pub) == 0, "cppp2_result_free recovers the owner by address");

    // How many strings a result will own, so the vector can be reserved EXACTLY once: a reallocation would move
    // the buffers every `c_str()` already handed out points into.
    inline size_t count_strings(const cpp_pinyin::v2::Result &r, bool includeReading) {
        size_t total = 0;
        for (const auto &segment : r) {
            total += 2;  // text + rendered
            if (includeReading && segment.failure_kind == cpp_pinyin::v2::Code::Ok)
                total += 3;  // initial + fin + spelling
            total += segment.candidates.size();  // candidates are copied for failed spans too
        }
        return total;
    }

    inline cppp2_result *make_result(const cpp_pinyin::v2::Result &r, bool includeReading) {
        // Held by a `unique_ptr` and released at the single success return: `nothrow` covers only the
        // allocation of the OWNER, and every `reserve`/`resize`/`emplace_back` below can still throw. That throw
        // unwinds into `cppp2_query`'s `catch (...)` -- which never sees this pointer -- so a raw `new` here
        // leaked the owner plus everything already reserved (found by the 2026-10-22 performance audit).
        auto ownerHolder = std::unique_ptr<ResultOwner>(new (std::nothrow) ResultOwner());
        ResultOwner *owner = ownerHolder.get();
        if (owner == nullptr)
            return nullptr;
        // Reserve before anything is handed out: `c_str()` pointers must survive every push_back.
        owner->strings.reserve(count_strings(r, includeReading));
        owner->segments.resize(r.size());
        owner->syllables.resize(r.size());
        owner->candidates.resize(r.size());
        owner->renderedOverrides.resize(r.size());
        owner->pub.count = r.size();
        owner->pub.segments = owner->segments.empty() ? nullptr : owner->segments.data();

        for (size_t i = 0; i < r.size(); ++i) {
            const cpp_pinyin::v2::Segment &s = r[i];
            cppp2_segment &slot = owner->segments[i];
            owner->strings.emplace_back(s.text);
            slot.text = owner->strings.back().c_str();
            owner->strings.emplace_back(s.rendered);
            slot.rendered = owner->strings.back().c_str();
            slot.failure_kind = static_cast<int32_t>(s.failure_kind);

            // The rule the switch carries: a non-failed span gets exactly one syllable when the caller asked for
            // the structure, and a failed span never gets one.
            if (includeReading && s.failure_kind == cpp_pinyin::v2::Code::Ok) {
                owner->syllables[i].resize(1);
                cppp2_syllable &data = owner->syllables[i][0];
                owner->strings.emplace_back(s.reading.initial);
                data.initial = owner->strings.back().c_str();
                owner->strings.emplace_back(s.reading.fin);
                data.fin = owner->strings.back().c_str();
                owner->strings.emplace_back(s.reading.spelling);
                data.spelling = owner->strings.back().c_str();
                data.tone = s.reading.tone;
                data.tone_kind = static_cast<int32_t>(s.reading.tone_kind);
                data.source = static_cast<int32_t>(s.reading.source);
                slot.syllables = owner->syllables[i].data();
                slot.syllable_count = 1;
            }

            if (s.candidates.empty())
                continue;
            owner->candidates[i].resize(s.candidates.size());
            for (size_t j = 0; j < s.candidates.size(); ++j) {
                owner->strings.emplace_back(s.candidates[j]);
                owner->candidates[i][j] = owner->strings.back().c_str();
            }
            slot.candidates = owner->candidates[i].data();
            slot.candidate_count = s.candidates.size();
        }
        return &ownerHolder.release()->pub;
    }

    // Shared by cppp2_dialect_ids and cppp2_readings: a NULL-terminated-free array of owned strings.
    inline char **dup_list(const std::vector<std::string> &items, size_t *out_count) {
        auto **list = new (std::nothrow) char *[(items.empty() ? 1 : items.size())]();
        if (list == nullptr)
            return nullptr;
        for (size_t i = 0; i < items.size(); ++i) {
            list[i] = dup(items[i]);
            if (list[i] == nullptr) {
                for (size_t j = 0; j < i; ++j)
                    delete[] list[j];
                delete[] list;
                return nullptr;
            }
        }
        *out_count = items.size();
        return list;
    }

    inline void free_list(char **list, size_t count) {
        if (list == nullptr)
            return;
        for (size_t i = 0; i < count; ++i)
            delete[] list[i];
        delete[] list;
    }

}  // namespace cppp_detail

// ---------------------------------------------------------------------------- handle ---

// The handle owns exactly one engine. It used to carry a second member -- the message explaining why
// the engine was unusable -- which was ALWAYS an empty string (the constructor below passed
// `std::string()` and nothing ever wrote it), so `cppp_last_error` could not have reported anything
// even if it had been kept. `cppp2_open` reports failures through its `err` out-parameter and publishes
// no handle at all; N4b removed the accessor and this member with it.
struct cppp2_engine {
    cpp_pinyin::v2::Engine engine;
};

// The readiness guard every entry point that takes a handle asks first. `cppp2_open` refuses to publish
// an unusable engine and nothing moves the engine out of a published handle, so no call in this
// library can reach the false branch today -- and it is not a testable branch either: `cppp2_destroy`
// deletes the handle, so using one afterwards is undefined behaviour rather than a state the tree can
// construct. It stays because the alternative reading of `Dialect::id` (a resolve that produced an
// empty id) would otherwise be answered with a query instead of CPPP2_ERR_NOT_READY.
inline bool usable(const cppp2_engine *handle) { return handle != nullptr && !handle->engine.dialect().id.empty(); }
