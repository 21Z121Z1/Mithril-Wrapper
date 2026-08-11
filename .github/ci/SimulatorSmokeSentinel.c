/* iOS Simulator smoke-process result handoff.
 *
 * The smoke source is compiled with main renamed to mithril_smoke_main and
 * linked with this wrapper. A successful process writes an exact marker into
 * the app sandbox's tmp directory. The host-side workflow reads that file via
 * simctl get_app_container instead of treating simctl --console stdout as the
 * test verdict; CoreSimulator console forwarding is useful diagnostics, but
 * it is not a deterministic result transport.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mithril_smoke_main(void);

static int write_success_sentinel(void) {
    const char *tmpdir = getenv("TMPDIR");
    const char *marker = getenv("MITHRIL_SIMULATOR_SUCCESS_MARKER");
    if (!tmpdir || !*tmpdir || !marker || !*marker) {
        fprintf(stderr, "simulator sentinel: TMPDIR or success marker missing\n");
        return 86;
    }

    const char *name = "mithril-ci-result.txt";
    size_t path_len = strlen(tmpdir) + 1 + strlen(name) + 1;
    char *path = malloc(path_len);
    if (!path) {
        fprintf(stderr, "simulator sentinel: path allocation failed\n");
        return 87;
    }
    snprintf(path, path_len, "%s/%s", tmpdir, name);

    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "simulator sentinel: cannot open %s\n", path);
        free(path);
        return 88;
    }
    int ok = fprintf(file, "%s\n", marker) >= 0 && fclose(file) == 0;
    if (!ok) {
        fprintf(stderr, "simulator sentinel: write failed for %s\n", path);
        free(path);
        return 89;
    }

    free(path);
    return 0;
}

int main(void) {
    int rc = mithril_smoke_main();
    if (rc != 0) return rc;
    return write_success_sentinel();
}
