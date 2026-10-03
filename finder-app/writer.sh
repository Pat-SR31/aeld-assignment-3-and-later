#!/bin/bash
# writer.sh <writefile> <writestr>
writefile=$1
writestr=$2

if [ -z "$writefile" ] || [ -z "$writestr"  ]; then
	echo "Error: usage: writer.sh <writefile> <writestr>"
	exit 1
fi

mkdir -p "$(dirname "$writefile")" || { echo "Error: cannot create directory for $writefile"; exit 1; }

echo "$writestr" > "$writefile" || { echo "Error: could not write $writefile"; exit 1; }
