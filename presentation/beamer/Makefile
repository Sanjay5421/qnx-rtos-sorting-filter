# Build the slide deck.
#
#   make            → build main.pdf
#   make view       → build and open it
#   make clean      → remove every generated file
#
# latexmk runs pdflatex as many times as the cross-references need, so the
# page totals and the outline settle on their own.

MAIN    = main
LATEXMK = latexmk
FLAGS   = -pdf -interaction=nonstopmode -halt-on-error

.PHONY: all view clean

all: $(MAIN).pdf

$(MAIN).pdf: $(MAIN).tex theme.tex
	$(LATEXMK) $(FLAGS) $(MAIN).tex

# Straight pdfLaTeX, if latexmk is not installed.
pdf: $(MAIN).tex theme.tex
	pdflatex $(FLAGS) $(MAIN).tex
	pdflatex $(FLAGS) $(MAIN).tex

view: $(MAIN).pdf
	@start "" $(MAIN).pdf 2>/dev/null || xdg-open $(MAIN).pdf 2>/dev/null || \
	  open $(MAIN).pdf

clean:
	$(LATEXMK) -C $(MAIN).tex 2>/dev/null || true
	-rm -f $(MAIN).pdf $(MAIN).aux $(MAIN).log $(MAIN).nav $(MAIN).out \
	       $(MAIN).snm $(MAIN).toc $(MAIN).fls $(MAIN).fdb_latexmk
