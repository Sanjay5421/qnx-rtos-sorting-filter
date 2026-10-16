# Slide deck — LaTeX Beamer

Project presentation for **26ELC331 — Real-Time Operating Systems**, Project 10:
*Real-Time Multi-Core Sorting and Median Filter Service*.

Preview only for now: title slide, two layout-preview slides, closing slide.
No project content yet — the section format will be filled in once it is
decided.

| File | What it is |
| --- | --- |
| `main.tex` | the deck shell — `\input`s the other two |
| `content.tex` | the slides. **Edit this one.** |
| `theme.tex` | palette, chrome, boxes, code style, TikZ styles |
| `deck.tex` | preamble and title metadata (authors, course) |
| `main.pdf` | the compiled preview |
| `Makefile` | build, view, clean |

Build with `make`, or `pdflatex main.tex` twice. Two passes are needed the
first time so the page totals settle.

## Theme — Graphite & Lime

Monospaced frame titles numbered like code listings, square corners, muted
greys, and two accents: **lime** for structure (rules, frames, bullets,
numbers) and a **mid blue** for figures and diagrams. The base stays neutral,
so the colour reads as punctuation rather than decoration.

| Role | Name | Hex | Used for |
| --- | --- | --- | --- |
| Primary | `rtmPrimary` | `#23272B` | titles, body text, top band |
| Accent | `rtmAccent` | `#5FA800` | lime rules, frames, the title-page underline |
| Accent ink | `rtmAccentInk` | `#4A8500` | lime for type on white — readable |
| Accent 2 | `rtmAccent2` | `#2C7FB8` | blue: subtitles, figures, arrows |
| Alert | `rtmAlert` | `#C9761B` | warning boxes |
| Good | `rtmGood` | `#2E7D5B` | results |
| Ink / Muted | `rtmInk` `rtmMuted` | `#23272B` `#7A8288` | body text, captions, footer |
| Line | `rtmLine` | `#DDE1E3` | hairlines |
| Surface | `rtmSurface` | `#F1F3F4` | box fills |
| Surface 2 | `rtmSurface2` | `#EAF4FB` | code and figure fills (blue tint) |

Two lime values on purpose: the saturated `rtmAccent` for rules and fills,
where it is a shape, and the darker `rtmAccentInk` for text, where `#5FA800`
would be too light to read on white. Changing either re-skins the deck — no
colour is hard-coded in `content.tex`.

## Layout

- **16:9**, `aspectratio=169`, 11 pt.
- **Frame title**: `d.3` in lime, the title in monospace, a lime rule under it.
- **Footer**: short author names, short title, `[3]/4` with the number in lime.
- **Title and closing slides**: graphite band across the top, lime rule, a
  second lime rule along the bottom.
- **Section pages** come from `\section{…}` automatically.

## Elements available

| Element | Usage |
| --- | --- |
| Bullets | `itemize`, lime marker; sub-items in blue |
| Emphasis | `\concept{…}` lime bold on white, `\alert{…}`, `\num{42.7\%}` |
| Callouts | `highlight` (lime), `tech` (blue), `warn` (amber) — one left rule, no frame |
| Beamer blocks | `block` / `exampleblock` / `alertblock` |
| Table | `booktabs` + `\rtmtablehead{…}` for the header row |
| Code | `lstlisting` (style `rtmc`), line numbers optional |
| Diagram | TikZ: `block` (blue), `accent block` (lime), `muted block` (grey), `flow`, `flow dashed` |
| Takeaway | `\key{…}` lime pill |
| Two columns | `columns` + `onlytextwidth` |

## Next

Each slide is a copy of one of the two preview frames with its content swapped
in. At that point delete the `\themeSignature` lines from `content.tex` and
both preview frames.
