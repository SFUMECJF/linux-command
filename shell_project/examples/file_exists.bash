#! /bin/bash
# A script demoing some of the more advanced features in bash. It's important
# to remember that as much as this looks like a full programming language
# (because it is!), you can still get the same effect by typing in the code
# below one line at a time in your REPL.

if [ -f "$1" ]; then
  echo "$1 is a file!"
elif [ -d "$1" ]; then
  echo "$1 is a directory!"
else
  echo "$1 does not exist!"
fi