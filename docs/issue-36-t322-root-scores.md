# Issue 36 T322 root scores

Pre-fix root-score dump, before the Task 5 follow-up tie-break and guard-score fixes:

```text
ebaca2a0 450 depth 11 move left scores {'left': -992000.0, 'right': 1568.6333333333332}
ebaca2a0 450 depth 12 move left scores {'left': -992000.0, 'right': 1571.1}
923544bf 322 depth 11 move up scores {'up': -991000.0, 'left': -998000.0}
923544bf 322 depth 12 move up scores {'up': -991000.0, 'left': -998000.0}
```

For 923544bf T322, both replayed `up` and alternative `left` score inside the terminal-loss band (`<= -967000.0`) at depths 11 and 12. This points to survival-step/score-band behavior rather than a case where the evaluator ranks a non-terminal `left` below `up`; treat it as evidence for issue #32 and proceed with the issue-36 evaluator work.

Post-fix Task 5 triage at fixed depth 11 keeps strict `bad_move` labels only for positions where the known-bad root score is strictly below the selected move: `ebaca2a0` T450 and `9f1b79ed` T290. Other critical turns remain informational in the fixture: `9f1b79ed` T298 is an equal terminal-loss tie, `923544bf` T322 is the issue-32 terminal-band case above, `b085baae` T344 is avoided by a terminal-loss tie-break but root scores tie, and `e1265a85` T290 has only one legal move.

Post-fix root-score dump after dead-region leaf evaluation and the production-budget performance pass:

```text
ebaca2a0 450 depth 11 move right scores {'left': -992000.0, 'right': 1988.6333333333332}
ebaca2a0 450 depth 12 move right scores {'left': -992000.0, 'right': 1995.1}
923544bf 322 depth 11 move up scores {'up': -991000.0, 'left': -998000.0}
923544bf 322 depth 12 move up scores {'up': -991000.0, 'left': -998000.0}
```

The horizon-miss case `ebaca2a0` T450 now avoids the losing `left` move at depths 11 and 12. `923544bf` T322 is unchanged: both candidate root moves remain terminal-band losses, so that position remains evidence for issue #32 rather than issue #36.
