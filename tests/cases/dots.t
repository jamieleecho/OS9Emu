# "." and ".." in a pathname.
#
# A component of "." is this directory and ".." is the parent, and neither is
# part of the name of anything. The root of the disk is its own parent, so no
# amount of ".." walks out of the OS-9 filesystem and into the host's.
$OS9 makdir SUB
$OS9 makdir SUB/DEEP
$OS9 copy hello.txt SUB/DEEP/there.txt

# The names in a listing, sorted, with the leading blank line and the
# "Directory of" header dropped: the header carries the host's clock. What a
# directory holds does not depend on how it was named, or on readdir order.
list() {
  $OS9 dir "$1" | tr '\r' '\n' | tail -n +3 | tr ' ' '\n' \
    | grep -v '^$' | sort | tr '\n' ' '
  echo
}

echo "--- SUB"           ; list SUB
echo "--- SUB/."         ; list SUB/.
echo "--- SUB/DEEP/.."   ; list SUB/DEEP/..
echo "--- SUB/./DEEP"    ; list SUB/./DEEP
echo "--- .. of the root"; list ..
echo "--- above the root"; list ../../../..

echo "--- a dot is otherwise just a character"
$OS9 list SUB/DEEP/there.txt

echo "--- pwd walks the entries back up"
# shellplus writes the first half of its banner to standard output and
# without a line ending, so it lands in front of the first pwd. Cut it away.
printf 'chd SUB/DEEP\npwd\nchd ..\npwd\nchd ..\npwd\nchd ..\npwd\n' \
  | $OS9 shell 2>/dev/null | tr '\r' '\n' \
  | sed -e 's/^Shell+ v[0-9.]*[a-z]* //' | grep '^/'
