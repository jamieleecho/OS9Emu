# working and execution directory reporting.
#
# A process starts in /dd with /dd/CMDS as its execution directory, and both
# are printed in full: pwd and pxd walk the directory entries up to the root,
# so this exercises the ".." entry of every directory along the way.
$OS9 pwd
$OS9 pxd
$OS9 --workdir /h0 --execdir /h0/CMDS pwd
$OS9 --workdir /h0 --execdir /h0/CMDS pxd
$OS9 makdir DEEPER
$OS9 --workdir /dd/DEEPER pwd
