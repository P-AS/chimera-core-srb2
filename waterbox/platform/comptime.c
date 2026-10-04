/* comptime.c - what upstream's comptime.c says of the build (its date, time
 * and git state, shown by the console's "version"), fixed: the native
 * reference and the sandboxed core are built at different times and must draw
 * the same console. */
const char *compbranch = "chimera";
const char *comprevision = "core";
const char *compnote = "";
const char *comptype = "Release";
const int compoptimized = 1;
const int compuncommitted = 0;
const char *compdate = "";
const char *comptime = "";
