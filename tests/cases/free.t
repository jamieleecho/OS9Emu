# free reports the disk the emulator is standing on. There is no disk -- the
# numbers come from the host filesystem -- so only the shape of the output is
# checked, not the values.
$OS9 --eol crlf free | sed -e 's/[0-9][0-9,]*/N/g' -e 's/[0-9]\{4\}\/[0-9]\{2\}\/[0-9]\{2\}/<date>/' -e 's/^".*"/"<volume>"/'
