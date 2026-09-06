# An entry keeps its slot. RBF never moves one -- a slot belongs to its file
# until the file goes -- and rename(1) depends on it: PD.DCP is a byte offset
# into the directory, read before the write and used after it. So bringing the
# listing back in step with the host has to recognise every entry it already
# has, including a name too long to store whole and one whose own bytes have
# the high bit set.
: > "another_very_long_filename_here"
: > "café"
tr '\n' '\r' > dirslots.c <<'EOF'
#include <stdio.h>
#include <modes.h>
#include <direct.h>

struct dirent ent;
char before[40][30];
char name[30];
int nbefore;

getname()
{
  int i;

  for (i = 0; i < 29; i++) {
    name[i] = ent.dir_name[i] & 0x7f;
    if (ent.dir_name[i] & 0x80) {
      i++;
      break;
    }
    if (name[i] == 0)
      break;
  }
  name[i] = 0;
  if (ent.dir_name[0] == 0)
    name[0] = 0;
}

main()
{
  int p;
  int fd;
  int slot;
  int moved;

  p = open(".", S_IFDIR+S_IREAD);
  if (p < 0) {
    printf("cannot open .\n");
    exit(1);
  }

  nbefore = 0;
  slot = 0;
  while (read(p, &ent, 32) == 32) {
    getname();
    if (slot < 40) {
      strcpy(before[slot], name);
      nbefore = slot + 1;
    }
    slot++;
  }

  fd = creat("zznew", S_IREAD+S_IWRITE);
  close(fd);

  moved = 0;
  lseek(p, 0L, 0);
  slot = 0;
  while (read(p, &ent, 32) == 32) {
    getname();
    if (slot < nbefore && before[slot][0] != 0 &&
        strcmp(before[slot], name) != 0) {
      printf("moved: slot %d held %s, now holds %s\n", slot, before[slot],
             name);
      moved++;
    }
    slot++;
  }
  printf("%s\n", moved ? "slots moved" : "slots stable");
  unlink("zznew");
  close(p);
}
EOF
$OS9 cc1 dirslots.c
echo "--- run ---"
$OS9 --eol crlf dirslots
