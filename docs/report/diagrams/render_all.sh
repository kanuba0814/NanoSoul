#!/usr/bin/env bash
# 批量把 diagrams/*.html 渲成 figures/<name>.png（去白边 + 白边距）
set -e
cd "$(dirname "$0")"
OUTDIR=../figures
TMP=/tmp/claude-1000/-home-gxxl-NanoSoul/e638f7b1-f7cf-47c6-a199-e4e4d805296c/scratchpad/render
mkdir -p "$TMP" "$OUTDIR"
for html in "$@"; do
  base=$(basename "$html" .html)
  timeout 90 chromium-browser --headless=new --no-sandbox --hide-scrollbars \
    --force-device-scale-factor=2 --window-size=1900,2600 \
    --default-background-color=FFFFFFFF \
    --screenshot="$TMP/$base.png" "file://$PWD/$html" >/dev/null 2>&1
  magick "$TMP/$base.png" -trim +repage -bordercolor white -border 26 "$OUTDIR/$base.png"
  echo "rendered $base -> $(identify -format '%wx%h' "$OUTDIR/$base.png")"
done
