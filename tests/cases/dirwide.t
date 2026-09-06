# dir lays a line out in a buffer and then writes a fixed 80 bytes of it, and
# its own check for that buffer filling up compares an absolute address, so it
# never fires for a process whose data area is not at $0000. Told a screen
# wider than 80 columns it therefore drops every name that falls past the
# eightieth -- silently, because those names are never written at all. The
# width we report is capped so it cannot be told one.
mkdir WIDE
for n in aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll; do : > "WIDE/$n"; done
for cols in 32 40 80 120 200; do
  echo "--- $cols columns ---"
  $OS9 --cols $cols dir WIDE \
    | tr '\r ' '\n\n' | grep -vE '^$|^[0-9]{4}/|^[0-9]{2}:' | sort | tr '\n' ' '
  echo
done
