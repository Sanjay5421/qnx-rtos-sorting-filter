# Slide deck — LaTeX Beamer template

Preview only: title slide, two layout-preview slides, closing slide. No project
content yet — the section format will be filled in once it is decided.

| File | What it is |
| --- | --- |
| `main.tex` | the deck — title block, slides. **Edit this one.** |
| `theme.tex` | palette, frame chrome, boxes, code style. Nothing visual lives in `main.tex`. |
| `main.pdf` | the compiled preview |
| `Makefile` | build, view, clean |

## Build

```sh
make          # → main.pdf
make view     # build and open
make clean    # remove generated files
```

Without `make`: `pdflatex main.tex && pdflatex main.tex` (two passes, so the
page totals settle).

## Colour scheme

Eight values at the top of `theme.tex`. Change those and the whole deck
re-skins — no colour is hard-coded anywhere else.

| Role | Name | Hex | Used for |
| --- | --- | --- | --- |
| Primary | `rtmPrimary` | `#12314F` | titles, title band, table head rules |
| Accent | `rtmAccent` | `#0E9AA7` | rule above each title, bullets, emphasis, numbers |
| Alert | `rtmAlert` | `#E8833A` | warning boxes, caveats |
| Good | `rtmGood` | `#2E9E5B` | completed work, results |
| Ink | `rtmInk` | `#1F2933` | body text |
| Muted | `rtmMuted` | `#6B7A8F` | captions, footer, secondary text |
| Line | `rtmLine` | `#D5DEE7` | hairlines under frame titles |
| Surface | `rtmSurface` | `#EDF2F7` | box fills, code background |

## Layout

- **16:9**, `\documentclass[aspectratio=169, 11pt]{beamer}`.
- **Frame title**: short teal rule, navy title, hairline under it. No band.
- **Footer**: short author names, short title, `n / total`.
- **Title and closing slides**: navy panel across the top, teal rule, details
  below on white (free space for a university logo).
- **Section pages** come from `\section{…}` automatically.

## Elements available

| Element | Usage |
| --- | --- |
| Bullets / sub-bullets | `itemize`, teal marker, quieter sub-level |
| Emphasis | `\concept{…}` teal bold, `\alert{…}`, `\num{42.7\%}` |
| Callouts | `highlight` (teal), `tech` (navy), `warn` (amber) — one left rule, no frame |
| Beamer blocks | `block` / `exampleblock` / `alertblock` |
| Table | `booktabs` + `\rtmtablehead{…}` for the header row |
| Code | `lstlisting` (style `rtmc`), line numbers optional |
| Diagram | TikZ styles `block`, `accent block`, `muted block`, `flow`, `flow dashed` |
| Takeaway | `\key{…}` navy pill |
| Two columns | `columns` + `onlytextwidth` |

## Next

When the section list is fixed, each slide is a copy of one of the two preview
frames with its content swapped in. Delete the preview frames at that point.
