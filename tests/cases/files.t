# A round trip through the file utilities: make a directory, copy into it,
# rename, list, delete.
$OS9 makdir WORK
$OS9 copy hello.txt WORK/copy.txt
echo "--- after copy ---"
$OS9 list WORK/copy.txt
$OS9 rename WORK/copy.txt renamed.txt
echo "--- after rename ---"
$OS9 dir WORK | tr '\r ' '\n\n' | grep -vE '^$|^[0-9]{4}/|^[0-9]{2}:' | sort
$OS9 del WORK/renamed.txt
echo "--- after delete ---"
$OS9 dir WORK | tr '\r ' '\n\n' | grep -vE '^$|^[0-9]{4}/|^[0-9]{2}:' | sort
