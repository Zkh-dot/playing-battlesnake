# Issue 36 T322 root scores

```text
ebaca2a0 450 depth 11 move left scores {'left': -992000.0, 'right': 1568.6333333333332}
ebaca2a0 450 depth 12 move left scores {'left': -992000.0, 'right': 1571.1}
923544bf 322 depth 11 move up scores {'up': -991000.0, 'left': -998000.0}
923544bf 322 depth 12 move up scores {'up': -991000.0, 'left': -998000.0}
```

For 923544bf T322, both replayed `up` and alternative `left` score inside the terminal-loss band (`<= -967000.0`) at depths 11 and 12. This points to survival-step/score-band behavior rather than a case where the evaluator ranks a non-terminal `left` below `up`; treat it as evidence for issue #32 and proceed with the issue-36 evaluator work.
