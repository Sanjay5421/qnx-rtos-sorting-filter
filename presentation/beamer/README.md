# Slide deck — LaTeX Beamer

Project presentation for **26ELC331 — Real-Time Operating Systems**, Project 10:
*Real-Time Multi-Core Sorting and Median Filter Service*.

38 frames: introduction, system overview, methodology, results, use cases.

| File | What it is |
| --- | --- |
| `main.tex` | the shell — `\input`s the three below |
| `content.tex` | the slides. **Edit this one.** |
| `diagrams.tex` | diagram slides and the layout helpers (`\columnsimage`, `\boardPhoto`) |
| `theme.tex` | palette, chrome, boxes, code style, TikZ styles |
| `deck.tex` | preamble and title metadata (authors, course) |
| `assets/` | console screenshots, cropped for the deck |
| `Console-outputs/` | the original, uncropped screenshots and the raw serial logs |
| `main.pdf` | the compiled deck |
| `Makefile` | build, view, clean |

Build with `make`, or `pdflatex main.tex` twice. Two passes are needed so the
outline and page totals settle.

## Theme — Graphite & Lime

Monospaced frame titles numbered like code listings, square corners, muted
greys, and two accents: **lime** for structure and a **mid blue** for figures
and diagrams.

| Role | Name | Hex | Used for |
| --- | --- | --- | --- |
| Primary | `rtmPrimary` | `#23272B` | titles, body text, top band |
| Accent | `rtmAccent` | `#5FA800` | lime rules, frames, title underline |
| Accent ink | `rtmAccentInk` | `#4A8500` | lime for type on white |
| Accent 2 | `rtmAccent2` | `#2C7FB8` | blue: subtitles, figures, arrows |
| Alert / Good | `rtmAlert` `rtmGood` | `#C9761B` `#2E7D5B` | warnings, results |
| Ink / Muted | `rtmInk` `rtmMuted` | `#23272B` `#7A8288` | body text, captions, footer |
| Line / Surface / Surface 2 | `rtmLine` `rtmSurface` `rtmSurface2` | `#DDE1E3` `#F1F3F4` `#EAF4FB` | hairlines, box fills, code fills |

Two lime values on purpose: saturated `rtmAccent` for shapes, darker
`rtmAccentInk` for text, where `#5FA800` is too light to read.

## Fitting rules

The frame title and the lists are spaced tightly in `theme.tex` — the title
block is on every frame, so the height it costs is height the content loses.
When adding a slide, keep to these and it will fit:

- 4–5 bullets of one to two lines, or two columns of the same.
- One callout box at the bottom of a column, two at most, one to two lines each.
- Console screenshots are placed by `\columnsimage{name}{width}{caption}` with
  the image capped at `0.52\textheight`; the caption sits beside it at
  `\scriptsize`. Anything larger clips.

`pdflatex` prints `Overfull \vbox` when a frame runs past the slide. A few
points is invisible; more than about 10 pt clips the last line, so check the
log after editing.

## Placeholders

Two frames are waiting on material from elsewhere, and both are marked:

- **`\boardPhoto`** in `diagrams.tex` draws a labelled box. Replace the node
  with `\includegraphics[width=\linewidth]{assets/board.jpg}` when the board
  photograph is available.
- The **diagrams** (`\systemOverviewSlide`, `\clientFlowchartSlide`,
  `\serverFlowchartSlide`, `\dataFlowSlide`) are drafted in `diagrams.tex` and
  are being drawn separately — each macro can be replaced on its own without
  touching `content.tex`.

## Console evidence

`assets/` holds eleven crops of the board's serial console, one per command
or test: boot, the four test suites, both `cksum` values, the loader and
sensor demos, the two pipeline layouts, and the worker sweep. The filenames
say which is which; `Console-outputs/` holds the originals alongside the two
full serial logs they were taken from.
