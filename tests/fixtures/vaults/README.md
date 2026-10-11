# Fixture vaults

`m2-render/` is the static render fixture (M2 slice S1). Tests and demos copy it into a
temporary root via `tests/support/fixture_vault.c`; never write into it.

## Hand-compile record (AC7)

Observed 2026-10-11 with LuaHBTeX 1.24.0 (TeX Live 2026), in a scratch directory outside the
repository, with `m2-render/.cwiki/preamble.tex` copied beside each wrapper. Each block of
`class-note.md` was placed in this wrapper:

```
\documentclass[varwidth]{standalone}
\input{preamble.tex}
\begin{document}
<block>
\end{document}
```

compiled with `lualatex -no-shell-escape -interaction=nonstopmode -file-line-error`.

| Block | Result |
|---|---|
| inline math, display math, `\ce{}`, `tikzpicture`, 5-line `align` | exit 0 |
| `$$\undefinedfixturemacro{x}$$` (note line 23) | exit 1, `Undefined control sequence` on the single block line |

The failing block is on line 23 of `class-note.md`, the only line containing
`\undefinedfixturemacro`. Slice S2 owns the real standalone template and re-asserts this line.
