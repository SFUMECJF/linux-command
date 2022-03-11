#!/bin/bash

ebb_dir=$(realpath tests/test-utils/p2a-ebb)
ebb_path=$ebb_dir/libevilboombox.so
inp_path=tests/test-specs/evilboombox/in

ebb_sys_fn=.ebb_syscall_fired
ebb_alloc_fn=.ebb_alloc_fired

function die(){
    kill -"$1" $$
}

pushd $ebb_dir || die 6
make
popd || die 6

## If UTCSH dies with a signal, bash will report an exit number of 128 + signum
# We need to make sure the script also dies with that signal, so that the test
# driver can report that to the user. Exiting 1 would be incorrect because this
# would appear to be a correct exit to test that only requires no-signal exits
function mirror_signal_exit() {
    if [ "$1" -gt 128 ]; then
        signum=$(($1 - 128))
        echo "Got signal $signum while running $2 tests"
        echo "Signal occurred on the $3 function call"
        die $signum
    fi
}

function cleanup(){
    rm -f $ebb_sys_fn $ebb_alloc_fn tests/test-specs/evilboombox/$ebb_sys_fn tests/test-specs/evilboombox/$ebb_alloc_fn
    exit
}
trap cleanup EXIT SIGINT SIGTERM

export LD_PRELOAD="$ebb_path:$LD_PRELOAD"

# Run the syscall tests, incrementing the failing function by one each time,
# until we get a failure or until we no longer fire a failure
touch $ebb_sys_fn
ebb_ctr=0
while [ -f $ebb_sys_fn ]; do
    rm -f $ebb_sys_fn
    EBB_SYSCALL_CTR=$ebb_ctr ./utcsh $inp_path
    exitnum=$?
    mirror_signal_exit $exitnum syscall $ebb_ctr
    ebb_ctr=$((ebb_ctr + 1))
done

# Do the same for allocation tests
touch $ebb_alloc_fn
ebb_ctr=0
while [ -f $ebb_alloc_fn ]; do
    rm -f $ebb_alloc_fn
    EBB_ALLOC_CTR=$ebb_ctr ./utcsh $inp_path
    exitnum=$?
    mirror_signal_exit $exitnum allocation $ebb_ctr
    ebb_ctr=$((ebb_ctr + 1))
done

exit 0
