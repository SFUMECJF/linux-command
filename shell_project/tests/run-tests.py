#!/usr/bin/env python3

## General workflow of running a test:
# Read settings from given directory
# Perform substitutions on skeleton to obtain `in` script, perform substitutions
#    on run script to get command to run
# Run script with utcsh, capturing the output
# Write output to files
# Diff files
# Print any necessary output

import tempfile
import sys, os
import subprocess
import argparse
import json
import re
import textwrap
from enum import Enum
from subprocess import PIPE

# Const blocks
tmpdir_pat = re.compile(r"\$TMPDIR")
specdir_pat = re.compile(r"\$SRCDIR")
testid_pat = re.compile(r"\$TESTID")
utildir_pat = re.compile(r"\$UTILDIR")

# tmp path MUST be unique per-user or failed tests by user1 can cause tests by
# user2 to appear to fail.
TMPDIR = os.path.join(tempfile.gettempdir(), os.getlogin(), "utcsh")


class TestAction(Enum):
    NO_OP = 0
    RUN_ALL = 1
    RUN_SINGLE = 2
    DESCRIBE = 3
    GRADE = 4


# Used for color printing. These don't change over the lifetime of the program
# so it's safe to declare them globally like this. These nominally only work
# with ANSI terminals, but unless one of the alternate terminal protocols wins
# out, it's unlikely that this will change anytime soon.
color_codes = {
    "black": "\u001b[30m",
    "red": "\u001b[31m",
    "green": "\u001b[32m",
    "yellow": "\u001b[33m",
    "blue": "\u001b[34m",
    "magenta": "\u001b[35m",
    "cyan": "\u001b[36m",
    "white": "\u001b[37m",
    "reset": "\u001b[0m",
}


def print_color(string, color, end="\n"):
    """Print the given string in the given color"""
    print(color_codes[color] + string + color_codes["reset"], end=end)


def okay(message):
    print_color(message, "green")


def info(message):
    print_color("[INFO]: " + message, "blue")


def fatal_error(message):
    error(message)
    sys.exit(1)


def error(message):
    print_color("[ERR]: " + message, "red")


def warning(message, end=""):
    print_color("[WARN]: " + message, "yellow", end)


def warn_testsuite_incorrect():
    error(
        """\
The testsuite files may not be correct. This can be caused by changing the files
in the testsuite or by a bug in the test scripts.

I will continue running tests for you, but you should NOT rely on the output of
these tests!! Download a fresh copy of the project and replace the `tests/` 
directory. If you continue to see this message, ask an instructor for help."""
    )


def exists_path(path):
    """Check that a path exists before returning it."""
    if os.path.isfile(path):
        return path
    else:
        error("Could not find required file " + path)
        error("This is probably because you've altered the tests directory")
        raise FileNotFoundError("Required file " + path + " not found")


class TestRc:
    """Class that represents how a test should behave: either a numeric return
    code or a condition that simply says that no crash can occur"""

    def __init__(self, rcstring):
        """Initialize the TestResultCode from an rc string in a JSON file"""
        try:
            self.nocrash = False
            self.code = int(rcstring)
        except ValueError:
            self.nocrash = True
            self.code = -999999999


class TestInfo:
    """Class that represents the information about a test case"""

    def __init__(self, pdir, idn):
        self.err_f = exists_path(os.path.join(pdir, "err"))
        self.out_f = exists_path(os.path.join(pdir, "out"))
        self.skel_f = exists_path(os.path.join(pdir, "skel"))
        self.run_f = exists_path(os.path.join(pdir, "run"))
        self.pre_f = None
        self.post_f = None
        if os.path.isfile(os.path.join(pdir, "pre")):
            self.pre_f = os.path.join(pdir, "pre")
        if os.path.isfile(os.path.join(pdir, "post")):
            self.post_f = os.path.join(pdir, "post")

        # Does NOT have to exist yet: we will generate it by filling out template
        # fields in the skel_f
        self.in_f = os.path.join(pdir, "in")
        self.idn = idn

        # Read other information from the info.json file
        with open(os.path.join(pdir, "info")) as json_f:
            json_obj = json.load(json_f)
        self.name = json_obj["name"]
        self.desc = textwrap.fill(json_obj["description"], 78)
        self.rc = TestRc(json_obj["rc"])
        self.pointval = int(json_obj["pointval"])
        self.srcdir = pdir


