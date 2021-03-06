.TH KDB 8 "April 4, 2003"
.hy 0
.SH NAME
Built-in Kernel Debugger for Linux - v4.1
.SH "Overview"
This document describes the built-in kernel debugger available
for linux.   This debugger allows the programmer to interactively
examine kernel memory, disassemble kernel functions, set breakpoints
in the kernel code and display and modify register contents.
.P
A symbol table is included in the kernel image and in modules which
enables all non-stack symbols (including static symbols) to be used as
arguments to the kernel debugger commands.
.SH "Getting Started"
To include the kernel debugger in a linux kernel, use a
configuration mechanism (e.g. xconfig, menuconfig, et. al.)
to enable the \fBCONFIG_KDB\fP option.   Additionally, for accurate
stack tracebacks, it is recommended that the \fBCONFIG_FRAME_POINTER\fP
option be enabled (if present).   \fBCONFIG_FRAME_POINTER\fP changes the compiler
flags so that the frame pointer register will be used as a frame
pointer rather than a general purpose register.
.P
After linux has been configured to include the kernel debugger,
make a new kernel with the new configuration file (a make clean
is recommended before making the kernel), and install the kernel
as normal.
.P
You can compile a kernel with kdb support but have kdb off by default,
select \fBCONFIG_KDB_OFF\fR.  Then the user has to explicitly activate
kdb by booting with the 'kdb=on' flag or, after /proc is mounted, by
.nf
  echo "1" > /proc/sys/kernel/kdb
.fi
You can also do the reverse, compile a kernel with kdb on and
deactivate kdb with the boot flag 'kdb=off' or, after /proc is mounted,
by
.nf
  echo "0" > /proc/sys/kernel/kdb
.fi
.P
When booting the new kernel, the 'kdb=early' flag
may be added after the image name on the boot line to
force the kernel to stop in the kernel debugger early in the
kernel initialization process.  'kdb=early' implies 'kdb=on'.
If the 'kdb=early' flag isn't provided, then kdb will automatically be
invoked upon system panic or when the \fBPAUSE\fP key is used from the
keyboard, assuming that kdb is on.  Older versions of kdb used just a
boot flag of 'kdb' to activate kdb early, this is still supported but
is deprecated.
.P
Kdb can also be used via the serial port.  Set up the system to
have a serial console (see \fIDocumentation/serial-console.txt\fP), you
must also have a user space program such as agetty set up to read from
the serial console..
The \fBControl-A\fP key sequence on the serial port will cause the
kernel debugger to be entered, assuming that kdb is on, that some
program is reading from the serial console, at least one cpu is
accepting interrupts and the serial consoel driver is still usable.
.P
\fBNote:\fR\ Your distributor may have chosen a different kdb
activation sequence for the serial console.
Consult your distribution documentation.
.P
If you have both a keyboard+video and a serial console, you can use
either for kdb.
Define both video and serial consoles with boot parameters
.P
.nf
  console=tty0 console=ttyS0,38400
.fi
.P
Any kdb data entered on the keyboard or the serial console will be echoed
to both.
.P
While kdb is active, the keyboard (not serial console) indicators may strobe.
The caps lock and scroll lock lights will turn on and off, num lock is not used
because it can confuse laptop keyboards where the numeric keypad is mapped over
the normal keys.
On exit from kdb the keyboard indicators will probably be wrong, they will not match the kernel state.
Pressing caps lock twice should get the indicators back in sync with
the kernel.
.SH "Basic Commands"
There are several categories of commands available to the
kernel debugger user including commands providing memory
display and modification, register display and modification,
instruction disassemble, breakpoints and stack tracebacks.
Any command can be prefixed with '-' which will cause kdb to ignore any
errors on that command, this is useful when packaging commands using
defcmd.
.P
The following table shows the currently implemented standard commands,
these are always available.  Other commands can be added by extra
debugging modules, type '?' at the kdb prompt to get a list of all
available commands.
.DS
.TS
box, center;
l | l
l | l.
Command	Description
_
bc	Clear Breakpoint
bd	Disable Breakpoint
be	Enable Breakpoint
bl	Display breakpoints
bp	Set or Display breakpoint
bph	Set or Display hardware breakpoint
bpa	Set or Display breakpoint globally
bpha	Set or Display hardware breakpoint globally
bt	Stack backtrace for current process
btp	Stack backtrace for specific process
bta	Stack backtrace for all processes
btc	Cycle over all live cpus and backtrace each one
cpu	Display or switch cpus
dmesg	Display system messages
defcmd	Define a command as a set of other commands
ef	Print exception frame
env	Show environment
go	Restart execution
help	Display help message
id	Disassemble Instructions
kill	Send a signal to a process
ll	Follow Linked Lists
lsmod	List loaded modules
md	Display memory contents
mdWcN	Display memory contents with width W and count N.
mdr	Display raw memory contents
mds	Display memory contents symbolically
mm	Modify memory contents, words
mmW	Modify memory contents, bytes
ps	Display process status
reboot	Reboot the machine
rd	Display register contents
rm	Modify register contents
rmmod	Remove a module
sections	List information on all known sections
set	Add/change environment variable
sr	Invoke SysReq commands
ss	Single step a cpu
ssb	Single step a cpu until a branch instruction
.TE
.DE
.P
Some commands can be abbreviated, such commands are indicated by a
non-zero \fIminlen\fP parameter to \fBkdb_register\fP; the value of
\fIminlen\fP being the minimum length to which the command can be
abbreviated (for example, the \fBgo\fP command can be abbreviated
legally to \fBg\fP).
.P
If an input string does not match a command in the command table,
it is treated as an address expression and the corresponding address
value and nearest symbol are shown.
.P
Some of the commands are described here.
Information on the more complicated commands can be found in the
appropriate manual pages.
.TP 8
cpu
With no parameters, it lists the available cpus, '*' after a cpu number
indicates a cpu that did not respond to the kdb stop signal.
.I cpu
followed by a number will switch to that cpu, you cannot switch to
a cpu marked '*'.
This command is only available if the kernel was configured for SMP.
.TP 8
dmesg [lines]
Displays the last set of system messages from the kernel buffer.  If
kdb logging is on, it is disabled by dmesg and is left as disabled.
If lines is specified, only dump the last 'lines' from the buffer, 0
dumps all lines.
.TP 8
defcmd
Defines a new command as a set of other commands, all input until
.I endefcmd
is saved and executed as a package.
.I defcmd
takes three parameters, the command name to be defined and used to
invoke the package, a quoted string containing the usage text and a
quoted string containing the help text for the command.
When using defcmd, it is a good idea to prefix commands that might fail
with '-', this ignores errors so the following commands are still
executed.
For example,
.P
.nf
        defcmd diag "" "Standard diagnostics"
          set LINES 2000
          set BTAPROMPT 0
          -id %eip-0x40
          -cpu
          -ps
          -dmesg 80
          -bt
          -bta
        endefcmd          
