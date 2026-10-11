# Render fixture

Inline math $E = mc^2$ sits in prose.

$$\int_0^1 x^2 \, dx = \frac{1}{3}$$

Chemistry: \ce{2H2 + O2 -> 2H2O}

\begin{tikzpicture}
  \draw (0,0) circle (1);
\end{tikzpicture}

\begin{align}
  a &= b + c \\
  b &= d + e \\
  d &= f + g \\
  e &= h + i \\
  c &= j
\end{align}

Failing block:

$$\undefinedfixturemacro{x}$$
