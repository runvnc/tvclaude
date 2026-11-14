# Keys typed into tvclaude by demo/record.sh, with pauses.
typeslow() { s="$1"; i=0; while [ $i -lt ${#s} ]; do printf '%s' "${s:$i:1}"; sleep 0.045; i=$((i+1)); done; }
sleep 7
printf '\033w'; sleep 0.8; printf 't'; sleep 1.5          # Window > Tile
typeslow "Write a short post about Turbo Pascal in turbo.md"; sleep 0.4; printf '\r'; sleep 2
printf '\033[17~'; sleep 1.2                                # F6: other window
typeslow "Add a remove(items, index) function to todo.py"; sleep 0.4; printf '\r'; sleep 5
printf '\033s'; sleep 1.5; printf '\033'; sleep 0.5          # Session menu
printf '\0330'; sleep 3; printf '\033'; sleep 1             # Session list
printf '\033o'; sleep 1.5; printf '\033[C'; sleep 1.5; printf '\033'; sleep 0.5; printf '\033'; sleep 1  # Options, Info
sleep 28
printf '\0330'; sleep 3; printf '\033'; sleep 1             # Session list again
printf '\033[15~'; sleep 3; printf '\033[15~'; sleep 3     # F5 zoom and back
