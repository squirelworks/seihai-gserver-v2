#!/bin/sh
# Seihai lab world for macOS: double-click this file in Finder to start both lab
# servers in Terminal. It runs start.sh beside it; see start.sh for the options.
exec "$(dirname "$0")/start.sh" "$@"
