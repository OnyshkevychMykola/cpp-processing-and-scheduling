| scenario | approach | jitter_p99_ms | deadline_misses | underrun_pct | proc_avg_ms | pub_late_avg_ms | data_age_avg_ms | skipped_ticks | jitter_max_ms | data_age_max_ms |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | A | 0.152 | 0 | 0 | 3 | 3.16 | 4.63 | 0 | 0.692 | 5.52 |
| baseline | B | 0.2 | 0 | 0.04 | 3 | 0.136 | 5.49 | 0 | 0.683 | 8.21 |
| delayed | A | 0.172 | 0 | 43.8 | 3 | 1.83 | 12.9 | 0 | 0.709 | 92.1 |
| delayed | B | 0.161 | 0 | 42.3 | 3 | 0.108 | 14.6 | 0 | 0.599 | 96.2 |
| load-ramp | A | 0.179 | 725 | 0 | 8.47 | 8.59 | 9.34 | 1.57e+03 | 0.28 | 22.2 |
| load-ramp | B | 0.189 | 0 | 53 | 7.75 | 0.153 | 17.6 | 0 | 0.61 | 40.2 |
