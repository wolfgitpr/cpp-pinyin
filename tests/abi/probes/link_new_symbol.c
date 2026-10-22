/* Positive half of the C ABI link probe: this file must LINK.
 *
 * Same instrument as link_old_symbol.c (hand-written declaration, no header, not a build target), with the
 * tokenised spelling. Both halves are run against the same library in the same invocation: "the old name
 * does not link" is only evidence when "the new name does link" holds at the same time.
 *
 * The token is the package's major version (docs/STABILITY.md, R7); it is spelled out here on purpose --
 * a probe that derived the symbol name from the header would follow the header wherever it went and could
 * not detect a rename that failed to reach the library.
 */
extern void cppp2_open(void);

int main(void) {
    cppp2_open();
    return 0;
}
