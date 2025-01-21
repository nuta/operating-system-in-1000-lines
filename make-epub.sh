#!/bin/bash
set -euo pipefail 

cd website/en
pandoc \
  -f gfm+alerts \
  -o ../../"Writing an OS in 1,000 Lines, v0.1.1-alpha.epub" \
  ../../epub/title.txt \
  {index.md,0*.md,1*.md} \
  --css=../../epub/stylesheet.css \
  --epub-metadata=../../epub/metadata.xml \
  --table-of-contents
