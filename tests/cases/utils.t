# Assorted utilities that should agree with a real system.
echo "--- ident ---";   $OS9 ident -s /dd/CMDS/echo
echo "--- attr ---";    $OS9 attr hello.txt
echo "--- cmp same ---"; $OS9 cmp hello.txt hello.txt
echo "--- dump ---";    $OS9 dump DATA/small.txt
echo "--- tmode ---";   $OS9 tmode
