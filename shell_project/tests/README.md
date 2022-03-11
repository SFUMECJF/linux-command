UTCSH Tests
===========

**Do not look in this directory unless you intend to modify or add tests to this 
testsuite.**

If you're failing a test and want to figure out what command it ran or what
the input was, use `make describe` as documented in the primary project spec.
Copying contents out of this directory may give you the wrong commands/input!

## Test Specification

A test specification is a set of files in a single directory. They give 
information about the test contents, how to run it, and the expected outputs,
as well as code for setup and teardown of tests (if needed).

The files needed to describe a test number are:
- `info`: A JSON file containing 4 fields:
  - `name`: A short name for the test
  - `description`: A short description for the test
  - `rc`: The return code expected from UTCSH. May be `0`, `1`, or `"nocrash"`,
          where the last indicates that any non-signal return code is accepted.
  - `pointval`: The number of points the test is worth
- `out`: The standard output expected from the test
- `err`: The standard error expected from the test
- `run`: How to run the test (which arguments it needs, etc.)
- `pre` (optional): Code to run before the test, to set something up
- `post` (optional): Code to run after the test, to clean something up

Note that the test framework assumes the project toplevel (where `utcsh` 
is located) is the pwd.

### Test Spec Variables

Because UTCSH does not support variable substitution, we need a way to provide
per-user paths for certain operations, so that interrupted tests do not leave
behind artifacts that stop other users from running tests (e.g. if all users
try to write `/tmp/utcsh/18/test.txt` for test 18, then a single user creating
and failing to delete that file can stop any user on the system from running 
test 18).

To solve this, the following strings are treated specially in the test suite:
they are replaced with the appropriate value before the command is run. Note
that this means that test files cannot have these as literal names, since they
will be substituted out, and there is no escape system. However, naming a file 
something like `$SRCDIR` is a terrible idea to begin with.

  - `$TMPDIR`: A test-specific temporary directory.
  - `$SRCDIR`: The source directory of the test.
  - `$TESTID`: The numeric ID of the test
  - `$UTILDIR`: The path to the test utility directory (`test-utils`)

Note that these variables are replaced in the `skel` file to generate the `in`
file, which the shell actually uses to run. Therefore, it is incorrect to 
run commands directly from the `skel` file. Instead, use the `make describe`
functionality to generate the files and substitutions needed.

# Test IDs + Point Values

Tests used to be identified by a test ID number that was hardcoded into the test
itself. This ended up giving me an aneurysm when adding more tests, since all
files (including scripts) needed to be updated when adding tests and missing
a single file could cause things to crash.

Tests are now identified by the name of their directory within `test-specs`.
They are mapped to a test ID by the file `name-to-number.txt`. Each line of the
file contains the test number and the name of the directory for that test.

### Notes on Tests

Tests are executed by passing them to `subprocess.run` in Python. Therefore,
even in the `pre` and `post` scripts, standard shell features like redirection
and envar substitution are not available. If you need these features, write a
bash script to do it, place it in `test-utils`, and then call that script from
the test.
