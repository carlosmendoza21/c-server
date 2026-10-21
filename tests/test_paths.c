// Unit tests for the path-handling functions in requests.c.
// requests.c is included directly so its static functions can be called.
#include "../src/requests.c"

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...) do {                          \
    checks++;                                          \
    if (!(cond)) {                                     \
        failures++;                                    \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);    \
        printf(__VA_ARGS__);                           \
        printf("\n");                                  \
    }                                                  \
} while (0)

// ---------- has_dot_dot_segment ----------

static void test_has_dot_dot_segment(void) {
    struct { const char *path; int want; } cases[] = {
        { "public/../README.md", 1 },   // .. in the middle
        { "public/css/..",       1 },   // .. at the end
        { "public/css/../../x",  1 },   // several
        { "../x",                1 },   // .. at the very start
        { "public/notes..txt",   0 },   // two dots inside a name
        { "public/..hidden",     0 },   // starts with .. but is longer
        { "public/.../x",        0 },   // three dots is an ordinary name
        { "public/index.html",   0 },
        { "",                    0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = has_dot_dot_segment(cases[i].path);
        CHECK(got == cases[i].want, "has_dot_dot_segment(\"%s\") = %d, expected %d",
              cases[i].path, got, cases[i].want);
    }
}

// ---------- build_file_path ----------

static void expect_path(const char *url, enum path_result want, const char *want_path) {
    char out[512];
    enum path_result got = build_file_path(url, out, sizeof(out));
    CHECK(got == want, "build_file_path(\"%s\") returned %d, expected %d",
          url, (int)got, (int)want);
    if (got == PATH_SUCCESS && want == PATH_SUCCESS) {
        CHECK(strcmp(out, want_path) == 0, "build_file_path(\"%s\") gave \"%s\", expected \"%s\"",
              url, out, want_path);
    }
}

static void test_build_file_path(void) {
    // Mapping
    expect_path("/",                 PATH_SUCCESS, "public/index.html");
    expect_path("/about.html",       PATH_SUCCESS, "public/about.html");
    expect_path("/css/style.css",    PATH_SUCCESS, "public/css/style.css");
    expect_path("/css/",             PATH_SUCCESS, "public/css/index.html");

    // Query strings are dropped
    expect_path("/index.html?v=2",   PATH_SUCCESS, "public/index.html");
    expect_path("/?v=2",             PATH_SUCCESS, "public/index.html");
    expect_path("/index.html?q=/../", PATH_SUCCESS, "public/index.html");

    // Names containing dots are allowed
    expect_path("/notes..txt",       PATH_SUCCESS, "public/notes..txt");

    // Rejections
    expect_path("../README.md",      PATH_BAD_REQUEST, NULL);
    expect_path("README.md",         PATH_BAD_REQUEST, NULL);
    expect_path("/../README.md",     PATH_FORBIDDEN, NULL);
    expect_path("/css/../../x",      PATH_FORBIDDEN, NULL);
    expect_path("/css/..",           PATH_FORBIDDEN, NULL);

    // Buffer limits
    char small[16];
    CHECK(build_file_path("/a-very-long-path.html", small, sizeof(small)) == PATH_TOO_LONG,
          "long path in 16-byte buffer should be PATH_TOO_LONG");
    char fits_until_index[17];   // "public/" fits, "public/index.html" (17 + '\0') doesn't
    CHECK(build_file_path("/", fits_until_index, sizeof(fits_until_index)) == PATH_TOO_LONG,
          "\"/\" in 17-byte buffer should be PATH_TOO_LONG once index.html is appended");
}

// ---------- is_inside_root ----------

static void set_root(const char *root) {
    snprintf(resolved_root, sizeof(resolved_root), "%s", root);
    resolved_root_len = strlen(resolved_root);
}

static void test_is_inside_root(void) {
    set_root("/srv/public");
    struct { const char *path; int want; } cases[] = {
        { "/srv/public",                 1 },   // the root itself
        { "/srv/public/index.html",      1 },
        { "/srv/public/css/style.css",   1 },
        { "/srv/public_secret/x.txt",    0 },   // sibling that shares the prefix
        { "/srv/publicity",              0 },
        { "/srv/publi",                  0 },   // shorter than the root
        { "/srv",                        0 },
        { "/etc/passwd",                 0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = is_inside_root(cases[i].path);
        CHECK(got == cases[i].want, "is_inside_root(\"%s\") = %d, expected %d (root %s)",
              cases[i].path, got, cases[i].want, resolved_root);
    }
}

int main(void) {
    test_has_dot_dot_segment();
    test_build_file_path();
    test_is_inside_root();

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
