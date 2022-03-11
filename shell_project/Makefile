CC = gcc
CFLAGS = -Wall -Wextra

SHELLNAME = utcsh
CFLAGS_REL = -O3 -g
CFLAGS_DEB = -Og -g3 -fno-omit-frame-pointer
CFLAGS_SAN = -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer -g

TESTDIR=tests
TESTSCRIPT=$(TESTDIR)/run-tests.py

SRCS = utcsh.c util.c
HEADERS = util.h
FILES = $(SRCS) $(HEADERS)

$(SHELLNAME): $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_REL) $(SRCS) -o $(SHELLNAME)

debug: $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_DEB) $(SRCS) -o $(SHELLNAME)

asan: $(FILES)
	$(CC) $(CFLAGS) $(CFLAGS_SAN) $(SRCS) -o $(SHELLNAME)

##################################
# Settings for fib and utilities #
##################################

# We place these here instead of at the top because make's implicit rule
# (when run just as `make` instead of `make utcsh` or `make fib` is the first
# rule in the file, and we don't really want that to be fib or argprinter

fib: fib.c
	$(CC) $(CFLAGS) -o fib $<

argprinter: argprinter.c
	$(CC) $(CFLAGS) -o argprinter $<

################################
# Prepare your work for upload #
################################

FILENAME = turnin.tar

turnin.tar: clean
	tar cvf $(FILENAME) `find . -type f | grep -v \.git | grep -v \.tar$$ | grep -v \.tar\.gz$$ | grep -v \.swp$$ | grep -v ~$$`
	gzip $(FILENAME)

turnin: turnin.tar
	@echo "================="
	@echo "Created $(FILENAME).gz for submission.  Please upload to Canvas."
	@echo "Before uploading, please verify:"
	@echo "     - Your README is correctly filled out."
	@echo "     - Your pair programming log is in the project directory."
	@echo "If either of those items are not done, please update your submission and run the make turnin command again."
	@ls -al $(FILENAME).gz

#########################
# Various utility rules #
#########################

clean:
	rm -f $(SHELLNAME) *.o *~
	rm -f .utcsh.grade.json readme.html shellspec.html
	rm -f fib argprinter
	rm -rf tests-out

# Checks that the test scripts have valid executable permissions and fix them if not.
validtestperms: $(TESTSCRIPT)
	@test -x $(TESTSCRIPT)  && { echo "Testscript permissions appear correct!"; } || \
{ echo "Testscript does not have executable permissions. Please run \`chmod u+x $(TESTSCRIPT)\` and try again.";\
echo "Also verify that *all* files in \`tests/test-utils\` have executable permissions before continuing,";\
echo "or future tests could silently break."; exit 1; }

fixtestscriptperms:
	@chmod u+x $(TESTSCRIPT)
	@chmod u+x tests/test-utils/*
	@chmod u+x tests/test-utils/p2a-test/*

.PHONY: clean fixtestscriptperms

##############
# Test Cases #
##############

check: $(SHELLNAME) validtestperms
	@echo "Running all tests..."
	$(TESTSCRIPT) -kv

testcase: $(SHELLNAME) validtestperms
	$(TESTSCRIPT) -vt $(id)

describe: $(SHELLNAME)
	$(TESTSCRIPT) -d $(id)

grade: $(SHELLNAME) validtestperms
	$(TESTSCRIPT) --compute-score

#####################################
# Rules intended for instructor use #
#####################################

# Rules for transforming the markdown files into HTML usable on cs.utexas.edu
# Requires: 
# - grip binary (for generating base HTML)
# - GNU sed (don't try this on MacOS)
# Process for how this works can be found in Instructor README
%.html: %.md
	grip $< --export --no-inline $@.body
	sed -Ei 's_<pre> *<code>_<pre data-role="codeBlock" data-info="" class="language-">_g' $@.body
	sed -Ei 's_</code> *</pre>_</pre>_g' $@.body
	sed -Ei 's_<code>_<tt>_g' $@.body
	sed -Ei 's_</code>_</tt>_g' $@.body
	# Extract just the stuff inside the <body> tags: we want to use our own
	# headers and stylesheets. Taken from https://stackoverflow.com/a/45280316
	sed -ni '/<body/,/<\/body>/{//!p}' $@.body
	cat html-stubs/preamble.html $@.body html-stubs/footer.html > $@
	rm $@.body

readme.html: README.html
	mv $< $@

webpages: readme.html shellspec.html

HANDOUT_FILES = examples/ tests/\
 argprinter.c fib.c \
 Makefile README.md README.shell \
 shell_design.txt\
 utcsh.c util.c util.h
# shellspec.md   # Not included at the moment because it's so new.

handout: $(HANDOUT_FILES)
	tar czvf shell_project.tar.gz $^