.fi
.TP 8
go
Continue normal execution.
Active breakpoints are reestablished and the processor(s) allowed to
run normally.
To continue at a specific address, use
.I rm
to change the instruction pointer then go.
.TP 8
id
Disassemble instructions starting at an address.
Environment variable IDCOUNT controls how many lines of disassembly
output the command produces.
.TP 8
kill
Internal command to send a signal (like kill(1)) to a process.
kill -signal pid.
.TP 8
lsmod
Internal command to list modules.
This does not use any kernel nor user space services so can be used at any time.
.TP 8
ps
Display status of all processes in the desired state.
This command does not take any locks (all cpus should be frozen while
kdb is running) so it can safely be used to debug lock problems with
the process table.
Without any parameters, \fBps\fP displays all processes.
If a parameter is specified, it is a single string consisting of the
letters D, R, S, T, Z and U, in any order.
Each letter selects processes in a specific state, when multiple
letters are specified, a process will be displayed if it is in any of
the specified states.
\fBps\ RD\fR displays only tasks that are running or are in an
uninterruptible sleep.
The states are\ :-
.P
.DS
.TS
box, center;
l | l
l | l.
D	Uninterruptible sleep
R	Running
S	Interruptible sleep
T	Traced or stopped
Z	Zombie
U	Unrunnable
.TE
.DE
.P
.TP 8
reboot
Reboot the system, with no attempt to do a clean close down.
.TP 8
rmmod
Internal command to remove a module.
This does not use any user space services, however it calls the module
cleanup routine and that routine may try to use kernel services.
Because kdb runs disabled there is no guarantee that the module cleanup
routine will succeed, there is a real risk of the routine hanging and
taking kdb with it.
Use the
.I rmmod
command with extreme care.
.TP 8
sections
List information for all known sections.  The output is one line per
module plus the kernel, starting with the module name.  This is
followed by one or more repeats of section name, section start,
section end and section flags.  This data is not designed for human
readability, it is intended to tell external debuggers where each
section has been loaded.
.SH INITIAL KDB COMMANDS
kdb/kdb_cmds is a plain text file where you can define kdb commands
which are to be issued during kdb_init().  One command per line, blank
lines are ignored, lines starting with '#' are ignored.  kdb_cmds is
intended for per user customization of kdb, you can use it to set
environment variables to suit your hardware or to set standard
breakpoints for the problem you are debugging.  This file is converted
to a small C object, compiled and linked into the kernel.  You must
rebuild and reinstall the kernel after changing kdb_cmds.  This file
will never be shipped with any useful data so you can always override
it with your local copy.  Sample kdb_cmds:
.P
.nf
# Initial commands for kdb, alter to suit your needs.
# These commands are executed in kdb_init() context, no SMP, no
# processes.  Commands that require process data (including stack or
# registers) are not reliable this early.  set and bp commands should
# be safe.  Global breakpoint commands affect each cpu as it is booted.

