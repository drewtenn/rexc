# Make rules for rendering the Rexy book PDF and EPUB artifacts.
include docs/sources.mk

EPUB_FRONTMATTER := docs/epub-copyright.md
PDF_FRONTMATTER := docs/epub-copyright.md
PDF_METADATA := docs/pdf-metadata.yaml
PDF_FILTER := docs/pdf.lua
PDF_TEMPLATE := docs/pdf-template.typ

PDF := docs/Rexy.pdf
EPUB := docs/Rexy.epub
COVER_ART := docs/cover-art.png

DIAGRAMS_SVG := $(wildcard docs/diagrams/*.svg)
DIAGRAMS_PNG := $(DIAGRAMS_SVG:.svg=.png)

docs/diagrams/%.png: docs/diagrams/%.svg
	@if command -v rsvg-convert >/dev/null 2>&1; then \
		rsvg-convert -f png -z 2 -o "$@" "$<"; \
	elif $(PYTHON) -c "import cairosvg" >/dev/null 2>&1; then \
		$(PYTHON) -c "import cairosvg; cairosvg.svg2png(url='$<', write_to='$@', scale=2)"; \
	else \
		echo "error: need rsvg-convert or Python package cairosvg to build EPUB diagrams" >&2; \
		exit 1; \
	fi

$(COVER_ART): docs/generate_cover.py
	$(PYTHON) docs/generate_cover.py "$@"

docs/Rexy.pdf: $(PDF_FRONTMATTER) $(DOCS_SRC) $(COVER_ART) docs/epub-metadata.yaml $(PDF_METADATA) $(PDF_FILTER) $(PDF_TEMPLATE)
	pandoc $(PDF_FRONTMATTER) $(DOCS_SRC) \
	    --to typst \
	    --standalone \
	    --template "$(PDF_TEMPLATE)" \
	    --metadata-file docs/epub-metadata.yaml \
	    --metadata-file "$(PDF_METADATA)" \
	    --lua-filter "$(PDF_FILTER)" \
	    --syntax-highlighting=monochrome \
	    --resource-path=docs \
	    --pdf-engine=typst \
	    -o "$(PDF)"

docs/Rexy.epub: $(EPUB_FRONTMATTER) $(DOCS_SRC) $(DIAGRAMS_PNG) $(COVER_ART) docs/epub.css docs/epub-metadata.yaml docs/strip-latex.lua
	tmpdir="$$(mktemp -d /tmp/rexc-epub.XXXXXX)"; \
	pandoc $(EPUB_FRONTMATTER) $(DOCS_SRC) \
	    --to epub3 \
	    --toc \
	    --toc-depth=2 \
	    --split-level=2 \
	    --epub-title-page=false \
	    --epub-cover-image="$(COVER_ART)" \
	    --css docs/epub.css \
	    --metadata-file docs/epub-metadata.yaml \
	    --lua-filter docs/strip-latex.lua \
	    --syntax-highlighting=monochrome \
	    --resource-path=docs \
	    -o "$$tmpdir/book.epub"; \
	cd "$$tmpdir" && unzip -q book.epub -d unpacked; \
	perl -0pi -e 's|<itemref idref="cover_xhtml" />\s*<itemref idref="nav" />\s*<itemref idref="ch001_xhtml" />|<itemref idref="cover_xhtml" />\n    <itemref idref="ch001_xhtml" />\n    <itemref idref="nav" />|s' "$$tmpdir/unpacked/EPUB/content.opf"; \
	rm -f "$(EPUB)"; \
	cd "$$tmpdir/unpacked" && zip -X0 "../book-fixed.epub" mimetype >/dev/null && zip -Xr9D "../book-fixed.epub" META-INF EPUB >/dev/null; \
	mv "$$tmpdir/book-fixed.epub" "$(CURDIR)/$(EPUB)"; \
	rm -rf "$$tmpdir"

.PHONY: pdf epub docs clean-docs

pdf: docs/Rexy.pdf

epub: docs/Rexy.epub

docs: pdf epub

clean-docs:
	rm -f "$(PDF)" "$(EPUB)" "$(COVER_ART)" $(DIAGRAMS_PNG)
