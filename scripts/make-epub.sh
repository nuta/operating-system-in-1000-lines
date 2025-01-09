#!/bin/bash
set -euo pipefail 

cd website/en
pandoc \
  -o "Writing an OS in 1,000 Lines.epub" \
  meta/title.txt \
  {index.md,0*.md,1*.md} \
  --css=meta/stylesheet.css \
  --epub-metadata=meta/metadata.xml \
  --table-of-contents

