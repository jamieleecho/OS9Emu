# A run of dots in a pathname.
#
# A component of nothing but dots is not a name: one dot is this directory and
# every dot after the first goes up another level, so ".." is the parent and
# "..." the grandparent. A dot anywhere else is an ordinary character. The
# root of the disk is its own parent, so no run of dots, however long, walks
# out of the OS-9 filesystem and into the host's.
$OS9 makdir SUB
$OS9 makdir SUB/DEEP
$OS9 makdir SUB/DEEP/DEEPER
$OS9 copy hello.txt SUB/DEEP/there.txt
$OS9 copy words.txt SUB/DEEP/DEEPER/bottom.txt

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

# The same listing helper, three directories down, so each extra dot has
# somewhere further to climb: DEEPER, DEEP, SUB, then the root.
deep() {
  $OS9 --workdir /dd/SUB/DEEP/DEEPER dir "$1" | tr '\r' '\n' | tail -n +3 \
    | tr ' ' '\n' | grep -v '^$' | sort | tr '\n' ' '
  echo
}

echo "--- ."                        ; deep .
echo "--- .. is DEEP"               ; deep ..
echo "--- ... is SUB"               ; deep ...
echo "--- .... is the root"         ; deep ....
echo "--- ..... climbs no further"  ; deep .....
echo "--- ../.. says what ... says" ; deep ../..

echo "--- components follow a run of dots like any other"
$OS9 --workdir /dd/SUB/DEEP/DEEPER list .../DEEP/there.txt

# A name that merely begins with dots is a name: the rule is about a component
# that is nothing else, not about what a component starts with.
printf 'begins with dots\r' > SUB/DEEP/DEEPER/..name
echo "--- a name that begins with dots"
$OS9 --workdir /dd/SUB/DEEP/DEEPER list ..name

echo "--- pwd walks the entries back up"
# shellplus writes the first half of its banner to standard output and
# without a line ending, so it lands in front of the first pwd. Cut it away.
printf 'chd SUB/DEEP\npwd\nchd ..\npwd\nchd ..\npwd\nchd ..\npwd\n' \
  | $OS9 shell 2>/dev/null | tr '\r' '\n' \
  | sed -e 's/^Shell+ v[0-9.]*[a-z]* //' | grep '^/'
