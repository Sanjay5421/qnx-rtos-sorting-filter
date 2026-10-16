# Slide deck — LaTeX Beamer

Four themes over the same slides, to pick from before committing to one.
Preview only: title slide, two layout-preview slides, closing slide. No project
content yet — the section format will be filled in once it is decided.

## The four themes

| Folder | Theme | Character |
| --- | --- | --- |
| `theme-a/` | **Navy & Teal** | White slides, a short teal rule above each title. Calm and minimal; colour only on the title and closing panels. |
| `theme-b/` | **Indigo & Violet** | Centred title page, numbered frames, serif body text. Formal — the viva / defence option. |
| `theme-c/` | **Crimson & Amber** | A crimson band across every frame, amber hairlines, full-bleed section panels. Boldest; best in a large room. |
| `theme-d/` | **Graphite & Lime** | Monospaced frame titles (`d.3`), square-cornered boxes and diagrams, one lime accent. Reads as an engineering log. |

All four show the same content, so the differences are purely palette, chrome
and typography.

## Layout

```
presentation/beamer/
├── deck.tex              shared preamble + title metadata (authors, course)
├── content.tex           the slides — theme-independent, no colour named
├── build-all.sh          builds all four themes
├── themes-titles.png     the four title slides side by side
├── themes-preview.png    the four layout previews side by side
├── theme-a/  main-a.tex  theme-a.tex  main-a.pdf
├── theme-b/  main-b.tex  theme-b.tex  main-b.pdf
├── theme-c/  main-c.tex  theme-c.tex  main-c.pdf
└── theme-d/  main-d.tex  theme-d.tex  main-d.pdf
```

`content.tex` names no colour and no font: everything visual comes from the
theme. Adding a theme means copying a `theme-*/` folder, changing the eight
palette values and, if wanted, the chrome templates — the slides do not change.

## Build

```sh
sh build-all.sh          # all four → theme-*/main-*.pdf
cd theme-a && pdflatex main-a.tex   # one of them, twice for page totals
```

Each theme compiles in its own folder; the shared sources are one level up.

## What each theme defines

An element contract, so a new theme only has to fill these in:

- **Palette** — `rtmPrimary`, `rtmAccent`, `rtmAlert`, `rtmGood`, `rtmInk`,
  `rtmMuted`, `rtmLine`, `rtmSurface`.
- **Chrome** — `frametitle` template, `footline` template, `title page`,
  `\AtBeginSection` page, `\closingSlide`.
- **Boxes** — `highlight` (accent rule), `tech` (primary rule), `warn` (alert).
- **Macros** — `\rtmtablehead`, `\concept`, `\num`, `\key`, `\themeSignature`.
- **TikZ** — `block`, `accent block`, `muted block`, `flow`, `flow dashed`.

`content.tex` uses only those names.

## Notes

- **16:9**, `aspectratio=169`, 11 pt.
- The preview slides carry a one-line caption naming the theme, so a printed
  page can be identified. Delete `\themeSignature` from `content.tex` when the
  real deck is written, and delete both preview frames.
- MiKTeX may need `initexmf --set-config-value "[MPM]AutoInstall=1"` once, or a
  first build stalls on a package-install prompt.
