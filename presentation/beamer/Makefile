# Build the slide deck.
#
#   make          → main.pdf   (pdflatex twice, so page totals settle)
#   make view     → build and open it
#   make clean    → remove generated files

MAIN  = main
LATEX = pdflatex -interaction=nonstopmode

.PHONY: all view clean

all: $(MAIN).pdf

$(MAIN).pdf: $(MAIN).tex theme.tex deck.tex content.tex
	$(LATEX) $(MAIN).tex > build.log 2>&1
	$(LATEX) $(MAIN).tex > build.log 2>&1
	@echo "→ $(MAIN).pdf"

view: $(MAIN).pdf
	@start "" $(MAIN).pdf 2>/dev/null || \
	 xdg-open $(MAIN).pdf 2>/dev/null || \
	 open $(MAIN).pdf

clean:
	-rm -f $(MAIN).pdf $(MAIN).aux $(MAIN).log $(MAIN).nav $(MAIN).out \
	       $(MAIN).snm $(MAIN).toc $(MAIN).vrb build.log
