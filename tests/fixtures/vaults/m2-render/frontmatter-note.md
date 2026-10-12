---
engine: pdflatex
packages: [tikz-cd]
preamble: |
  \newenvironment{foo}{}{}
---
# Frontmatter note

\begin{tikzcd}
A \arrow[r] & B
\end{tikzcd}

\begin{foo}
text
\end{foo}