class RuntimeTestParams:
    """Represents a set of parameters passed in on the command line. Controls
    things like binary path, input/output/temporary paths, whether to force
    output generation, etc. etc."""

    def __init__(self, parsed_args):
        self.verbose = parsed_args.verbose
        self.cont_on_err = parsed_args.keep_going
        self.skip_init = parsed_args.skip_init
        self.outpath = parsed_args.out
        self.inpath = "tests/test-specs"
        self.utildir = "tests/test-utils"
        if parsed_args.compute_score:
            self.action = TestAction.GRADE
        elif parsed_args.describe is not None:
            self.action = TestAction.DESCRIBE
            self.tid = parsed_args.describe
        elif parsed_args.run_one is not None:
            self.action = TestAction.RUN_SINGLE
            self.tid = parsed_args.run_one
        else:
            self.action = TestAction.RUN_ALL


def sub_special_vars(string, runparams, testinfo):
    """Replace $TMPDIR and $SRCDIR in the given string with their runtime values"""
    string = re.sub(tmpdir_pat, TMPDIR, string)
    string = re.sub(specdir_pat, testinfo.srcdir, string)
    string = re.sub(testid_pat, str(testinfo.idn), string)
    string = re.sub(utildir_pat, runparams.utildir, string)
    return string


def load_nnmap(rtparams):
    """Get the name-to-number mapping for the tests by reading the mapping file"""
    nnmapfile = os.path.join(rtparams.inpath, "name_to_number.txt")
    with open(nnmapfile, "r") as inf:
        lineparts = [line.split() for line in inf.readlines()]
        return [(int(parts[0]), parts[1]) for parts in lineparts]


def get_test_spec(rtparams, target_idn):
    """Get the test spec for the given ID, if it exists"""

    # Is reparsing the mapping file for every test slow? Yes. Do I care? No.
    nnmap = load_nnmap(rtparams)

    for (idn, testdir) in nnmap:
        # print(idn, testdir)   # Debugging only
        if idn == target_idn:
            return TestInfo(os.path.join(rtparams.inpath, testdir), idn)

    nnmapfile = os.path.join(rtparams.inpath, "name_to_number.txt")
    print(f"Did not find a test with ID {target_idn} in {nnmapfile}")
    return None


def get_test_specs(rtparams):
    """Using run time parameters, obtain a set of tests"""
    nnmap = load_nnmap(rtparams)
    max_testid = max(*(t[0] for t in nnmap))

    return [get_test_spec(rtparams, x) for x in range(1, max_testid + 1)]


def run_bash_file(fname, runparams, testinfo):
    """Run a bash file, returning its stdout/stderr/rc in a TestResult"""

    with open(fname, "r") as inf:
        contents = inf.read()

    contents = sub_special_vars(contents, runparams, testinfo)
    outpath = os.path.join(TMPDIR, "test-pre")

    with open(outpath, "w") as outf:
        outf.write(contents)

    if runparams.verbose:
        info("Running command: " + contents.strip())

    return subprocess.run(
        ["bash", outpath],
        stdout=PIPE,
        stderr=PIPE,
        universal_newlines=True,
    )


def gen_inp_file(testinfo, runparams):
    """Replace special variables in the skeleton file to create the script for UTCSH"""
    with open(testinfo.skel_f, "r") as inf:
        contents = inf.read()

    contents = sub_special_vars(contents, runparams, testinfo)

    with open(testinfo.in_f, "w") as outf:
        outf.write(contents)


def try_make_path(path):
    """Tries to make a path, printing diagnostic messages"""
    if not os.path.isdir(path):
        try:
            os.makedirs(path)
        except Exception as e:
            warning("Error when trying to make the directory {path}")
            print("Error was {e}")

    if not os.path.isdir(path):
        error(f"Could not create directory {path}. Bailing out.")
        return False

    return True


def files_differ(path1, path2):
    with open(path1, "r") as in1, open(path2, "r") as in2:
        contents1 = in1.read()
        contents2 = in2.read()
        return contents1 != contents2


def print_test_error_message(tst_path, ref_path):
    print(f"  The correct results can be found in file: {ref_path}")
    print(f"  Your utcsh results can be found in file: {tst_path}")
    print(f"  compare the two using diff, cmp, or related tools to debug, e.g.:")
    print(f"  prompt> diff {ref_path} {tst_path}")


def check_abnormal_retcodes(code):
    """Check abnormal return codes from Python. Returns True if exit code was abnormal."""
    if code == -11:
        info("UTCSH appears to have died from a segmentation fault.")
        return True
    elif code == -9:
        info("UTCSH died because it was killed by SIGKILL.")
        return True
    elif code < 0:
        info(
            f"UTCSH died from signal {-code}. Run `man 7 signal` to get a list of signals."
        )
        return True
    else:
        return False