set LINES=50
set MDCOUNT=25
set RECURSE=1
bp sys_init_module
.fi
.SH INTERRUPTS AND KDB
When a kdb event occurs, one cpu (the initial cpu) enters kdb state.
It uses a cross system interrupt to interrupt the
other cpus and bring them all into kdb state.  All cpus run with
interrupts disabled while they are inside kdb, this prevents most
external events from disturbing the kernel while kdb is running.
.B Note:
Disabled interrupts means that any I/O that relies on interrupts cannot
proceed while kdb is in control, devices can time out.  The clock tick
is also disabled, machines will lose track of time while they are
inside kdb.
.P
Even with interrupts disabled, some non-maskable interrupt events will
still occur, these can disturb the kernel while you are debugging it.
The initial cpu will still accept NMI events, assuming that kdb was not
entered for an NMI event.  Any cpu where you use the SS or SSB commands
will accept NMI events, even after the instruction has finished and the
cpu is back in kdb.  This is an unavoidable side effect of the fact that
doing SS[B] requires the cpu to drop all the way out of kdb, including
exiting from the event that brought the cpu into kdb.  Under normal
circumstances the only NMI event is for the NMI oopser and that is kdb
aware so it does not disturb the kernel while kdb is running.
.P
Sometimes doing SS or SSB on ix86 will allow one interrupt to proceed,
even though the cpu is disabled for interrupts.  I have not been able
to track this one down but I suspect that the interrupt was pending
when kdb was entered and it runs when kdb exits through IRET even
though the popped flags are marked as cli().  If any ix86 hardware
expert can shed some light on this problem, please notify the kdb
maintainer.
.SH RECOVERING FROM KDB ERRORS
If a kdb command breaks and kdb has enough of a recovery environment
then kdb will abort the command and drop back into mainline kdb code.
This means that user written kdb commands can follow bad pointers
without killing kdb.  Ideally all code should verify that data areas
are valid (using kdb_getarea) before accessing it but lots of calls to
kdb_getarea can be clumsy.
.P
The sparc64 port does not currently provide this error recovery.
If someone would volunteer to write the necessary longjmp/setjmp
code, their efforts would be greatly appreciated. In the
meantime, it is possible for kdb to trigger a panic by accessing
a bad address.
.SH DEBUGGING THE DEBUGGER
kdb has limited support for debugging problems within kdb.  If you
suspect that kdb is failing, you can set environment variable KDBDEBUG
to a bit pattern which will activate kdb_printf statements within kdb.
See include/linux/kdb.h, KDB_DEBUG_FLAG_xxx defines.  For example
.nf
  set KDBDEBUG=0x60
.fi
activates the event callbacks into kdb plus state tracing in sections
of kdb.
.nf
  set KDBDEBUG=0x18
.fi
gives lots of tracing as kdb tries to decode the process stack.
.P
You can also perform one level of recursion in kdb.  If environment
variable RECURSE is not set or is 0 then kdb will either recover from
an error (if the recovery environment is satisfactory) or kdb will
allow the error to percolate, usually resulting in a dead system.  When
RECURSE is 1 then kdb will recover from an error or, if there is no
satisfactory recovery environment, it will drop into kdb state to let
you diagnose the problem.  When RECURSE is 2 then all errors drop into
kdb state, kdb does not attempt recovery first.  Errors while in
recursive state all drop through, kdb does not even attempt to recover
from recursive errors.
.SH KEYBOARD EDITING
kdb supports a command history, which can be accessed via keyboard
sequences.
It supports the special keys on PC keyboards, control characters and
vt100 sequences on a serial console or a PC keyboard.
.P
.DS
.TS
box, center;
l | l | l l | l
l | l | l l | l.
PC Special keys	Control	VT100 key	Codes	Action
_
Backspace	ctrl-H	Backspace	0x7f	Delete character to the left of the cursor
Delete	ctrl-D	Delete	\\e[3~	Delete character to the right of the cursor
Home	ctrl-A	Home	\\e[1~	Go to start of line
End	ctrl-E	End	\\e[4~	Go to end of line
Up arrow	ctrl-P	Up arrow	\\e[A	Up one command in history
Down arrow	ctrl-N	Down arrow	\\e[B	Down one command in history
Left arrow	ctrl-B	Left arrow	\\e[D	Left one character in current command
Right arrow	ctrl-F	Right arrow	\\e[C	Right one character in current command
.TE
.DE
.P
There is no toggle for insert/replace mode, kdb editing is always in
insert mode.
Use delete and backspace to delete characters.
.P
kdb also supports tab completion for kernel symbols
Type the start of a kernel symbol and press tab (ctrl-I) to complete
the name
If there is more than one possible match, kdb will append any common
characters and wait for more input, pressing tab a second time will
display the possible matches
The number of matches is limited by environment variable DTABCOUNT,
with a default of 30 if that variable is not set.
.SH AUTHORS
Scott Lurndal, Richard Bass, Scott Foehner, Srinivasa Thirumalachar,
Masahiro Adegawa, Marc Esipovich, Ted Kline, Steve Lord, Andi Kleen,
Sonic Zhang.
.br
Keith Owens <kaos@sgi.com> - kdb maintainer.
.SH SEE ALSO
.P
linux/Documentation/kdb/kdb_{bp,bt,env,ll,md,rd,sr,ss}.man
