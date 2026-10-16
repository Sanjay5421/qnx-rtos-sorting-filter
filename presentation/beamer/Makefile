# Build the slide deck (all four themes).
#
#   make            → theme-*/main-*.pdf
#   make a          → just theme A
#   make view-a     → build and open a theme
#   make clean      → remove generated files
#
# Each theme compiles in its own folder; the shared sources are one level up,
# so nothing needs copying. latexmk would also work, but plain pdflatex run
# twice is enough and is guaranteed to be installed alongside the themes.

THEMES  = a b c d
LATEX   = pdflatex -interaction=nonstopmode

.PHONY: all view clean $(THEMES) $(addprefix view-,$(THEMES))

all:
	sh build-all.sh

# Per-theme: two passes, so the page totals and cross-references settle.
$(THEMES):
	cd theme-$@ && $(LATEX) main-$@.tex > build.log 2>&1 \
	           && $(LATEX) main-$@.tex > build.log 2>&1
	@echo "theme $@ → theme-$@/main-$@.pdf"

$(addprefix view-,$(THEMES)): view-%: %
	@start "" theme-$*/main-$*.pdf 2>/dev/null || \
	 xdg-open theme-$*/main-$*.pdf 2>/dev/null || \
	 open theme-$*/main-$*.pdf

clean:
	-rm -f $(addprefix theme-,$(THEMES))
	-for t in $(THEMES); do \
		rm -f theme-$$t/main-$$t.pdf theme-$$t/main-$$t.aux \
		      theme-$$t/main-$$t.log theme-$$t/main-$$t.nav \
		      theme-$$t/main-$$t.out theme-$$t/main-$$t.snm \
		      theme-$$t/main-$$t.toc theme-$$t/main-$$t.vrb \
		      theme-$$t/build.log; \
	done
