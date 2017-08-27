
/* Copyright (c) 2010-2017 the corto developers
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

#include <driver/tool/test/test.h>
#include <corto/argparse/argparse.h>

#define GREEN   "\033[1;32m"
#define NORMAL  "\033[0;49m"

static char *errfmt = "[ %k %f:%l: %c: %m ]";

void cortotool_testHelp(void) {
    printf("Usage: corto test\n");
    printf("       corto test <project>\n");
    printf("\n");
    printf("Options:\n");
    printf("   --build        Just build tests, do not run test.\n");
    printf("   --rebuild      Just rebuild tests, do not run tests. Same as combining --build and --clean\n");
    printf("   --clean        Just clean tests, do not run tests.\n");
    printf("   --verbose      Verbose output. Sets CORTO_VERBOSITY to 'TRACE'\n");
    printf("\n");
    printf("Test your Corto project. Returns -1 if tests fails.\n");
    printf("\n");
}


int cortomain(int argc, char *argv[]) {
    corto_string projectArg = NULL;
    corto_int8 ret, sig, err = 0;
    corto_ll verbose, project, build, rebuild, clean, tool, testcase;

    CORTO_UNUSED(argc);
    
    corto_errfmt(errfmt);

    /* Generate rakefile */
    if (!corto_fileTest("rakefile")) {
        if (corto_load("driver/tool/rakefile", 0, NULL)) {
            goto error;
        }
    }

    corto_argdata *data = corto_argparse(
      argv,
      (corto_argdata[]){
        {"$0", NULL, NULL}, /* Ignore 'test' */
        {"--verbose", &verbose, NULL},
        {"--build", &build, NULL},
        {"--rebuild", &rebuild, NULL},
        {"--clean", &clean, NULL},
        {"--tool", NULL, &tool},
        {"--testcase", NULL, &testcase},
        {"-t", NULL, &testcase},
        {"*", &project, NULL},
        {NULL}
      }
    );

    if (!data) {
        corto_error("test: %s", corto_lasterr());
        goto error;
    }

    if (project) {
        projectArg = corto_ll_get(project, 0);
    }

    if (tool) {
        char *toolstr = corto_ll_get(tool, 0);
        setenv("CORTO_TEST_TOOL", toolstr, TRUE);
        setenv("CI", "TRUE", TRUE);
    }

    corto_int32 i = 0;
    do {
        if (projectArg) {
            if (corto_chdir(projectArg)) {
                corto_error("can't change to directory '%s'", projectArg);
                goto error;
            }
        }

        corto_pid pid;
        if (testcase) {
            corto_id testcaseStr;
            sprintf(testcaseStr, "testcase=%s", (char*)corto_ll_get(testcase, 0));

            /* Set environment variable, so test framework knows to log when
             * the test was executed. */
            corto_setenv("CORTO_TEST_BY_ID", "TRUE");

            pid = corto_procrun("rake", (char*[]){
                "rake",
                "test",
                verbose ? "verbose=true" : "verbose=false",
                build ? "build=true" : "build=false",
                rebuild ? "rebuild=true" : "rebuild=false",
                clean ? "clean=true" : "clean=false",
                testcaseStr,
                "silent=true",
                NULL});
        } else {
            pid = corto_procrun("rake", (char*[]){
              "rake",
              "test",
              verbose ? "verbose=true" : "verbose=false",
              build ? "build=true" : "build=false",
              rebuild ? "rebuild=true" : "rebuild=false",
              clean ? "clean=true" : "clean=false",
              "silent=true",
              NULL});
        }
        if ((sig = corto_procwait(pid, &ret) || ret)) {
            if ((sig > 0) && !(build || rebuild || clean)) {
                corto_error("Aww, tests failed.");
            }
            err = 1;
        }

        if (err) {
            goto error;
        } else if (!(build || rebuild || clean)) {
            printf("%sYay, all green :-)%s\n", GREEN, NORMAL);
        }

        i ++;
    } while (project && (projectArg = corto_ll_get(project, i)));

    corto_argclean(data);

    return 0;
error:
    return -1;
}

