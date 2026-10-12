# gdb script for the SSP: waits for Synthor to crash, then records why. Synthor's stderr is
# /dev/null, and gdb there cannot unwind past libc, so it also traps asserts, C++ throws and
# std::terminate, and dumps each thread's raw stack as symbols.
#
# usage, from the host (attaching pauses Synthor for a few seconds):
#   scp -O scripts/ssp-crash.gdb root@192.168.1.6:/tmp/
#   ssh root@192.168.1.6 'nohup gdb -batch -x /tmp/ssp-crash.gdb -p $(pidof SYNTHOR) > /tmp/gdb.log 2>&1 &'
# After a crash, read /tmp/gdb.log; gdb detaches and Synthor restarts.

set pagination off
set confirm off
set print thread-events off
handle all nostop noprint pass
handle SIGSEGV stop print nopass
handle SIGBUS stop print nopass
handle SIGILL stop print nopass
handle SIGABRT stop print nopass
handle SIGFPE stop print nopass

break __assert_fail
commands
silent
echo \n=== ASSERT ===\n
x/s $r0
x/s $r1
p $r2
x/s $r3
bt 40
continue
end

catch throw
commands
silent
echo \n=== THROW ===\n
bt 40
continue
end

break std::terminate
commands
silent
echo \n=== TERMINATE ===\n
bt 40
continue
end

# Libraries loaded from here on, such as scsy's 196 UGen libraries, are not read for symbols:
# reading them made every load take seconds, which changed timing enough to hide a crash. A crash
# reads them all first.
set auto-solib-add off

continue

# Ordered so that a command that fails, which ends the script, comes last.
echo \n=== CRASH ===\n
sharedlibrary
info registers
bt 60
echo \n=== THREADS ===\n
info threads
thread apply all bt 25
echo \n=== LIBRARIES ===\n
info sharedlibrary
echo \n=== STACKS ===\n
thread apply all x/64a $sp
echo \n=== PC ===\n
x/6i $pc
