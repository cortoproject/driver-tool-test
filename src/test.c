
/* Copyright (c) 2010-2018 the corto developers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <driver.tool.test>

void cortotool_testHelp(void) {
    printf("Usage: corto test\n");
    printf("       corto test <project>\n");
    printf("\n");
    printf("Options:\n");
    printf("   --build        Just build tests, do not run test.\n");
    printf("   --rebuild      Just rebuild tests, do not run tests. Same as combining --build and --clean\n");
    printf("   --clean        Just clean tests, do not run tests.\n");
    printf("\n");
    printf("Test your Corto project. Returns -1 if tests fails.\n");
    printf("\n");
}

static
int16_t test_crawl(
    ut_ll projects,
    char *testcase,
    bool nestedFoldersAreTests,
    char *action)
{
    corto_int32 i = 0;
    char *project = ".";

    if (projects) {
        project = ut_ll_get(projects, 0);
    }

    /* If this is a child corto process, we are in a corto project that has
     * a test project that does not contain a project.json. It is possible
     * that the test project contains nested directories which do contain
     * test suites, so invoke bake again to locate those projects. Only this
     * time, every project found by bake should be treated as a testsuite. */
    char *kind = "child";
    if (nestedFoldersAreTests) {
        kind = "test-child"; /* Flags that project is a testsuite */
        project = "test"; /* Locate test projects in test directory */
    }

    do {
        int sig;
        int8_t ret;
        ut_proc pid;
        if (testcase) {
            /* Set environment variable, so test framework knows to log when
             * the test was executed. */
            ut_setenv("CORTO_TEST_BY_ID", "TRUE");

            pid = ut_proc_run("bake", (const char*[]) {
                "bake",
                "foreach",
                project,
                strarg("corto test --%s --%s -t %s", kind, action, testcase),
                NULL
            });
        } else {
            pid = ut_proc_run("bake", (const char*[]) {
                "bake",
                "foreach",
                project,
                strarg("corto test --%s --%s", kind, action),
                NULL
            });
        }

        if ((sig = ut_proc_wait(pid, &ret) || ret)) {
            if ((sig > 0) && strcmp(action, "test")) {
                ut_error("Aww, tests failed.");
            }
            goto error;
        }

        if (!strcmp(action, "test")) {
            ut_log("#[green]Yay, all green :-)#[normal]\n");
        }

        i ++;
    } while (projects && (project = ut_ll_get(projects, i)));

    return 0;
error:
    return -1;
}

static
int16_t test_run(
    char *path,
    char *testcase,
    char *action)
{
    /* If this is a child process invoked by bake, run tests if the projects
     * contains a test folder. */
    if (!path || ut_file_test(path)) {
        if (path) {
            if (ut_chdir(path)) {
                ut_throw("cannot enter test directory '%s'", path);
                goto error;
            }
        }

        /* Build project */
        int8_t ret; int sig;
        char *bakeCmd = strcmp(action, "test") ? action : "build";
        if ((sig = ut_proc_cmd(strarg("bake %s", bakeCmd), &ret) || ret)) {
            ut_throw("failed to build testcase");
            goto error;
        }

        if (!strcmp(action, "test")) {
            char *testlib = ut_envparse(
                "bin/%s-$BAKE_CONFIG/libtest" UT_OS_LIB_EXT, UT_PLATFORM_STRING);
            if (testcase) {
                if (ut_use(testlib, 2, (char*[]){
                    testlib,
                    testcase,
                    NULL}))
                {
                    ut_throw(NULL);
                    goto error;
                }
            } else {
                if (ut_use(testlib, 1, (char*[]){
                    testlib,
                    NULL}))
                {
                    ut_throw(NULL);
                    goto error;
                }
            }
        }
    }

    return 0;
error:
    return -1;
}

int cortomain(
    int argc,
    char *argv[])
{
    ut_ll project, build, rebuild, clean, tool, testcase, child, test_child;

    CORTO_UNUSED(argc);

    corto_argdata *data = corto_argparse(
      argv,
      (corto_argdata[]){
        {"$0", NULL, NULL}, /* Ignore 'test' */
        {"--build", &build, NULL},
        {"--rebuild", &rebuild, NULL},
        {"--clean", &clean, NULL},
        {"--test", NULL, NULL}, /* This is the default action */
        {"--tool", NULL, &tool},
        {"--testcase", NULL, &testcase},
        {"--child", &child, NULL},
        {"--test-child", &test_child, NULL},
        {"-t", NULL, &testcase},
        {"*", &project, NULL},
        {NULL}
      }
    );

    if (!data) {
        ut_throw(NULL);
        goto error;
    }

    /* Instruct the test frameworks to run tests with a tool like valgrind to
     * do extra checking */
    if (tool) {
        char *toolstr = ut_ll_get(tool, 0);
        setenv("CORTO_TEST_TOOL", toolstr, TRUE);
        setenv("CI", "TRUE", TRUE);
    }

    /* Specify the action that needs to be performed for every testsuite */
    char *action = "test";
    if (clean) action = "clean";
    if (build) action = "build";
    if (rebuild) action = "rebuild";

    /* Was a testcase explicitly specified */
    char *tc = testcase ? ut_ll_get(testcase, 0) : NULL;

    /* If current folder contains a test directory without project.json, crawl
     * for nested projects, which will be treated as test suites. */
    bool nestedFoldersAreTests =
        ut_file_test("test") && !ut_file_test("test/project.json");

    /* If 'corto test' is called from command line by user, use bake to crawl
     * for corto projects. For each project, run 'test' again with --child. */
    if (!test_child && (!child || nestedFoldersAreTests)) {
        if (nestedFoldersAreTests) {
            ut_trace("crawl '%s/test' for test suites", ut_cwd());
        } else {
            ut_trace("crawl '%s' for projects", ut_cwd());
        }
        if (test_crawl(
            project, tc, nestedFoldersAreTests, action))
        {
            goto error;
        }
    } else {
        if (test_child) {
            ut_trace("%s suite '%s'", action, ut_cwd());

            /* If --test-child is specified, project is a testsuite */
            if (test_run(".", tc, action)) {
                goto error;
            }
        } else {
            ut_trace("%s suite '%s/test'", action, ut_cwd());

            /* If --child is specified, project may contain a testsuite */
            if (ut_file_test("test")) {
                if (test_run("test", tc, action)) {
                    goto error;
                }
            }
        }
    }

    corto_argclean(data);

    return 0;
error:
    return -1;
}
