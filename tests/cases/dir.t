# dir on a data directory. The modification date is filtered out: it is the
# host's clock, and a golden file cannot know what it will say.
$OS9 dir DATA | tr '\r ' '\n\n' | grep -vE '^$|^[0-9]{4}/|^[0-9]{2}:' | sort