def run_test(testinfo, runparams):
    """Execute a test."""
    outdir = os.path.join(runparams.outpath, str(testinfo.idn))
    if not try_make_path(outdir):
        error(f"Could not make output directory {outdir}")
        return False
    gen_inp_file(testinfo, runparams)  # Generate the in_f file from skel_f

    if testinfo.pre_f is not None and not runparams.skip_init:
        # These can fail, so don't check the returncode on them
        if runparams.verbose:
            info("Running setup command")
        run_bash_file(testinfo.pre_f, runparams, testinfo)

    with open(testinfo.run_f, "r") as inf:
        cmd = inf.read().split()
        # Do substution after splitting to avoid having a dirname with space
        # incorrectly cause an extra split
        for j in range(1, len(cmd)):
            cmd[j] = sub_special_vars(cmd[j], runparams, testinfo)

    if runparams.verbose:
        info("Running command: " + " ".join(cmd))

    try:
        cmd_res = subprocess.run(
            cmd, stdout=PIPE, stderr=PIPE, universal_newlines=True, timeout=20
        )
    except subprocess.TimeoutExpired as e:
        error(f"Test {testinfo.idn} timed out.")
        return False

    outfile = os.path.join(outdir, "out")
    errfile = os.path.join(outdir, "err")
    retcode = cmd_res.returncode
    with open(outfile, "w") as outf:
        outf.write(cmd_res.stdout)
    with open(errfile, "w") as errf:
        errf.write(cmd_res.stderr)

    success = True

    # Compare results and notify the user if they differ
    if testinfo.rc.nocrash:
        if check_abnormal_retcodes(retcode):
            error(f"UTCSH exited with a signal when it should not have.")
            success = False
    else:
        if check_abnormal_retcodes(retcode):
            error(
                f"UTCSH should have exited with {testinfo.rc.code} but it exited due to a signal."
            )
            success = False
        elif retcode != testinfo.rc.code:
            error(
                f"UTCSH should have exited with {testinfo.rc.code} but it exited with {retcode}"
            )
            success = False

    # If the test is labeled "nocrash", it implicitly does not check output
    # since the only requirement is that the test does not crash
    if testinfo.rc.nocrash:
        return success

    if files_differ(outfile, testinfo.out_f):
        error("The stdout from UTCSH differed from the expected output")
        print_test_error_message(outfile, testinfo.out_f)
        success = False

    if files_differ(errfile, testinfo.err_f):
        error("The stderr from UTCSH differed from the expected output")
        print_test_error_message(errfile, testinfo.err_f)
        success = False

    if testinfo.post_f is not None:
        if runparams.verbose:
            info("Running cleanup command")
        run_bash_file(testinfo.post_f, runparams, testinfo)

    return success


def replace_whitespace(string):
    output = ""
    for i in range(len(string)):
        if string[i] == "\t":
            output += "\t⇥"
        elif string[i] == "\n":
            output += "⏎\n"
        else:
            output += string[i]
    return output


