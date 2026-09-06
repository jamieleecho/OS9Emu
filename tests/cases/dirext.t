# dir -e prints a line per file out of the file descriptor sector, and it asks
# for one with SS.FDInf -- the descriptor of any file on the device, named by
# the number the directory entry carries rather than by a path. Answering
# E$UnkSvc ends the listing there: dir takes the error as fatal and exits with
# it, so "dir -e" printed a header and error 208 and nothing else.
#
# The date, the time and the sector numbers are the host's to decide; what is
# checked here is the attributes, the size and the name.
mkdir EXT
printf 'hello\r' > EXT/one
printf 'a much longer line of text\r' > EXT/two
: > EXT/empty
chmod 755 EXT/one
mkdir EXT/SUB
: > EXT/SUB/inside
$OS9 dir -e EXT | tr -d '\n' | tr '\r' '\n' \
  | awk '$1 ~ /^[0-9]+$/ { print $4, $6, $7 }' | sort
