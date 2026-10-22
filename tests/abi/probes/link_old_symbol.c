/* Negative probe for the C ABI's generation token: this file must FAIL TO LINK once the rename has
 * landed, and that failure is the evidence -- not an accident to be repaired.
 *
 * It is deliberately NOT a build target, and it deliberately does NOT include ext/c_api.h. Including the
 * header would turn the verdict into a COMPILE error, and the question being asked is a LINK-level one:
 * after the tokenised rebuild, does a consumer binary that still spells the old symbol find a definition?
 * A silent success here means the rename never reached the built library.
 *
 * The declaration below is therefore spelled by hand and has no prototype to agree with; only the symbol
 * name matters. tests/abi/probes/link_new_symbol.c is its positive half -- a verdict from one half alone
 * proves nothing, because "did not link" is also what a missing library path produces.
 */
extern void cppp_open(void);

int main(void) {
    cppp_open();
    return 0;
}
