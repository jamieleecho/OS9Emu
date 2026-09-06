# Naming a program by a pathname rather than by module name.
#
# OS-9 looks for a program in the execution directory and nowhere else:
# F$Load opens with EXEC. set, and ioman starts a relative pathlist there when
# it is. A bare name is a module name and gets exactly that. A name with a "/"
# in it is a pathname, and "./prog" typed at a host shell means the prog here,
# so our own command line tries the working directory for those too.
mkdir -p BIN
cp CMDS/echo BIN/myecho
cp CMDS/echo ./here

echo "--- absolute"          ; $OS9 /dd/BIN/myecho absolute
echo "--- relative with /"   ; $OS9 BIN/myecho relative
echo "--- ./name"            ; $OS9 ./here dotslash
echo "--- plain module name" ; $OS9 echo plain
echo "--- not there anywhere"; $OS9 nosuchprog 2>&1; echo " rc=$?"

# A bare name is looked up in the execution directory alone, the way OS-9 does
# it -- "here" sits in the working directory and must not be found.
echo "--- bare name in the working directory"; $OS9 here bare 2>&1; echo " rc=$?"
echo "--- the same one, named as a path"     ; $OS9 ./here path

# Inside the shell an absolute pathname has to survive the failed F$Link that
# precedes the open -- F$PrsNam eats the leading "/", and a link that reports
# E$MNF must hand the caller back the X it was given, or the shell opens a
# relative name and gets the execution directory pasted in front of it.
echo "--- absolute, from the shell"
printf '/dd/BIN/myecho fromshell\n' | $OS9 shell 2>/dev/null | tr '\r' '\n' \
  | grep fromshell
