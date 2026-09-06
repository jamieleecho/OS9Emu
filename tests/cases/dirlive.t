# A directory listing follows the host. A program that opens a directory and
# keeps reading it sees a file created afterwards -- by itself or by anything
# else -- and stops seeing one that has gone, the way RBF's own directory
# sectors behave. It used to see the listing as it was when it opened the
# path and nothing after that.
tr '\n' '\r' > dirlive.c <<'EOF'
#include <stdio.h>
#include <modes.h>
#include <direct.h>

struct dirent ent;
char name[30];

look(path)
int path;
{
  int i;
  int found;

  found = 0;
  lseek(path, 0L, 0);
  while (read(path, &ent, 32) == 32) {
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
    if (strcmp(name, "zzlive") == 0)
      found = 1;
  }
  return found;
}

say(what, path)
char *what;
int path;
{
  printf("%s %s\n", what, look(path) ? "yes" : "no");
}

main()
{
  int p;
  int fd;

  p = open(".", S_IFDIR+S_IREAD);
  if (p < 0) {
    printf("cannot open .\n");
    exit(1);
  }
  say("before create:", p);
  fd = creat("zzlive", S_IREAD+S_IWRITE);
  close(fd);
  say("after create: ", p);
  unlink("zzlive");
  say("after delete: ", p);
  close(p);
}
EOF
$OS9 cc1 dirlive.c
echo "--- run ---"
$OS9 --eol crlf dirlive