def describe_test(testinfo, runparams):
    """Describe a test in a user-accessible way that doesn't involve holding
    the contents of 7 files in your head simultaneously."""
    gen_inp_file(testinfo, runparams)  # Generate the in_f file from skel_f

    if runparams.verbose:
        info(f"Describing the contents of test {testinfo.idn}")
        info(f"All colored terminal output is from this test script.")
        info(f"All other output is information from the testing files.")
        info(
            f"""To help you visualize whitespace, newlines have been replaced with '⏎'"""
        )
    else:
        info(f"For more details on this output, run with --verbose")

    print_color("Test ID: ", "green", end="")
    print(str(testinfo.idn))
    print_color("Test Name: ", "green", end="")
    print(str(testinfo.name))
    print_color("Description: ", "green", end="")
    print(testinfo.desc)

    if testinfo.pre_f is not None:
        okay(
            f"The test has a setup step: it starts by running the following command(s):"
        )
        with open(testinfo.pre_f, "r") as inf:
            print(replace_whitespace(inf.read()))

    okay("The test runs the following UTCSH script:")
    with open(testinfo.in_f, "r") as inf:
        print(replace_whitespace(inf.read()))

    if testinfo.rc.nocrash:
        okay(f"The test should not crash or segfault")
    else:
        okay(f"The test is supposed to exit {testinfo.rc.code}")
    okay(f"The standard output of the test is supposed to be:")

    with open(testinfo.out_f, "r") as inf:
        print(replace_whitespace(inf.read()))

    okay(f"The standard error of the test is supposed to be:")
    with open(testinfo.err_f, "r") as inf:
        print(replace_whitespace(inf.read()))

    okay("You can run the test yourself with the following command:")
    with open(testinfo.run_f, "r") as inf:
        cmd = inf.read()
        cmd = sub_special_vars(cmd, runparams, testinfo)
        print(cmd)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-v", "--verbose", help="Print extra information", action="store_true"
    )
    alt_actions = parser.add_mutually_exclusive_group()
    alt_actions.add_argument(
        "-t", "--run-one", type=int, help="Only run test n", metavar="n"
    )
    alt_actions.add_argument(
        "--compute-score",
        help="Compute score on assignment. Cannot be used with -t. Implies -k.",
        action="store_true",
    )
    alt_actions.add_argument(
        "-d",
        "--describe",
        type=int,
        metavar="n",
        help="Describe what test n does, but do not run it.",
    )
    parser.add_argument(
        "-k",
        "--keep-going",
        help="Continue running tests if an error occurs",
        action="store_true",
    )
    parser.add_argument(
        "-s", "--skip-init", help="Skip pre-test initialization", action="store_true"
    )
    parser.add_argument(
        "-o",
        "--out",
        help="Path to place output of tests",
        metavar="path",
        default="./tests-out/",
    )

    args = parser.parse_args()
    rtparams = RuntimeTestParams(args)

    if not try_make_path(args.out):
        sys.exit(1)
    if not try_make_path(TMPDIR):
        sys.exit(1)
    if not os.path.exists("./utcsh"):
        fatal_error("The file ./utcsh does not exist. Try running `make` first.")

    if rtparams.action == TestAction.RUN_ALL:
        testspecs = get_test_specs(rtparams)

        failed_list = []
        for testspec in testspecs:
            print_color("==================================", "magenta")
            if rtparams.verbose:
                okay(f"Running Test {testspec.idn}: {testspec.name}")
                okay(f"Description: {testspec.desc}")
                print("")
            success = run_test(testspec, rtparams)
            if success:
                okay(f"[PASS]: Test {testspec.idn}")
            else:
                failed_list.append(testspec.idn)
                print_color(f"[FAIL]: Test {testspec.idn}", "red")
                if rtparams.cont_on_err:
                    continue
                else:
                    break
        if not failed_list:
            okay("Passed all tests! Congratulations!")
        else:
            print("")
            error(f"Failed tests {failed_list}")

    elif rtparams.action == TestAction.RUN_SINGLE:
        testspec = get_test_spec(rtparams, rtparams.tid)
        if rtparams.verbose:
            okay(f"Running Test {rtparams.tid}: {testspec.name}")
            okay(f"Description: {testspec.desc}")

        success = run_test(testspec, rtparams)
        if success:
            okay(f"[PASS]: Test {testspec.idn}")
    elif rtparams.action == TestAction.DESCRIBE:
        testspec = get_test_spec(rtparams, rtparams.tid)
        describe_test(testspec, rtparams)
    elif rtparams.action == TestAction.GRADE:
        testspecs = get_test_specs(rtparams)
        successes = set()
        print("Checking tests: ", end="")
        for testspec in testspecs:
            print(f"{testspec.idn} ", end="")
            sys.stdout.flush()
            success = run_test(testspec, rtparams)
            if success:
                successes.add(testspec)

        # Prepare for two types of output: printing to terminal and recording
        # results into a JSON file for easy grading elsewhere.
        print("")
        print("============= Results ==============")
        results = []
        grade = 0
        maxpts = 0

        for testspec in testspecs:
            pts = testspec.pointval
            maxpts += pts
            pad = "" if testspec.idn >= 10 else " "
            if testspec in successes:
                okay(f"Test {pad}{testspec.idn}    {pts}/{pts}")
                grade += pts
                results.append({"id": testspec.idn, "pts": pts, "max": pts})
            else:
                print_color(f"Test {pad}{testspec.idn}    0/{pts}", "red")
                results.append({"id": testspec.idn, "pts": 0, "max": pts})
        print("==================================")
        print(f"Total Score: \t {grade}/{maxpts}")

        with open(".utcsh.grade.json", "w") as outf:
            json.dump(results, outf)
    else:
        fatal_error("Unreachable clause executed.")


if __name__ == "__main__":
    main()
